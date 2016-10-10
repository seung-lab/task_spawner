#include <string>
#include <vector>
#include <map>
#include <memory>

#include "CurlObject.h"
#include "LZMADecode.h"
#include "Volume.h"
#include "SpawnHelper.h"


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


extern "C" CTaskSpawner * TaskSpawner_Spawn(char * pre_path, char * post_path, uint32_t segmentCount, uint32_t * segments) {
  std::set<uint32_t> selected(segments, segments + segmentCount);

  std::string path = std::string(pre_path);
  CCurlObject pre_seg_curl(path + "segmentation.lzma");
  CCurlObject pre_meta_curl(path + "metadata.json");
  CCurlObject pre_segbbox_curl(path + "segmentation.bbox");
  CCurlObject pre_segsize_curl(path + "segmentation.size");

  path = std::string(post_path);
  CCurlObject post_seg_curl(path + "segmentation.lzma");
  CCurlObject post_meta_curl(path + "metadata.json");
  CCurlObject post_segbbox_curl(path + "segmentation.bbox");
  CCurlObject post_segsize_curl(path + "segmentation.size");

  // Volume
  std::unique_ptr<CVolumeMetadata> pre_meta(new CVolumeMetadata(pre_meta_curl.getData(), pre_segbbox_curl.getData(), pre_segsize_curl.getData()));
  std::unique_ptr<CVolumeMetadata> post_meta(new CVolumeMetadata(post_meta_curl.getData(), post_segbbox_curl.getData(), post_segsize_curl.getData()));

  // Segmentation Images
  std::vector<unsigned char> pre_compressed = pre_seg_curl.getData();
  std::vector<unsigned char> post_compressed = post_seg_curl.getData();

  LZMADec pre_lzma(pre_compressed, pre_meta->GetVolumeDimensions()[0] * pre_meta->GetVolumeDimensions()[1] * pre_meta->GetVolumeDimensions()[2] * pre_meta->GetSegmentTypeSize());
  LZMADec post_lzma(post_compressed, post_meta->GetVolumeDimensions()[0] * post_meta->GetVolumeDimensions()[1] * post_meta->GetVolumeDimensions()[2] * post_meta->GetSegmentTypeSize());

  std::vector<unsigned char> pre_uncompressed = pre_lzma.getUncompressed();
  std::vector<unsigned char> post_uncompressed = post_lzma.getUncompressed();

  CVolume pre(std::move(pre_meta), pre_uncompressed);
  CVolume post(std::move(post_meta), post_uncompressed);

  // Do Important Stuff
  std::vector<std::map<uint32_t, uint32_t>> seeds;
  get_seeds(seeds, pre, selected, post);

  return new CTaskSpawner(seeds);
}

extern "C" void TaskSpawner_Release(CTaskSpawner * taskspawner) {
  delete taskspawner;
  taskspawner = nullptr;
}
