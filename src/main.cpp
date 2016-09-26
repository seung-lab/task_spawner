//#include "Spawn.h"



#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <zi/disjoint_sets/disjoint_sets.hpp>

#include "CurlObject.h"
#include "LZMADecode.h"
#include "Volume.h"
#include "SpawnHelper.h"

#include <zi/timer.hpp>
/*****************************************************************/

const char PRE_PATH[] = "https://storage.googleapis.com/pinky_3x3x2_2/pinky/3x3x2_2/hypersquare/chunk_17107-18130_23251-24274_4003-4130/";
const char POST_PATH[] = "https://storage.googleapis.com/pinky_3x3x2_2/pinky/3x3x2_2/hypersquare/chunk_17107-18130_23251-24274_4115-4242/";

const int16_t PRE_SEGMENTS[] = { 318,324,348,396,406,448,452,453,520,534,623,625,698,715,786,787,788,789,793,885,887,971,977,978,980,982,1724,2303,2304,2365,2535,2542,2624,2728,2834,2925,2989,3001,3021,3071,3072,3074,3109,3160,3171,3256,3278,3421,3434,3511,3536,3685,3714,3715,3718,3760,3767,3769,3770,3807,3843,3878,3890,3891,3964,4045,4090,4199,4284,4357,4358,4446,4541,4603,4716,4772,4773,4968,5033,5213,5216,5217,5270,5313,5361,5417,5534,5645,5653,5766,6050,6938,7272,8269,8373,8461,8462,8663,8664,8667,8876,9084,9093,9266,9267,9395,9403,9495,9497,9537,9539,9545,9749,9751,9761,9791,9859,9885,9899,9900,9905,9972,10016,10018,10094,10099,10100,10246,10253,10295,10296,10384,10388,10389,10467,10473,10503,10531,10581 };


/*****************************************************************/
void get_seeds(std::vector<std::map<uint32_t, uint32_t>> &seeds, const CVolume &pre, const std::set<uint32_t> &selected, const CVolume &post);
/*****************************************************************/

int main(int argc, char* argv[]) {

  std::set<uint32_t> selected(std::begin(PRE_SEGMENTS), std::end(PRE_SEGMENTS));

  std::string path = std::string(PRE_PATH);
  CCurlObject pre_seg_curl(path + "segmentation.lzma");
  CCurlObject pre_meta_curl(path + "metadata.json");
  CCurlObject pre_segbbox_curl(path + "segmentation.bbox");
  CCurlObject pre_segsize_curl(path + "segmentation.size");

  path = std::string(POST_PATH);
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

  std::cout << "Press enter to continue ...";
  std::cin.get();

  return 0;
}

/*****************************************************************/

