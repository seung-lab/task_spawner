// Packages
let app = module.exports = require('./mykoa.js')();
let Promise    = require('bluebird');
let ref        = require('ref');
let ffi        = require('ffi');
let Struct     = require('ref-struct');
let fs         = require('fs');
let mkdirp     = require('mkdirp');
let send       = require('koa-send');
let rp         = require('request-promise');
let lzma       = require('lzma-native');     // one time decompression of segmentation
let lz4        = require('lz4');             // (de)compression of segmentation from/to redis
let protobuf   = require('protobufjs');

let NodeRedis  = require('redis');           // cache for volume data (metadata, segment bboxes and sizes, segmentation)
let redis = NodeRedis.createClient('6379', '127.0.0.1', {return_buffers: true});

Promise.promisifyAll(NodeRedis.RedisClient.prototype);
Promise.promisifyAll(NodeRedis.Multi.prototype);

// Typedefs
let UCharPtr = ref.refType(ref.types.uchar);
let UInt32Ptr = ref.refType(ref.types.uint32);

let Segment = Struct({
    'id': ref.types.uint32,
    'size': ref.types.uint32
});

let SpawnSeed = Struct({
    'segmentCount': ref.types.uint32,
    'segments': ref.refType(Segment)
});

let CTaskSpawner = Struct({
    'spawnSetCount': ref.types.uint32,
    'seeds': ref.refType(SpawnSeed)
});

let CInputVolume = Struct({
    'metadata': "string",
    'bboxesLength': ref.types.uint32,
    'bboxes': UCharPtr,
    'sizesLength': ref.types.uint32,
    'sizes': UCharPtr,
    'segmentationLength': ref.types.uint32,
    'segmentation': UCharPtr
});

let CTaskSpawnerPtr = ref.refType(CTaskSpawner);
let CInputVolumePtr = ref.refType(CInputVolume);

let TaskSpawnerLib = ffi.Library('../lib/libspawner', {
    // CTaskSpawner * TaskSpawner_Spawn(CInputVolume * pre, CInputVolume * post, uint32_t * segments, uint32_t segmentCount, double matchRatio);
    "TaskSpawner_Spawn": [ CTaskSpawnerPtr, [ CInputVolumePtr, CInputVolumePtr, UInt32Ptr, "uint32", "double" ] ],

    // void      TaskSpawner_Release(CTaskSpawner * taskspawner);
    "TaskSpawner_Release": [ "void", [ CTaskSpawnerPtr ] ],
});

let SpawnTableDef;
protobuf.load("../res/spawnset.proto").then(function(root) {
    SpawnTableDef = root.lookupType("ew.spawner.SpawnTable");
}).catch(function(err) {
    console.log("Couldn't load protobuf definitions: " + err);
});

function strMapToObj(strMap) {
    const obj = Object.create(null);
    for (const [key, val] of strMap) {
        obj[key] = val;
    }
    return obj;
}

function validateMetadata(metadataString) { // Todo: more checks, better error handling
    try {
        var metadata = JSON.parse(metadataString);
        let errors = "";
        let warnings = "";
        if (!metadata["segment_id_type"]) {
            errors += "Error: segment_id_type not specified\n";
        }
        if (!metadata["affinity_type"]) {
            warnings += "Warning: affinity_type not specified\n";
        }
        if (!metadata["bounding_box_type"]) {
            errors += "Error: bounding_box_type not specified\n";
        }
        if (!metadata["size_type"]) {
            errors += "Error: size_type not specified\n";
        }
        if (!metadata["image_type"]) {
            errors += "Error: image_type not specified\n";
        }
        if (!metadata["num_segments"]) {
            errors += "Error: num_segments not specified\n";
        }
        if (!metadata["num_edges"]) {
            warnings += "Warning: num_edges not specified\n";
        }
        if (!metadata["chunk_voxel_dimensions"]) {
            errors += "Error: chunk_voxel_dimensions not specified\n";
        }
        if (!metadata["voxel_resolution"]) {
            errors += "Error: voxel_resolution not specified\n";
        }
        if (!metadata["resolution_units"]) {
            warnings += "Warning: resolution_units not specified\n";
        }
        if (!metadata["physical_offset_min"]) {
            errors += "Error: physical_offset_min not specified\n";
        }
        if (!metadata["physical_offset_max"]) {
            errors += "Error: physical_offset_max not specified\n";
        }

        console.log(warnings);
        if (errors != "") {
            console.log(errors);
            return false;
        }
        return true;
    }
    catch (err) {
        console.log(err.message);
        return false;
    }
}

