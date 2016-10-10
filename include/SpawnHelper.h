#pragma once

#ifndef _SPAWN_HELPER_H_
#define _SPAWN_HELPER_H_

#include "Volume.h"

#include <zi/disjoint_sets/disjoint_sets.hpp>
#include <zi/timer.hpp>

#include <vmmlib/vmmlib.hpp>

#include <unordered_map>
#include <map>
#include <set>
#include <cassert>

/*****************************************************************/

// Some additions to vmmlib...
namespace vmml {
  template<typename T>
  AABB<T> intersect(const AABB<T> &pre, const AABB<T> &post) {
    AABB<T> intersection(pre);

    if ( post.getMin().x() > pre.getMin().x() )
      intersection.getMin().x() = post.getMin().x();
    if ( post.getMin().y() > pre.getMin().y() )
      intersection.getMin().y() = post.getMin().y();
    if ( post.getMin().z() > pre.getMin().z() )
      intersection.getMin().z() = post.getMin().z();

    if ( post.getMax().x() < pre.getMax().x() )
      intersection.getMax().x() = post.getMax().x();
    if ( post.getMax().y() < pre.getMax().y() )
      intersection.getMax().y() = post.getMax().y();
    if ( post.getMax().z() < pre.getMax().z() )
      intersection.getMax().z() = post.getMax().z();

    return intersection;
  }

  template<typename T>
  AABB<T> subtractVector(const AABB<T> &aabb, const Vector<3, T> &vec) {
    AABB<T> res(aabb);
    res.getMin() -= vec;
    res.getMax() -= vec;
    return res;
  }

  template<typename T>
  AABB<T> addVector(const AABB<T> &aabb, const Vector<3, T> &vec) {
    AABB<T> res(aabb);
    res.getMin() += vec;
    res.getMax() += vec;
    return res;
  }

  template<typename T>
  AABB<T> divideVector(const AABB<T> &aabb, const Vector<3, T> &vec) {
    AABB<T> res(aabb);
    res.getMin() /= vec;
    res.getMax() /= vec;
    return res;
  }

  template<typename T>
  AABB<T> multiplyVector(const AABB<T> &aabb, const Vector<3, T> &vec) {
    AABB<T> res(aabb);
    res.getMin() *= vec;
    res.getMax() *= vec;
    return res;
  }

  template<typename T>
  AABB<T> dilate(const AABB<T> &aabb, const Vector<3, T> &vec) {
    AABB<T> res(aabb);
    
    res.getMin() -= vec;
    res.getMax() += vec;
    return res;
  }

  //TODO: Better way to prevent underflow
  template<>
  AABB<int64_t> dilate(const AABB<int64_t> &aabb, const Vector<3, int64_t> &vec) {
    AABB<int64_t> res(aabb);

    Vector<3, int64_t> tmp = res.getMin() - vec;
    res.getMin().x() = tmp.x() > res.getMin().x() ? 0 : tmp.x();
    res.getMin().y() = tmp.y() > res.getMin().y() ? 0 : tmp.y();
    res.getMin().z() = tmp.z() > res.getMin().z() ? 0 : tmp.z();

    res.getMax() += vec;
    return res;
  }
}

/*****************************************************************/

// Direction from Pre to Post
enum class Direction {
  XMin,
  XMax,
  YMin,
  YMax,
  ZMin,
  ZMax,
};

/*****************************************************************/

Direction getDirection(const vmml::AABB<int64_t>& pre, const vmml::AABB<int64_t>& post) {
  vmml::AABB<int64_t> bounds = intersect(pre, post);
  assert(!bounds.isEmpty());

  auto dims = bounds.getDimension();

  if (dims.x() < dims.y() && dims.x() < dims.z()) {
    if (bounds.getMin().x() > post.getMin().x()) {
      return Direction::XMin;
    } else {
      return Direction::XMax;
    }
  }

  if (dims.y() < dims.x() && dims.y() < dims.z()) {
    if (bounds.getMin().y() > post.getMin().y()) {
      return Direction::YMin;
    } else {
      return Direction::YMax;
    }
  }

  if (bounds.getMin().z() > post.getMin().z()) {
    return Direction::ZMin;
  } else {
    return Direction::ZMax;
  }
}

/*****************************************************************/

vmml::AABB<int64_t> getOverlapRegion(const vmml::AABB<int64_t> &pre, const vmml::AABB<int64_t> &post, const Direction d, const vmml::Vector<3, int64_t> &margin) {
  vmml::AABB<int64_t> bounds = intersect(pre, post);

  // Trim on preside
  auto min = bounds.getMin();
  auto max = bounds.getMax();

  switch (d) {
    case Direction::XMin:
      bounds.setMax(vmml::Vector<3, int64_t>(max.x() - margin.x(), max.y(), max.z()));
      break;
    case Direction::YMin:
      bounds.setMax(vmml::Vector<3, int64_t>(max.x(), max.y() - margin.y(), max.z()));
      break;
    case Direction::ZMin:
      bounds.setMax(vmml::Vector<3, int64_t>(max.x(), max.y(), max.z() - margin.z()));
      break;
    case Direction::XMax:
      bounds.setMin(vmml::Vector<3, int64_t>(min.x() + margin.x(), min.y(), min.z()));
      break;
    case Direction::YMax:
      bounds.setMin(vmml::Vector<3, int64_t>(min.x(), min.y() + margin.y(), min.z()));
      break;
    case Direction::ZMax:
      bounds.setMin(vmml::Vector<3, int64_t>(min.x(), min.y(), min.z() + margin.z()));
      break;
  }

  return bounds;
}

/*****************************************************************/

