// Packages
let app = module.exports = require('./mykoa.js')();
let ref        = require('ref');
let ffi        = require('ffi');
var Struct     = require('ref-struct');
let fs         = require('fs');
let mkdirp     = require('mkdirp');
let send       = require('koa-send');
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

app.post('/get_seeds', null, {
        bucket: { type: 'string' },
        path_pre: { type: 'string'},
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
