#pragma once

#ifndef _VOLUME_H_
#define _VOLUME_H_

#include <string>
#include <vector>
#include <memory>
#include <vmmlib/vmmlib.hpp>

/*****************************************************************/

enum class MetaDataType {
  Unknown,
  UInt8,
  UInt16,
  UInt32,
  Float32,
  Float64
};

/*****************************************************************/

MetaDataType StringToMetaDataType(const std::string & str, uint8_t * sizeInByte = NULL);

/*****************************************************************/
class CVolume; // forward declaration


class CVolumeMetadata {
  friend class CVolume;

private:

  struct CSegments {
    int64_t                          count;
    std::vector<int64_t>             sizes;
    std::vector<vmml::AABB<int64_t>> boundsVolume;
    std::vector<vmml::AABB<int64_t>> boundsWorld;

    CSegments(const CVolumeMetadata &meta, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes);
  };

  vmml::AABB<int64_t>        physical_offset;
  vmml::Vector<3, int64_t>   volume_dimensions;
  vmml::Vector<3, int64_t>   voxel_resolution;
  std::string                resolution_units;
  MetaDataType               segment_id_type;
  MetaDataType               segment_bbox_type;
  MetaDataType               segment_size_type;
  uint8_t                    segment_id_type_size;
  int64_t                    segment_count;
  CSegments                * segments;

public:

  CVolumeMetadata(const std::vector<unsigned char> &raw_json, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes);
  ~CVolumeMetadata();

  const vmml::AABB<int64_t> &      GetPhysicalBounds() const;
  const vmml::Vector<3, int64_t> & GetVolumeDimensions() const;
  const vmml::Vector<3, int64_t> & GetVoxelResolution() const;
  const std::string &              GetResolutionUnit() const;
  uint8_t                          GetSegmentTypeSize() const;
  int64_t                          GetSegmentCount() const;

};

/*****************************************************************/

class CSegmentation {
protected:
  const vmml::Vector<3, int64_t> dimensions_;

  CSegmentation(const vmml::Vector<3, int64_t> & dimensions);

public:
  virtual uint32_t operator()(int64_t x, int64_t y, int64_t z) const;
  virtual uint32_t operator()(const vmml::Vector<3, int64_t> & pos) const;

};

/*****************************************************************/

class CSegmentationUChar : public CSegmentation {
private:
  const uint8_t * segmentation_;
public:
  CSegmentationUChar(const vmml::Vector<3, int64_t> &dimensions, const uint8_t * segmentation);
  uint32_t operator()(int64_t x, int64_t y, int64_t z) const override;
  uint32_t operator()(const vmml::Vector<3, int64_t> & pos) const override;
};

/*****************************************************************/

class CSegmentationUShort : public CSegmentation {
private:
  const uint16_t * segmentation_;
public:
  CSegmentationUShort(const vmml::Vector<3, int64_t> &dimensions, const uint16_t * segmentation);
  uint32_t operator()(int64_t x, int64_t y, int64_t z) const override;
  uint32_t operator()(const vmml::Vector<3, int64_t> & pos) const override;
};

/*****************************************************************/

class CSegmentationUInt : public CSegmentation {
private:
  const uint32_t * segmentation_;
public:
  CSegmentationUInt(const vmml::Vector<3, int64_t> &dimensions, const uint32_t * segmentation);
  uint32_t operator()(int64_t x, int64_t y, int64_t z) const override;
  uint32_t operator()(const vmml::Vector<3, int64_t> & pos) const override;
};

/*****************************************************************/

class CVolume {
  private:

  static vmml::AABB<int64_t>       empty_bbox;

  std::unique_ptr<CVolumeMetadata> meta_;

  CSegmentation                  * segmentation_;


  public:
  CVolume(std::unique_ptr<CVolumeMetadata> &&meta, const std::vector<unsigned char> &raw_segmentation);
  ~CVolume();

  const vmml::AABB<int64_t> &      GetPhysicalBounds() const;
  const vmml::Vector<3, int64_t> & GetVoxelResolution() const;
  int64_t                          GetSegmentCount() const;
  const vmml::AABB<int64_t> &      GetSegmentBoundsWorld(int64_t segID) const;
  const vmml::AABB<int64_t> &      GetSegmentBoundsVolume(int64_t segID) const;
  int64_t                          GetSegmentSizeVoxel(int64_t segID) const;
  const CSegmentation *            GetSegmentation() const;


};

/*****************************************************************/
#endif
