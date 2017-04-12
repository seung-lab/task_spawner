#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "SpawnHelper.h"
#include "Volume.h"

#include "../res/spawnset.pb.h"

using namespace ew;

struct CInputVolume {
  char * metadata;

  uint32_t bboxesLength;
  unsigned char * bboxes;
  
  uint32_t sizesLength;
  unsigned char * sizes;
  
  uint32_t segmentationLength;
  unsigned char * segmentation;
};

class CSpawnTableWrapper {
public:
  uint32_t spawntableLength;
  unsigned char * spawntableBuffer;

  CSpawnTableWrapper() : spawntableLength(0), spawntableBuffer(nullptr) { };
  ~CSpawnTableWrapper() {
    delete[] spawntableBuffer;
    spawntableBuffer = nullptr;
    spawntableLength = 0;
  }
};

void calcSpawnTable(spawner::SpawnTable &spawntable, const CVolume &pre, const CVolume &post) {
#pragma region SanityChecks
  vmml::Vector<3, int64_t> res = pre.GetVoxelResolution();
  vmml::AABB<int64_t> prePhysicalBounds = pre.GetPhysicalBounds();
  vmml::AABB<int64_t> postPhysicalBounds = post.GetPhysicalBounds();

  Direction dir = getDirection(prePhysicalBounds, postPhysicalBounds);

  vmml::AABB<int64_t> preBoundsWorld = vmml::divideVector(prePhysicalBounds, res);
  vmml::AABB<int64_t> postBoundsWorld = vmml::divideVector(postPhysicalBounds, res);

  int64_t overlap = getOverlap(preBoundsWorld, postBoundsWorld, dir);

  vmml::AABB<int64_t> postHalfOverlapWorld = getOverlapRegion(preBoundsWorld, postBoundsWorld, dir, overlap/2, 0);
  if (postHalfOverlapWorld.isEmpty()) {
    std::cout << "Boxes do not overlap.\n";
    return;
  }

  vmml::AABB<int64_t> roiWorld = getOverlapRegion(preBoundsWorld, postBoundsWorld, dir, 1, 1);
  vmml::AABB<int64_t> preVolumeROI = vmml::subtractVector(roiWorld, preBoundsWorld.getMin());
  vmml::AABB<int64_t> postVolumeROI = vmml::subtractVector(roiWorld, postBoundsWorld.getMin());

  vmml::Vector<3, int64_t> dimROI = roiWorld.getDimension();
  int64_t volumeROI = dimROI.x() * dimROI.y() * dimROI.z();

  std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> mappingCountsPrePost;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> mappingCountsPostPre;
  std::unordered_map<uint32_t, int> overlapSizePost;
  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> neighborsPre;
#pragma endregion Initialization and sanity checks

#pragma region PostSideMatches

  
  vmml::Vector<3, int64_t> preROIPos(preVolumeROI.getMin());
  vmml::Vector<3, int64_t> postROIPos(postVolumeROI.getMin());
  const CSegmentation & preSegmentation = *(pre.GetSegmentation());
  const CSegmentation & postSegmentation = *(post.GetSegmentation());
  for (int64_t z = 0; z < dimROI.z(); ++z, ++preROIPos.z(), ++postROIPos.z()) {
    preROIPos.y() = preVolumeROI.getMin().y();
    postROIPos.y() = postVolumeROI.getMin().y();
    for (int64_t y = 0; y < dimROI.y(); ++y, ++preROIPos.y(), ++postROIPos.y()) {
      preROIPos.x() = preVolumeROI.getMin().x();
      postROIPos.x() = postVolumeROI.getMin().x();
      for (int64_t x = 0; x < dimROI.x(); ++x, ++preROIPos.x(), ++postROIPos.x()) {
        uint32_t segID = preSegmentation(preROIPos);
        if (is_valid_segment(segID, pre)) {
          uint32_t postSegID = postSegmentation(postROIPos);
          if (postSegID > 0) {
            mappingCountsPrePost[segID][postSegID]++;
            mappingCountsPostPre[postSegID][segID]++;
            overlapSizePost[postSegID]++;
          }

          if (x > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x() - 1, preROIPos.y(), preROIPos.z());
            if (neighborSegID > 0 && neighborSegID != segID) {
              neighborsPre[segID].emplace(neighborSegID);
              neighborsPre[neighborSegID].emplace(segID);
            }
          }
          if (y > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x(), preROIPos.y() - 1, preROIPos.z());
            if (neighborSegID > 0 && neighborSegID != segID) {
              neighborsPre[segID].emplace(neighborSegID);
              neighborsPre[neighborSegID].emplace(segID);
            }
          }
          if (z > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x(), preROIPos.y(), preROIPos.z() - 1);
            if (neighborSegID > 0 && neighborSegID != segID) {
              neighborsPre[segID].emplace(neighborSegID);
              neighborsPre[neighborSegID].emplace(segID);
            }
          }
        }
      }
    }
  }
