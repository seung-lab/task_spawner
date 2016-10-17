#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>

#include "Volume.h"
#include "SpawnHelper.h"

struct CInputVolume {
  char * metadata;

  uint32_t bboxesLength;
  char * bboxes;
  
  uint32_t sizesLength;
  char * sizes;
  
  uint32_t segmentationLength;
  char * segmentation;
};

class CTaskSpawner {
private:
  struct Segment {
    uint32_t id;
    uint32_t size;
  };

  struct SpawnSeed {
    uint32_t  segmentCount;
    Segment  *segments;
  };


public:
  uint32_t    spawnSetCount;
  SpawnSeed  *seeds;

  CTaskSpawner(const std::vector<std::map<uint32_t, uint32_t>> & seeds_) {
    spawnSetCount = seeds_.size();
    if (spawnSetCount > 0) {
      seeds = new SpawnSeed[spawnSetCount]; 
      for (uint32_t i = 0; i < spawnSetCount; ++i) {
        seeds[i].segmentCount = seeds_[i].size();
        seeds[i].segments = new Segment[seeds[i].segmentCount];

        uint32_t j = 0;
        for (auto const &seedset : seeds_[i]) {
          seeds[i].segments[j].id = seedset.first;
          seeds[i].segments[j].size = seedset.second;
          ++j;
        }
      }
    }
  }

  ~CTaskSpawner() {
    for (uint32_t i = 0; i < spawnSetCount; ++i) {
      seeds[i].segmentCount = 0;
      delete[] seeds[i].segments;
      seeds[i].segments = nullptr;
    }
    spawnSetCount = 0;
    delete[] seeds;
    seeds = nullptr;
  }
};


extern "C" CTaskSpawner * TaskSpawner_Spawn(CInputVolume * pre, CInputVolume * post, uint32_t * segments, uint32_t segmentCount) {
  std::set<uint32_t> selected(segments, segments + segmentCount);

  size_t metadataLength = strlen(pre->metadata);
  std::vector<unsigned char> pre_meta_vec(pre->metadata, pre->metadata + metadataLength);
  
  metadataLength = strlen(post->metadata);
  std::vector<unsigned char> post_meta_vec(post->metadata, post->metadata + metadataLength);

  std::vector<unsigned char> pre_segbbox_vec(pre->bboxes, pre->bboxes + pre->bboxesLength);
  std::vector<unsigned char> post_segbbox_vec(post->bboxes, post->bboxes + post->bboxesLength);
  
  std::vector<unsigned char> pre_segsize_vec(pre->sizes, pre->sizes + pre->sizesLength);
  std::vector<unsigned char> post_segsize_vec(post->sizes, post->sizes + post->sizesLength);

  // Volume
  std::unique_ptr<CVolumeMetadata> pre_meta(new CVolumeMetadata(pre_meta_vec, pre_segbbox_vec, pre_segsize_vec));
  std::unique_ptr<CVolumeMetadata> post_meta(new CVolumeMetadata(post_meta_vec, post_segbbox_vec, post_segsize_vec));

  // Segmentation Images
  std::vector<unsigned char> pre_segmentation_vec(pre->segmentation, pre->segmentation + pre->segmentationLength);
  std::vector<unsigned char> post_segmentation_vec(post->segmentation, post->segmentation + post->segmentationLength);

  CVolume pre_volume(std::move(pre_meta), pre_segmentation_vec);
  CVolume post_volume(std::move(post_meta), post_segmentation_vec);

  // Do Important Stuff
  std::vector<std::map<uint32_t, uint32_t>> seeds;
  get_seeds(seeds, pre_volume, selected, post_volume);

  return new CTaskSpawner(seeds);
}

extern "C" void TaskSpawner_Release(CTaskSpawner * taskspawner) {
  delete taskspawner;
  taskspawner = nullptr;
}