function generateSpawnCandidates(pre, post, segments, matchRatio) {
    let segmentsTA = new Uint32Array(segments);
    let segmentsBuffer = Buffer.from(segmentsTA.buffer);
    segmentsBuffer.type = ref.types.uint32;

    //console.log(pre);

    let pre_vol = new CInputVolume(
        { metadata:            pre.metadata,
          bboxesLength:        pre.bounds.length,
          bboxes:              pre.bounds,
          sizesLength:         pre.sizes.length,
          sizes:               pre.sizes,
          segmentationLength:  pre.segmentation.length,
          segmentation:        pre.segmentation
        });
    
    let post_vol = new CInputVolume(
        { metadata:            post.metadata,
          bboxesLength:        post.bounds.length,
          bboxes:              post.bounds,
          sizesLength:         post.sizes.length,
          sizes:               post.sizes,
          segmentationLength:  post.segmentation.length,
          segmentation:        post.segmentation
        });

    let spawnSets = TaskSpawnerLib.TaskSpawner_Spawn(pre_vol.ref(), post_vol.ref(), segmentsBuffer, segmentsTA.length, matchRatio);

    return spawnSets;
}


/* cachedFetch

 * Input: request-promise input, e.g. { url: path, encoding: null }
 * 
 * Description: Checks if the requested object exists in cache (using path as key).
 *              If not, download it and send it gzipped to cache, otherwise retrieve and decompress it from cache.
 *              LZMA segmentation (file ends with .lzma) will be decompressed first before it is recompressed with
 *              gzip and send to cache (trading a slight increase in file size for major speedup).
 * 
 * Returns: Buffer
 */
function cachedFetch(request) {
    return redis.getAsync(request.url)
        .then(function (value) {
            if (value !== null) {
                let decoded_resp = lz4.decode(value);
                console.log(request.url + " successfully retrieved from cache.");
                return decoded_resp;
            }
            else
            {
                console.log(request.url + " not in cache. Downloading ...");
                return rp(request)
                .then(function (resp) {
                    console.log(request.url + " successfully downloaded.");
                    if (request.url.endsWith(".lzma")) {
                        console.log("Decompressing " + request.url);
                        return lzma.decompress(resp);
                    }
                    else {
                        return resp;
                    }
                })
                .then(function (decoded_resp) {
                    compressed_resp = lz4.encode(decoded_resp);
                    console.log(request.url + " compressed (Ratio: " + (100.0 * compressed_resp.byteLength / decoded_resp.byteLength).toFixed(2) + " %)");
                    return redis.setAsync(request.url, compressed_resp)
                    .then(function () {
                        console.log(request.url + " sent to cache.");
                        return decoded_resp;
                    })
                    .catch(function (err) {
                        console.log("Caching " + request.url + " failed: " + err);
                        return decoded_resp;
                    });
                })
                .catch(function (err) {
                    throw new Error("Aquiring " + request.url + " failed: " + err);
                });
            }
        })
        .catch(function (err) {
            throw new Error("Unknown redis error when loading " + request.url + ": " + err);
        });
}