void get_seeds(std::vector<std::map<uint32_t, uint32_t>> &seeds, const CVolume &pre, const std::set<uint32_t> &selected, const CVolume &post) {
  //TODO: log "Getting Seeds\n" << pre.Endpoint() << '\n' << post.Endpoint();
  seeds.clear();
  zi::wall_timer t;
  t.reset();

  vmml::Vector<3, size_t> res = pre.GetVoxelResolution();
  vmml::AABB<size_t> preBoundsWorld = vmml::divideVector(pre.GetPhysicalBounds(), res);
  vmml::AABB<size_t> postBoundsWorld = vmml::divideVector(post.GetPhysicalBounds(), res);
  Direction dir = getDirection(preBoundsWorld, postBoundsWorld);

  vmml::AABB<size_t> overlapWorld = getOverlapRegion(preBoundsWorld, postBoundsWorld, dir, vmml::Vector<3, size_t>(5, 5, 5));
  if (overlapWorld.isEmpty()) {
    //TODO: Boxes do not overlap. Log and throw some error.
  }

  vmml::AABB<size_t> segmentBoundsWorld;

  for (auto& segID : selected) {
    if (segID < pre.GetSegmentCount() && segID > 0) {
      segmentBoundsWorld.merge(vmml::divideVector(pre.GetSegmentBoundsWorld(segID), res));
    }
  }

  vmml::AABB<size_t> roiWorld = intersect(overlapWorld, segmentBoundsWorld);
  if (roiWorld.isEmpty()) {
    //TODO: No segments in non-fudge area. Log some info.
    return;
  }


  vmml::AABB<size_t> preVolumeROI = vmml::subtractVector(roiWorld, preBoundsWorld.getMin());
  vmml::AABB<size_t> postVolumeROI = vmml::subtractVector(roiWorld, postBoundsWorld.getMin());

  vmml::Vector<3, size_t> dimROI = roiWorld.getDimension();
  size_t volumeROI = dimROI.x() * dimROI.y() * dimROI.z();

  if (volumeROI > 400 * 128 * 128 * 128) {
    // Copied from old code... where does this restriction come from?

    // TODO: Overlap region is too large. Log and throw some error.
  }

  std::unordered_map<uint32_t, int> mappingCounts;
  std::unordered_map<uint32_t, int> sizes;
  zi::disjoint_sets<uint32_t> sets(volumeROI);
  std::set<uint32_t> included, postSelected;

  std::cout << "Initialization and sanity checks: " << t.elapsed<double>() << " s\n";
  t.reset();


  // TODO: Write an iterator for iterating through the given bounding box and skipping irrelevant segment IDs
  vmml::Vector<3, size_t> preROIPos(preVolumeROI.getMin());
  vmml::Vector<3, size_t> postROIPos(postVolumeROI.getMin());
  const CSegmentation & preSegmentation = *(pre.GetSegmentation());
  const CSegmentation & postSegmentation = *(post.GetSegmentation());
  for (size_t z = 0; z < dimROI.z(); ++z, ++preROIPos.z(), ++postROIPos.z()) {
    preROIPos.y() = preVolumeROI.getMin().y();
    postROIPos.y() = postVolumeROI.getMin().y();
    for (size_t y = 0; y < dimROI.y(); ++y, ++preROIPos.y(), ++postROIPos.y()) {
      preROIPos.x() = preVolumeROI.getMin().x();
      postROIPos.x() = postVolumeROI.getMin().x();
      for (size_t x = 0; x < dimROI.x(); ++x, ++preROIPos.x(), ++postROIPos.x()) {
        uint32_t segID = preSegmentation(preROIPos);
        //std::cout << segID << "\n";
        if (segID > 0 && selected.find(segID) != selected.end()) {
          uint32_t postSegID = postSegmentation(postROIPos);
          if (postSegID > 0) {
            postSelected.insert(postSegID);
            mappingCounts[postSegID]++;
          }

          uint32_t proxy = x + y * dimROI.x() + z * dimROI.x() * dimROI.y();
          included.insert(proxy);

          // TODO: Store last (x-1) segID for possible speedup?
          // TODO: Skip after first join?
          if (x > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x() - 1, preROIPos.y(), preROIPos.z());
            if (neighborSegID > 0 && selected.find(neighborSegID) != selected.end()) {
              uint32_t neighborProxy = (x - 1) + y * dimROI.x() + z * dimROI.x() * dimROI.y();
              sets.join(sets.find_set(proxy), sets.find_set(neighborProxy));
            }
          }
          if (y > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x(), preROIPos.y() - 1, preROIPos.z());
            if (neighborSegID > 0 && selected.find(neighborSegID) != selected.end()) {
              uint32_t neighborProxy = x + (y - 1) * dimROI.x() + z * dimROI.x() * dimROI.y();
              sets.join(sets.find_set(proxy), sets.find_set(neighborProxy));
            }
          }
          if (z > 0) {
            uint32_t neighborSegID = preSegmentation(preROIPos.x(), preROIPos.y(), preROIPos.z() - 1);
            if (neighborSegID > 0 && selected.find(neighborSegID) != selected.end()) {
              uint32_t neighborProxy = x + y * dimROI.x() + (z - 1) * dimROI.x() * dimROI.y();
              sets.join(sets.find_set(proxy), sets.find_set(neighborProxy));
            }
          } 
        } // preSegmentation(preROIPos) > 0 && selected.find(preSegmentation(preROIPos)) != selected.end()
      }
    }
  }

  std::cout << "Connected Components + Finding Post Matches: " << t.elapsed<double>() << " s\n";
  t.reset();


  const vmml::Vector<3, size_t> EXPANSION(50,50,50);
  vmml::AABB<size_t> dilatedPostVolumeROI = vmml::dilate(postVolumeROI, EXPANSION);
  dilatedPostVolumeROI = vmml::intersect(dilatedPostVolumeROI, vmml::subtractVector(overlapWorld, postBoundsWorld.getMin()));

  vmml::Vector<3, size_t> dilatedPostROIPos(dilatedPostVolumeROI.getMin());
  vmml::Vector<3, size_t> dilatedPostROIDim(dilatedPostVolumeROI.getDimension());

  for (size_t z = 0; z < dilatedPostROIDim.z(); ++z, ++dilatedPostROIPos.z()) {
    dilatedPostROIPos.y() = dilatedPostVolumeROI.getMin().y();
    for (size_t y = 0; y < dilatedPostROIDim.y(); ++y, ++dilatedPostROIPos.y()) {
      dilatedPostROIPos.x() = dilatedPostVolumeROI.getMin().x();
      for (size_t x = 0; x < dilatedPostROIDim.x(); ++x, ++dilatedPostROIPos.x()) {
        uint32_t segID = postSegmentation(dilatedPostROIPos);

        if (segID > 0 && postSelected.find(segID) != postSelected.end()) {
          sizes[segID]++;
        }
      }
    }
  }

  std::cout << "Accumulate Post Segment Sizes: " << t.elapsed<double>() << " s\n";
  t.reset();

  std::unordered_map<uint32_t, std::set<uint32_t>> newSeedSets;
  std::unordered_map<uint32_t, std::set<uint32_t>> preSideSets;
  std::unordered_map<uint32_t, bool> escapes;
  for (auto& i : included) {
    escapes[sets.find_set(i)] = false;
  }
  for (auto& i : included) {
    const vmml::Vector<3, size_t> pos(i % dimROI.x(), (i / dimROI.x()) % dimROI.y(), i / (dimROI.x() * dimROI.y()));
    const uint32_t root = sets.find_set(i);
    newSeedSets[root].insert(postSegmentation(pos + postVolumeROI.getMin()));
    preSideSets[root].insert(preSegmentation(pos + preVolumeROI.getMin()));
    if (!escapes[root] && inCriticalRegion(pos + roiWorld.getMin(), preBoundsWorld, dir, vmml::Vector<3, size_t>(5, 5, 5))) {
      escapes[root] = true;
    }
  }

  std::cout << "Agglomeration of seed sets: " << t.elapsed<double>() << " s\n";
  t.reset();

  for (auto& seed : newSeedSets) {
    if (escapes[seed.first]) {
      seeds.push_back(makeSeed(seed.second, mappingCounts, sizes));
      std::cout << "\nSpawned: \n";
      std::cout << "  Pre Side: ";
      for (auto& seg : preSideSets[seed.first]) {
        std::cout << seg << ", ";
      }
      std::cout << "\n";

      std::stringstream ss;
      for (auto& seg : seeds.back()) {
        ss << seg.first << ", ";
      }
      std::cout << "  Post Side: " << ss.str() << "\n";
    } else {
      std::cout << "\nDoes not escape: ";
      for (auto& seg : preSideSets[seed.first]) {
        std::cout << seg << ", ";
      }
      std::cout << "\n";
    }
  }

  std::cout << "Finalization: " << t.elapsed<double>() << " s\n";
  t.reset();
}