#pragma once

#ifndef _SPAWN_HELPER_H_
#define _SPAWN_HELPER_H_

#include "Volume.h"
#include <vmmlib/vmmlib.hpp>
#include <cassert>

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
  AABB<size_t> dilate(const AABB<size_t> &aabb, const Vector<3, size_t> &vec) {
    AABB<size_t> res(aabb);

    Vector<3, size_t> tmp = res.getMin() - vec;
    res.getMin().x() = tmp.x() > res.getMin().x() ? 0 : tmp.x();
    res.getMin().y() = tmp.y() > res.getMin().y() ? 0 : tmp.y();
    res.getMin().z() = tmp.z() > res.getMin().z() ? 0 : tmp.z();

    res.getMax() += vec;
    return res;
  }
}



// Direction from Pre to Post
enum class Direction {
  XMin,
  XMax,
  YMin,
  YMax,
  ZMin,
  ZMax,
};

Direction getDirection(const vmml::AABB<size_t>& pre, const vmml::AABB<size_t>& post) {
  vmml::AABB<size_t> bounds = intersect(pre, post);
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

vmml::AABB<size_t> getOverlapRegion(const vmml::AABB<size_t> &pre, const vmml::AABB<size_t> &post, const Direction d, const vmml::Vector<3, size_t> &margin) {
  vmml::AABB<size_t> bounds = intersect(pre, post);

  // Trim on preside
  auto min = bounds.getMin();
  auto max = bounds.getMax();

  switch (d) {
    case Direction::XMin:
      bounds.setMax(vmml::Vector<3,size_t>(max.x() - margin.x(), max.y(), max.z()));
      break;
    case Direction::YMin:
      bounds.setMax(vmml::Vector<3,size_t>(max.x(), max.y() - margin.y(), max.z()));
      break;
    case Direction::ZMin:
      bounds.setMax(vmml::Vector<3,size_t>(max.x(), max.y(), max.z() - margin.z()));
      break;
    case Direction::XMax:
      bounds.setMin(vmml::Vector<3,size_t>(min.x() + margin.x(), min.y(), min.z()));
      break;
    case Direction::YMax:
      bounds.setMin(vmml::Vector<3,size_t>(min.x(), min.y() + margin.y(), min.z()));
      break;
    case Direction::ZMax:
      bounds.setMin(vmml::Vector<3,size_t>(min.x(), min.y(), min.z() + margin.z()));
      break;
  }

  return bounds;
}

inline bool inCriticalRegion(const vmml::Vector<3, size_t> &pos, const vmml::AABB<size_t> &bounds, const Direction d, const vmml::Vector<3, size_t> &margin) {
  return ((d == Direction::XMin && pos.x() < bounds.getMin().x() + margin.x()) ||
          (d == Direction::YMin && pos.y() < bounds.getMin().y() + margin.y()) ||
          (d == Direction::ZMin && pos.z() < bounds.getMin().z() + margin.z()) ||
          (d == Direction::XMax && pos.x() > bounds.getMax().x() - margin.x()) ||
          (d == Direction::YMax && pos.y() > bounds.getMax().y() - margin.y()) ||
          (d == Direction::ZMax && pos.z() > bounds.getMax().z() - margin.z()));
}

std::map<uint32_t, uint32_t> makeSeed(const std::set<uint32_t>& bundle, const std::unordered_map<uint32_t, int> & mappingCounts, const std::unordered_map<uint32_t, int> & sizes) {
  const double FALSE_OBJ_RATIO_THR = 0.999;
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





#endif