#pragma endregion Find post-side matches

  auto& spawnEntries = *spawntable.mutable_entries();

  for (auto preKey : mappingCountsPrePost) {
    // Post-side counterparts
    // Possible cases:
    //   A) 1:1 match, the pre-side segment has exactly 1 post-side segment, and vice versa
    //   B) 1:N match, the pre-side segment has multiple post-side segment matches that are fully enclosed (they refer to the same, single pre-side segment)
    //   C) M:1 match, the pre-side segment has exactly 1 post-side segment, but this segment is also partially covered by other pre-side segments
    //   D) N:M match, the pre-side segment has multiple post-side segment matches that are full or only partially enclosed (some refer to other pre-side segments)

    // preKey: pre-side segment, current key.
    // postSeg: post-side segment(s), all overlap with `preSeg` (partially or fully)
    // preSeg: pre-side segment(s), all overlap with `postSeg` (partially or fully), includes the original `preKey`

    spawner::SpawnEntry match;
    auto & postSegments = preKey.second;
    for (auto postSeg : postSegments) {
      uint32_t postSegmentID = postSeg.first;
      spawner::SpawnEntry_PostSegment* postMatch = match.add_postsidecounterparts();
      postMatch->set_id(postSegmentID);
      postMatch->set_overlapsize(overlapSizePost[postSegmentID]);

      for (auto preSeg : mappingCountsPostPre[postSegmentID]) {
        uint32_t preSegmentID = preSeg.first;
        spawner::SpawnEntry_PreSegment* preSupport = postMatch->add_presidesupports();
        preSupport->set_id(preSegmentID);
        preSupport->set_intersectionsize(mappingCountsPrePost[preSegmentID][postSegmentID]);
      }
    }

    // Pre-side neighbors
    for (auto neighbor : neighborsPre[preKey.first]) {
      spawner::SpawnEntry_PreSegment* preNeighbor = match.add_presideneighbors();
      preNeighbor->set_id(neighbor);
    }

    // Can spawn
    auto segBoundsWorld = vmml::divideVector(pre.GetSegmentBoundsWorld(preKey.first), res);
    bool canSpawn = (intersect(segBoundsWorld, postHalfOverlapWorld).isEmpty() == false) && (is_valid_segment(preKey.first, pre));
    match.set_canspawn(canSpawn);

    spawnEntries[preKey.first] = std::move(match);
  }

}

extern "C" CSpawnTableWrapper * SpawnSet_Generate(CInputVolume * pre, CInputVolume * post) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

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

  spawner::SpawnTable spawntable;
  calcSpawnTable(spawntable, pre_volume, post_volume);

  CSpawnTableWrapper * spawntableWrapper = new CSpawnTableWrapper();
  size_t size = spawntable.ByteSizeLong();
  unsigned char * spawntableBuffer = new unsigned char[size];
  spawntable.SerializeToArray((void*)spawntableBuffer, size);

  spawntableWrapper->spawntableLength = uint32_t(size);
  spawntableWrapper->spawntableBuffer = spawntableBuffer;

  google::protobuf::ShutdownProtobufLibrary();

  return spawntableWrapper;
}

extern "C" void SpawnSet_Release(CSpawnTableWrapper * spawntableWrapper) {
  delete spawntableWrapper;
  spawntableWrapper = nullptr;
}
