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

// Old spawner version that calculates new seeds on demand as fallback. Slower in general because
// it has to download and decompress segmentation.lzma and interface with the spawner C library.
// Also connected components becomes somewhat slow for a large amount of selected supervoxels.
function* oldSpawner(bucket, path_pre, path_post, segments, match_ratio) {
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