// Packages
let app = module.exports = require('./mykoa.js')();
let ref        = require('ref');
let ffi        = require('ffi');
let Struct     = require('ref-struct');
let fs         = require('fs');
let mkdirp     = require('mkdirp');
let send       = require('koa-send');
let Bluebird   = require('bluebird');
let rp         = require('request-promise');
//let gcs        = require('@google-cloud/storage')();

// Typedefs
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

let CTaskSpawnerPtr = ref.refType(CTaskSpawner);

let UInt8Ptr = ref.refType(ref.types.uint8);
let UInt16Ptr = ref.refType(ref.types.uint16);
let UInt32Ptr = ref.refType(ref.types.uint32);
let SizeTPtr = ref.refType(ref.types.size_t);
let CharPtr = ref.refType(ref.types.char);
let CharPtrPtr = ref.refType(ref.types.CString);

let TaskSpawnerLib = ffi.Library('../lib/libspawner', {
    // CTaskSpawner * TaskSpawner_Spawn(char * pre, char * post, uint32_t pre_segment_count, uint32_t * pre_segments);
    "TaskSpawner_Spawn": [ CTaskSpawnerPtr, [ "string", "string", "uint32", UInt32Ptr ] ],

    // void      TaskSpawner_Release(CTaskSpawner * taskspawner);
    "TaskSpawner_Release": [ "void", [ CTaskSpawnerPtr ] ],
});

function generateSpawnCandidates(pre_segmentation_path, post_segmentation_path, segments) {
    let segmentsTA = new Uint32Array(segments);
    let segmentsBuffer = Buffer.from(segmentsTA.buffer);
    segmentsBuffer.type = ref.types.uint32;
    let spawnSets = TaskSpawnerLib.TaskSpawner_Spawn(pre_segmentation_path, post_segmentation_path, segmentsTA.length, segmentsBuffer);

    return spawnSets;
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

        let {bucket, path, segments} = this.params;
        let segmentation_path = `https://storage.googleapis.com/${bucket}/${path}`;

        let req_meta           = rp({ url: segmentation_path + 'metadata.json' });
        let req_options_sizes  = rp({ url: segmentation_path + 'segmentation.size', encoding : null });
        let req_options_bounds = rp({ url: segmentation_path + 'segmentation.bbox', encoding : null });

        var that = this;
        yield Bluebird.all([req_meta, req_options_sizes, req_options_bounds])
            .spread(function (resp_meta, resp_sizes, resp_bounds) {
                meta = JSON.parse(resp_meta);
                
                //sizes = new ArrayBuffer(resp_sizes);
                //bounds = new ArrayBuffer(resp_bounds);

                let sizesView, boxesView
                if (meta.size_type == "UInt16") {
                    sizesView = new Uint16Array(resp_sizes.buffer);
                    
                } else if (meta.size_type == "UInt32") {
                    sizesView = new Uint32Array(resp_sizes.buffer);
                }

                if (meta.bounding_box_type == "UInt8") {
                    boxesView = new Uint8Array(resp_bounds.buffer);
                } else if (meta.bounding_box_type == "UInt16") {
                    boxesView = new Uint16Array(resp_bounds.buffer);
                    console.log(boxesView[0]);
                }
                else if (meta.bounding_box_type == "UInt32") {
                    boxesView = new Uint32Array(resp_bounds.buffer);
                }


                let result = {};
                for (let i = 0; i < segments.length; ++i) {
                    let segID = segments[i];
                    let size = sizesView[segID];
                    let bounds = {
                        "min": {
                            "x": boxesView[6 * segID + 0],
                            "y": boxesView[6 * segID + 1],
                            "z": boxesView[6 * segID + 2]
                        },
                        "max": {
                            "x": boxesView[6 * segID + 3],
                            "y": boxesView[6 * segID + 4],
                            "z": boxesView[6 * segID + 5]
                        }
                    };
                    result[segID] = { "size": size, "bounds": bounds };
                }
                //console.log(result);
                console.log("settingbody");
                that.body = JSON.stringify(result);

            })
            .catch(function (err) {
                console.log("Yikes: " + err);
            });

            console.log("IM LEAVING");
    

});

app.post('/get_seeds', null, {
        bucket: { type: 'string' },
        path_pre: { type: 'string' },
        path_post: { type: 'string'},
        segments: {
            type: 'array',
            itemType: 'int',
            rule: { min: 0 }
        }
    }, function* () {

        let start = Date.now();
        let {bucket, path_pre, path_post, segments} = this.params;

        //let pre_segmentation_path = `/mnt/${bucket}_bucket/${path_pre}segmentation.lzma`;
        //let post_segmentation_path = `/mnt/${bucket}_bucket/${path_post}segmentation.lzma`;
        let pre_segmentation_path = `https://storage.googleapis.com/${bucket}/${path_pre}`;
        let post_segmentation_path = `https://storage.googleapis.com/${bucket}/${path_post}`;

        let taskSpawnerPtr = generateSpawnCandidates(pre_segmentation_path, post_segmentation_path, segments);
        let taskSpawner = taskSpawnerPtr.deref();

        let spawnSetArray = taskSpawner.seeds.ref().readPointer(0, taskSpawner.spawnSetCount * SpawnSeed.size);

        var result = [];
        for (let i = 0; i < taskSpawner.spawnSetCount; ++i) {
          let spawnSet = ref.get(spawnSetArray, i * SpawnSeed.size, SpawnSeed);
          let segmentArray = spawnSet.segments.ref().readPointer(0, spawnSet.segmentCount * Segment.size);

          let set = {}
          for (let j = 0; j < spawnSet.segmentCount; ++j) {
            let segment = ref.get(segmentArray, j * Segment.size, Segment);
            set[segment.id] = segment.size;
          }
          result.push(set);

        }

        this.body = JSON.stringify(result);

});