app.post('/get_segment_data', null, {
    bucket: { type: 'string' },
    path: { type: 'string' },
    segments: {
        type: 'array',
        itemType: 'int',
        rule: { min: 0 }
    }
}, function* () {
    let { bucket, path, segments } = this.params;
    let segmentation_path = `https://storage.googleapis.com/${bucket}/${path}`;
    let _this = this;

    let requests = [
        { url: segmentation_path + 'metadata.json' },
        { url: segmentation_path + 'segmentation.size', encoding: null }, // "encoding: null" is request's cryptic way of saying: binary
        { url: segmentation_path + 'segmentation.bbox', encoding: null }
    ];

    // Bluebird.map ensures order of responses equals order of requests
    yield Promise.map(requests, function (request) {
        console.log("Request: " + request.url);
        return cachedFetch(request);
    }).then(function(responses) {
        let meta = JSON.parse(responses[0].toString());
        let resp_sizes = responses[1];
        let resp_bounds = responses[2];
        let sizesView, boxesView;

        if (meta.size_type == "UInt8") {
            sizesView = new Uint8Array(resp_sizes.buffer);
        } else if (meta.size_type == "UInt16") {
            sizesView = new Uint16Array(resp_sizes.buffer);     
        } else if (meta.size_type == "UInt32") {
            sizesView = new Uint32Array(resp_sizes.buffer);
        } else  {
            throw new Error("Segment size type not supported.");
        }

        if (meta.bounding_box_type == "UInt8") {
            boxesView = new Uint8Array(resp_bounds.buffer);
        } else if (meta.bounding_box_type == "UInt16") {
            boxesView = new Uint16Array(resp_bounds.buffer);
        } else if (meta.bounding_box_type == "UInt32") {
            boxesView = new Uint32Array(resp_bounds.buffer);
        } else  {
            throw new Error("Bounding box type not supported.");
        }

        // Note: Segment Bounding Boxes are given relative to the volume, in voxel. But we want them in physical coordinates in world space.
        let resolution = meta.voxel_resolution;
        let offset = meta.physical_offset_min;

        let result = {};
        for (let i = 0; i < segments.length; ++i) {
            let segID = segments[i];
            let size = sizesView[segID];
            let bounds = {
                "min": {
                    "x": offset[0] + resolution[0] * boxesView[6 * segID + 0],
                    "y": offset[1] + resolution[1] * boxesView[6 * segID + 1],
                    "z": offset[2] + resolution[2] * boxesView[6 * segID + 2]
                },
                "max": {
                    "x": offset[0] + resolution[0] * boxesView[6 * segID + 3],
                    "y": offset[1] + resolution[1] * boxesView[6 * segID + 4],
                    "z": offset[2] + resolution[2] * boxesView[6 * segID + 5]
                }
            };
            result[segID] = { "size": size, "bounds": bounds };
        }

        _this.body = JSON.stringify(result);
    });

});

