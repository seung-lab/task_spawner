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
    size_t                          count;
    std::vector<size_t>             sizes;
    std::vector<vmml::AABB<size_t>> boundsVolume;
    std::vector<vmml::AABB<size_t>> boundsWorld;

    CSegments(const CVolumeMetadata &meta, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes);
  };

  vmml::AABB<size_t>         physical_offset;
  vmml::Vector<3, size_t>    volume_dimensions;
  vmml::Vector<3, size_t>    voxel_resolution;
  std::string                resolution_units;
  MetaDataType               segment_id_type;
  MetaDataType               segment_bbox_type;
  MetaDataType               segment_size_type;
  uint8_t                    segment_id_type_size;
  size_t                     segment_count;
  CSegments                * segments;

public:

  CVolumeMetadata(const std::vector<unsigned char> &raw_json, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes);
  ~CVolumeMetadata();

  const vmml::AABB<size_t> &      GetPhysicalBounds() const;
  const vmml::Vector<3, size_t> & GetVolumeDimensions() const;
  const vmml::Vector<3, size_t> & GetVoxelResolution() const;
  const std::string &             GetResolutionUnit() const;
  uint8_t                         GetSegmentTypeSize() const;
  size_t                          GetSegmentCount() const;

};

/*****************************************************************/

class CSegmentation {
protected:
  const vmml::Vector<3, size_t> dimensions_;

  CSegmentation(const vmml::Vector<3, size_t> & dimensions);

public:
  virtual uint32_t operator()(size_t x, size_t y, size_t z) const;
  virtual uint32_t operator()(const vmml::Vector<3, size_t> & pos) const;

};

/*****************************************************************/

class CSegmentationUChar : public CSegmentation {
private:
  const uint8_t * segmentation_;
public:
  CSegmentationUChar(const vmml::Vector<3, size_t> &dimensions, const uint8_t * segmentation);
  uint32_t operator()(size_t x, size_t y, size_t z) const override;
  uint32_t operator()(const vmml::Vector<3, size_t> & pos) const override;
};

/*****************************************************************/

class CSegmentationUShort : public CSegmentation {
private:
  const uint16_t * segmentation_;
public:
  CSegmentationUShort(const vmml::Vector<3, size_t> &dimensions, const uint16_t * segmentation);
  uint32_t operator()(size_t x, size_t y, size_t z) const override;
  uint32_t operator()(const vmml::Vector<3, size_t> & pos) const override;
};

/*****************************************************************/

class CSegmentationUInt : public CSegmentation {
private:
  const uint32_t * segmentation_;
public:
  CSegmentationUInt(const vmml::Vector<3, size_t> &dimensions, const uint32_t * segmentation);
  uint32_t operator()(size_t x, size_t y, size_t z) const override;
  uint32_t operator()(const vmml::Vector<3, size_t> & pos) const override;
};

/*****************************************************************/

class CVolume {
  private:

  static vmml::AABB<size_t>        empty_bbox;

  std::unique_ptr<CVolumeMetadata> meta_;

  CSegmentation                  * segmentation_;


  public:
  CVolume(std::unique_ptr<CVolumeMetadata> &&meta, const std::vector<unsigned char> &raw_segmentation);
  ~CVolume();

  const vmml::AABB<size_t> &      GetPhysicalBounds() const;
  const vmml::Vector<3, size_t> & GetVoxelResolution() const;
  size_t                          GetSegmentCount() const;
  const vmml::AABB<size_t> &      GetSegmentBoundsWorld(size_t segID) const;
  const vmml::AABB<size_t> &      GetSegmentBoundsVolume(size_t segID) const;
  size_t                          GetSegmentSizeVoxel(size_t segID) const;
  const CSegmentation *           GetSegmentation() const;


};

/*****************************************************************/
#endif