inline bool inCriticalRegion(const vmml::Vector<3, int64_t> &pos, const vmml::AABB<int64_t> &bounds, const Direction d, const vmml::Vector<3, int64_t> &margin) {
  return ((d == Direction::XMin && pos.x() < bounds.getMin().x() + margin.x()) ||
          (d == Direction::YMin && pos.y() < bounds.getMin().y() + margin.y()) ||
          (d == Direction::ZMin && pos.z() < bounds.getMin().z() + margin.z()) ||
          (d == Direction::XMax && pos.x() > bounds.getMax().x() - margin.x()) ||
          (d == Direction::YMax && pos.y() > bounds.getMax().y() - margin.y()) ||
          (d == Direction::ZMax && pos.z() > bounds.getMax().z() - margin.z()));
}

/*****************************************************************/

std::map<uint32_t, uint32_t> makeSeed(const std::set<uint32_t>& bundle, const std::unordered_map<uint32_t, int> & mappingCounts, const std::unordered_map<uint32_t, int> & sizes) {
  const double FALSE_OBJ_RATIO_THR = 0.5;
  std::map<uint32_t, uint32_t> ret;
  uint32_t largest = 0;
  uint32_t largestSize = 0;

  for (auto& seg : bundle) {
    if (seg == 0) {
      continue;
    }
    if (((const double)mappingCounts.at(seg)) / ((const double)sizes.at(seg)) >= FALSE_OBJ_RATIO_THR) {
      ret[seg] = sizes.at(seg);
    }
    if (sizes.at(seg) > largestSize) {
      largest = seg;
      largestSize = sizes.at(seg);
    }
  }

  if (ret.size() == 0) {
    ret[largest] = largestSize;
  }

  return ret;
}

/*****************************************************************/

void get_seeds(std::vector<std::map<uint32_t, uint32_t>> &seeds, const CVolume &pre, const std::set<uint32_t> &selected, const CVolume &post) {
  //TODO: log "Getting Seeds\n" << pre.Endpoint() << '\n' << post.Endpoint();
  seeds.clear();
  zi::wall_timer t;
  t.reset();

  vmml::Vector<3, int64_t> res = pre.GetVoxelResolution();
  vmml::AABB<int64_t> prePhysicalBounds = pre.GetPhysicalBounds();
  vmml::AABB<int64_t> postPhysicalBounds = post.GetPhysicalBounds();

  Direction dir = getDirection(prePhysicalBounds, postPhysicalBounds);

  vmml::AABB<int64_t> preBoundsWorld = vmml::divideVector(prePhysicalBounds, res);
  vmml::AABB<int64_t> postBoundsWorld = vmml::divideVector(postPhysicalBounds, res);

  vmml::AABB<int64_t> overlapWorld = getOverlapRegion(preBoundsWorld, postBoundsWorld, dir, vmml::Vector<3, int64_t>(5, 5, 5));
  if (overlapWorld.isEmpty()) {
    //TODO: Boxes do not overlap. Log and throw some error.
  }

  vmml::AABB<int64_t> segmentBoundsWorld;

  for (auto& segID : selected) {
    if (segID < pre.GetSegmentCount() && segID > 0) {
      segmentBoundsWorld.merge(vmml::divideVector(pre.GetSegmentBoundsWorld(segID), res));
    }
  }

  vmml::AABB<int64_t> roiWorld = intersect(overlapWorld, segmentBoundsWorld);
  if (roiWorld.isEmpty()) {
    //TODO: No segments in non-fudge area. Log some info.
    return;
  }


  vmml::AABB<int64_t> preVolumeROI = vmml::subtractVector(roiWorld, preBoundsWorld.getMin());
  vmml::AABB<int64_t> postVolumeROI = vmml::subtractVector(roiWorld, postBoundsWorld.getMin());

  vmml::Vector<3, int64_t> dimROI = roiWorld.getDimension();
  int64_t volumeROI = dimROI.x() * dimROI.y() * dimROI.z();

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


  const vmml::Vector<3, int64_t> EXPANSION(50,50,50);
  vmml::AABB<int64_t> dilatedPostVolumeROI = vmml::dilate(postVolumeROI, EXPANSION);
  dilatedPostVolumeROI = vmml::intersect(dilatedPostVolumeROI, vmml::subtractVector(overlapWorld, postBoundsWorld.getMin()));

  vmml::Vector<3, int64_t> dilatedPostROIPos(dilatedPostVolumeROI.getMin());
  vmml::Vector<3, int64_t> dilatedPostROIDim(dilatedPostVolumeROI.getDimension());

  for (int64_t z = 0; z < dilatedPostROIDim.z(); ++z, ++dilatedPostROIPos.z()) {
    dilatedPostROIPos.y() = dilatedPostVolumeROI.getMin().y();
    for (int64_t y = 0; y < dilatedPostROIDim.y(); ++y, ++dilatedPostROIPos.y()) {
      dilatedPostROIPos.x() = dilatedPostVolumeROI.getMin().x();
      for (int64_t x = 0; x < dilatedPostROIDim.x(); ++x, ++dilatedPostROIPos.x()) {
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
    const vmml::Vector<3, int64_t> pos(i % dimROI.x(), (i / dimROI.x()) % dimROI.y(), i / (dimROI.x() * dimROI.y()));
    const uint32_t root = sets.find_set(i);
    newSeedSets[root].insert(postSegmentation(pos + postVolumeROI.getMin()));
    preSideSets[root].insert(preSegmentation(pos + preVolumeROI.getMin()));
    if (!escapes[root] && inCriticalRegion(pos + roiWorld.getMin(), preBoundsWorld, dir, vmml::Vector<3, int64_t>(5, 5, 5))) {
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

/*****************************************************************/

#endif