// get_seeds function that returns seed segments based on a precomputed spawntable. Very fast.
app.post('/get_seeds', null, {
    bucket: { type: 'string' },
    path_pre: { type: 'string' },
    path_post: { type: 'string'},
    segments: {
        type: 'array',
        itemType: 'int',
        rule: { min: 0 }
    },
    match_ratio: {
        required: false,
        type: 'number',
        min: 0.5,
        max: 1.0
    }
}, function* () {
    let { bucket, path_pre, path_post, segments, match_ratio } = this.params;
    match_ratio = match_ratio || 0.6;

    const _this = this;
    const pre_segmentation_path = `https://storage.googleapis.com/${bucket}/${path_pre}`;
    const spawntable_path = pre_segmentation_path + path_post.match(/([^\/]*)\/*$/)[1] + '.pb.spawn';

    console.time("get_seeds for " + spawntable_path);
    console.time("loading " + spawntable_path);

    const requests = [
        { url: spawntable_path, encoding: null }, // "encoding: null" is request's cryptic way of saying: binary
    ];

    // Bluebird.map ensures order of responses equals order of requests
    yield Promise.map(requests, function (request) {
        console.log("Request: " + request.url);
        return cachedFetch(request);
    }).then(function(responses) {
        console.timeEnd("loading " + spawntable_path);
        console.time("parsing " + spawntable_path);
        const spawnMapEnc = responses[0];
        const spawnMapDec = SpawnTableDef.decode(spawnMapEnc);
        const spawnMap = spawnMapDec.preSpawnMap;
        const regionGraph = spawnMapDec.postRegionGraph;
        const version = spawnMapDec.version || 0;
        console.timeEnd("parsing " + spawntable_path);

        if (version !== 1) {
            throw(TypeError(`${spawntable_path} version is ${version}, but 1 expected!`));
        }

        // Retrieve post-side candidates for selection
        const selection = new Set(segments);
        const postCandidates = new Map();
        for (const preKey of selection) {
            if (spawnMap[preKey]) {
                for (const postCandidate of spawnMap[preKey].postSideCounterparts) {
                    if (postCandidates.has(postCandidate.id) === false) {
                        postCandidates.set(postCandidate.id, { segment: postCandidate, groupID: -1 });
                    } else if (postCandidate.canSpawn) {
                        // canSpawn is a postCandidate property that might be different for each selected segment.
                        // Allow post-side segment to spawn if one of the postCandidates canSpawn properties is true. 
                        postCandidates.get(postCandidate.id).segment.canSpawn = true;
                    }
                }
            }
        }

        // Connected components using BFS on post-side candidates
        let postGroups = [];
        for (const segID of postCandidates.keys()) {
            if (postCandidates.get(segID).groupID !== -1) {
                continue;
            }

            const queue = [segID];
            const groupID = postGroups.length;
            postGroups[groupID] = new Set();

            while (queue.length > 0) {
                const seg = queue.pop();
                postCandidates.get(seg).groupID = groupID;
                postGroups[groupID].add(seg);
                if (regionGraph[seg]) {
                    for (const regiongraphNeighbor of regionGraph[seg].postSideNeighbors) {
                        if (postCandidates.get(regiongraphNeighbor.id) && postCandidates.get(regiongraphNeighbor.id).groupID === -1) {
                            queue.push(regiongraphNeighbor.id);
                        }
                    }
                }
            }
        }

        let result = [];
        // Remove segments that don't have enough coverage
        for (const group of postGroups) {
            const groupID = result.length;
            let bestMatch = null;
            for (const postSegID of group) {
                const postSeg = postCandidates.get(postSegID).segment;
                const requiredSize = match_ratio * postSeg.overlapSize;
                let accumSize = 0;
                for (const preSeg of postSeg.preSideSupports) {
                    if (selection.has(preSeg.id)) {
                        accumSize += preSeg.intersectionSize;
                    }
                }

                if (accumSize >= requiredSize) {
                    result[groupID] = result[groupID] || new Map();
                    result[groupID].set(postSeg.id, postSeg.overlapSize);
                }

                // Store best (valid) match in case we don't find a single valid post-segment for this spawn group
                const matchScore = (accumSize + 1000.0) / (postSeg.overlapSize + 2000.0);
                if (postSeg.canSpawn && (!bestMatch || matchScore > bestMatch.score)) {
                    bestMatch = { segID: postSeg.id, score: matchScore, size: postSeg.overlapSize, mappedSize: accumSize }
                }
            }
            // No valid post-segment found, so use the best match we got (if any)
            if (!result[groupID] && bestMatch) {
                result[groupID] = new Map();
                result[groupID].set(bestMatch.segID, bestMatch.size);
                console.log("No perfect seed found. Chose seg " + bestMatch.segID + " with " + bestMatch.mappedSize + " / " + bestMatch.size + " voxels matching.");
            }
        }

        // Remove groups that *only* consist of segments not allowed to spawn (dust, or near boundary)
        result = result.filter((group) => { return [...group.keys()].some((postSegID) => { return postCandidates.get(postSegID).segment.canSpawn === true }) });

        const result_str = JSON.stringify(result.map((map) => { return strMapToObj(map) }));
        console.log(result_str)
        console.timeEnd("get_seeds for " + spawntable_path);
        _this.body = result_str;

    })
    .catch (function (err) {
        console.log("get_seeds failed: " + err);
        console.log("Params were: " + JSON.stringify(_this.params));

        _this.status = 400;
        _this.body = err.message;
    });

});

// Old spawner version that calculates new seeds on demand as fallback. Slower in general because
// it has to download and decompress segmentation.lzma and interface with the spawner C library.
// Also connected components becomes somewhat slow for a large amount of selected supervoxels.
app.post('/get_seeds_old', null, {
    bucket: { type: 'string' },
    path_pre: { type: 'string' },
    path_post: { type: 'string'},
    segments: {
        type: 'array',
        itemType: 'int',
        rule: { min: 0 }
    },
    match_ratio: {
        required: false,
        type: 'number',
        min: 0.5,
        max: 1.0
    }
}, function* () {
    let { bucket, path_pre, path_post, segments, match_ratio } = this.params;
    match_ratio = match_ratio || 0.6;

    let pre_segmentation_path = `https://storage.googleapis.com/${bucket}/${path_pre}`;
    let post_segmentation_path = `https://storage.googleapis.com/${bucket}/${path_post}`;
    let _this = this;

    let requests = [
        { url: pre_segmentation_path + 'metadata.json' },
        { url: pre_segmentation_path + 'segmentation.bbox', encoding: null }, // "encoding: null" is request's cryptic way of saying: binary
        { url: pre_segmentation_path + 'segmentation.size', encoding: null },
        { url: pre_segmentation_path + 'segmentation.lzma', encoding: null }, 
        { url: post_segmentation_path + 'metadata.json' },
        { url: post_segmentation_path + 'segmentation.bbox', encoding: null },
        { url: post_segmentation_path + 'segmentation.size', encoding: null },
        { url: post_segmentation_path + 'segmentation.lzma', encoding: null }
    ];

    // Bluebird.map ensures order of responses equals order of requests
    console.time("Retrieving files for " + path_pre + " and " + path_post);
    yield Promise.map(requests, function (request) {
        console.log("Request: " + request.url);
        return cachedFetch(request);
    }).then(function(responses) {
        console.timeEnd("Retrieving files for " + path_pre + " and " + path_post);
        if (!validateMetadata(responses[0].toString()) ||
          !validateMetadata(responses[4].toString())) {
          _this.status = 400;
          _this.body = "Metadata validation failed."
          return;
        }

        let pre = {
            metadata: responses[0].toString(),
            bounds: responses[1],
            sizes: responses[2],
            segmentation: responses[3]
        };

        let post = {
            metadata: responses[4].toString(),
            bounds: responses[5],
            sizes: responses[6],
            segmentation: responses[7]
        };
        console.time("get_seeds for " + path_pre + " to " + path_post);
        let taskSpawnerPtr = generateSpawnCandidates(pre, post, segments, match_ratio);
        let taskSpawner = taskSpawnerPtr.deref();
        var result = [];

        if (taskSpawner.spawnSetCount > 0) {
            let spawnSetArray = taskSpawner.seeds.ref().readPointer(0, taskSpawner.spawnSetCount * SpawnSeed.size);
            
            for (let i = 0; i < taskSpawner.spawnSetCount; ++i) {
                let spawnSet = ref.get(spawnSetArray, i * SpawnSeed.size, SpawnSeed);
                if (spawnSet.segmentCount > 0) {
                    let segmentArray = spawnSet.segments.ref().readPointer(0, spawnSet.segmentCount * Segment.size);
                    
                    let set = {}
                    for (let j = 0; j < spawnSet.segmentCount; ++j) {
                        let segment = ref.get(segmentArray, j * Segment.size, Segment);
                        set[segment.id] = segment.size;
                    }
                    result.push(set);
                }
            }
        }

        TaskSpawnerLib.TaskSpawner_Release(taskSpawnerPtr);

        console.timeEnd("get_seeds for " + path_pre + " to " + path_post);
        _this.body = JSON.stringify(result);

    })
    .catch (function (err) {
        console.log("get_seeds failed: "  + err);
        _this.status = 400;
        _this.body = err.message;
    });

});
