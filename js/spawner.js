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
let protobuf   = require('protobufjs');

let old_spawner = require('./old_spawner.js');

let NodeRedis  = require('redis');           // cache for volume data (metadata, segment bboxes and sizes, segmentation)
let redis = NodeRedis.createClient('6379', '127.0.0.1', {return_buffers: true});

Promise.promisifyAll(NodeRedis.RedisClient.prototype);
Promise.promisifyAll(NodeRedis.Multi.prototype);

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
        const spawnMap = SpawnTableDef.decode(spawnMapEnc).entries;
        console.timeEnd("parsing " + spawntable_path);

        // Filter selected segments in overlapping region, attach a default group id to each segment
        const roiSegments = new Map(segments.filter((segID) => { return spawnMap[segID] }).map((segID) => { return [segID, -1] }));

        // Connected components using BFS
        let roiSegGroups = [];
        for (const segID of roiSegments.keys()) {
            if (roiSegments.get(segID) !== -1) {
                continue;
            }

            const queue = [segID];
            const groupID = roiSegGroups.length;
            roiSegGroups[groupID] = new Set();

            while (queue.length > 0) {
                const seg = queue.pop();
                roiSegments.set(seg, groupID);
                roiSegGroups[groupID].add(seg);
                for (const regiongraphNeighbor of spawnMap[seg].preSideNeighbors) {
                    if (roiSegments.get(regiongraphNeighbor.id) === -1) {
                        queue.push(regiongraphNeighbor.id);
                    }
                }
            }
        }

        // Remove connected groups not allowed to spawn (only contains dust, very flat boundary segments, ...)
        roiSegGroups = roiSegGroups.filter((group) => { return [...group].some((segID) => { return spawnMap[segID].canSpawn === true }) });

        // Retrieve post side equivalents for each group:
        const result = [];
        for (const group of roiSegGroups) {
            const groupID = result.length;
            let bestMatch = null;
            for (const preKey of group) {
                for (const postSeg of spawnMap[preKey].postSideCounterparts) {
                    // Shortcut if post-side segment was already spawned by another pre-side segment
                    if (result[groupID] && result[groupID].has(postSeg.id)) {
                        continue;
                    }

                    // Check if enough pre-side segments are selected to spawn this post-side segment
                    const requiredSize = match_ratio * postSeg.overlapSize;
                    let accumSize = 0;
                    for (const preSeg of postSeg.preSideSupports) {
                        if (roiSegments.has(preSeg.id)) {
                            accumSize += preSeg.intersectionSize;
                        }
                    }
                    if (accumSize >= requiredSize) {
                        result[groupID] = result[groupID] || new Map();
                        result[groupID].set(postSeg.id, postSeg.overlapSize);
                    }

                    // Store best match in case we don't find a single valid post-segment for this spawn group
                    const matchScore = (accumSize + 1000.0) / (postSeg.overlapSize + 2000.0);
                    if (!bestMatch || matchScore > bestMatch.score) {
                        bestMatch = { segID: postSeg.id, score: matchScore, size: postSeg.overlapSize, mappedSize: accumSize }
                    }
                }
            }

            // No valid post-segment found, so use the best match we got
            if (!result[groupID] && bestMatch) {
                result[groupID] = new Map();
                result[groupID].set(bestMatch.segID, bestMatch.size);
                console.log("No perfect seed found. Chose seg " + bestMatch.segID + " with " + bestMatch.mappedSize + " / " + bestMatch.size + " voxels matching.");
            }
        }
        const result_str = JSON.stringify(result.map((map) => { return strMapToObj(map) }));
        console.log(result_str)
        console.timeEnd("get_seeds for " + spawntable_path);
        _this.body = result_str;

    })
    .catch (function (err) {
        console.log("get_seeds failed: " + err);
        console.log("Params were: " + JSON.stringify(_this.params));

        old_spawner.old_spawner(blah);

        _this.status = 400;
        _this.body = err.message;
    });

});
