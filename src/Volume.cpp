#include "Volume.h"
#include "json.hpp"

using json = nlohmann::json;

/*****************************************************************/

MetaDataType StringToMetaDataType(const std::string & str, uint8_t * sizeInByte) {
  if (str == "UInt8") {
    if (sizeInByte) *sizeInByte = 1;
    return MetaDataType::UInt8;
  }
  else if (str == "UInt16") {
    if (sizeInByte) *sizeInByte = 2;
    return MetaDataType::UInt16;
  }
  else if (str == "UInt32") {
    if (sizeInByte) *sizeInByte = 4;
    return MetaDataType::UInt32;
  }
  else {
    throw(std::string("Unsupported type '" + str + "'. Must be UInt8, UInt16, UInt32, Float32 or Float64."));
  }
}

/*****************************************************************/

vmml::AABB<int64_t> CVolume::empty_bbox = vmml::AABB<int64_t>();

/*****************************************************************/

CSegmentation::CSegmentation(const vmml::Vector<3, int64_t> &dimensions) : dimensions_(dimensions)
{
}

uint32_t CSegmentation::operator()(int64_t x, int64_t y, int64_t z) const {
  return 0;
}

uint32_t CSegmentation::operator()(const vmml::Vector<3, int64_t> & pos) const {
  return 0;
}

/*****************************************************************/

CSegmentationUChar::CSegmentationUChar(const vmml::Vector<3, int64_t> &dimensions, const uint8_t * segmentation) :
  CSegmentation(dimensions),
  segmentation_(segmentation)
{
}

uint32_t CSegmentationUChar::operator()(int64_t x, int64_t y, int64_t z) const {
  return segmentation_[x + y * dimensions_.x() + z * dimensions_.x() * dimensions_.y()];
}

uint32_t CSegmentationUChar::operator()(const vmml::Vector<3, int64_t> & pos) const {
  return segmentation_[pos.x() + pos.y() * dimensions_.x() + pos.z() * dimensions_.x() * dimensions_.y()];
}

/*****************************************************************/

CSegmentationUShort::CSegmentationUShort(const vmml::Vector<3, int64_t> &dimensions, const uint16_t * segmentation) :
  CSegmentation(dimensions),
  segmentation_(segmentation)
{
}

uint32_t CSegmentationUShort::operator()(int64_t x, int64_t y, int64_t z) const {
  return segmentation_[x + y * dimensions_.x() + z * dimensions_.x() * dimensions_.y()];
}

uint32_t CSegmentationUShort::operator()(const vmml::Vector<3, int64_t> & pos) const {
  return segmentation_[pos.x() + pos.y() * dimensions_.x() + pos.z() * dimensions_.x() * dimensions_.y()];
}

/*****************************************************************/

CSegmentationUInt::CSegmentationUInt(const vmml::Vector<3, int64_t> &dimensions, const uint32_t * segmentation) :
  CSegmentation(dimensions),
  segmentation_(segmentation)
{
}

uint32_t CSegmentationUInt::operator()(int64_t x, int64_t y, int64_t z) const {
  return segmentation_[x + y * dimensions_.x() + z * dimensions_.x() * dimensions_.y()];
}

uint32_t CSegmentationUInt::operator()(const vmml::Vector<3, int64_t> & pos) const {
  return segmentation_[pos.x() + pos.y() * dimensions_.x() + pos.z() * dimensions_.x() * dimensions_.y()];
}

/*****************************************************************/

CVolume::CVolume(std::unique_ptr<CVolumeMetadata> &&meta, const std::vector<unsigned char> &raw_segmentation) :
    meta_(std::move(meta))
{
  switch (meta_->segment_id_type) {
  case MetaDataType::UInt8:
    segmentation_ = new CSegmentationUChar(meta_->volume_dimensions, reinterpret_cast<const uint8_t *>(raw_segmentation.data()));
    break;
  case MetaDataType::UInt16:
    segmentation_ = new CSegmentationUShort(meta_->volume_dimensions, reinterpret_cast<const uint16_t *>(raw_segmentation.data()));
    break;
  case MetaDataType::UInt32:
    segmentation_ = new CSegmentationUInt(meta_->volume_dimensions, reinterpret_cast<const uint32_t *>(raw_segmentation.data()));
    break;
  }

}

/*****************************************************************/

CVolume::~CVolume() {
    meta_.reset();
    delete segmentation_;
}

/*****************************************************************/

const vmml::AABB<int64_t> & CVolume::GetPhysicalBounds() const {
    return meta_->GetPhysicalBounds();
}

/*****************************************************************/

const vmml::Vector<3, int64_t> & CVolume::GetVoxelResolution() const {
    return meta_->GetVoxelResolution();
}

/*****************************************************************/

int64_t CVolume::GetSegmentCount() const {
    return meta_->GetSegmentCount();
}

/*****************************************************************/

const vmml::AABB<int64_t> & CVolume::GetSegmentBoundsVolume(int64_t segID) const {
    if (segID < GetSegmentCount())
        return meta_->segments->boundsVolume[segID];
    else
        return empty_bbox;
}

/*****************************************************************/

const vmml::AABB<int64_t> & CVolume::GetSegmentBoundsWorld(int64_t segID) const {
    if (segID < GetSegmentCount())
        return meta_->segments->boundsWorld[segID];
    else
        return empty_bbox;
}

/*****************************************************************/

int64_t CVolume::GetSegmentSizeVoxel(int64_t segID) const {
    if (segID < GetSegmentCount())
        return meta_->segments->sizes[segID];
    else
        return 0;
}

/*****************************************************************/

const CSegmentation * CVolume::GetSegmentation() const {
  return segmentation_;
}

/*****************************************************************/

CVolumeMetadata::CSegments::CSegments(const CVolumeMetadata &meta, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes) {
  assert(raw_bboxes.size() % (6 * meta.segment_count) == 0);
  assert(raw_sizes.size() % meta.segment_count == 0);

  count = meta.segment_count;

  sizes.reserve(count);
  boundsVolume.reserve(count);
  boundsWorld.reserve(count);

  switch (meta.segment_size_type) {
    case MetaDataType::UInt8: {
      const uint8_t * data = reinterpret_cast<const uint8_t *>(raw_sizes.data());
      sizes.insert(sizes.end(), &data[0], &data[count]);
      break;
    }
    case MetaDataType::UInt16: {
      const uint16_t * data = reinterpret_cast<const uint16_t *>(raw_sizes.data());
      sizes.insert(sizes.end(), &data[0], &data[count]);
      break;
    }
    case MetaDataType::UInt32: {
      const uint32_t * data = reinterpret_cast<const uint32_t *>(raw_sizes.data());
      sizes.insert(sizes.end(), &data[0], &data[count]);
      break;
    }
  }
        
  switch (meta.segment_bbox_type) {
    case MetaDataType::UInt8: {
      const uint8_t * data = reinterpret_cast<const uint8_t *>(raw_bboxes.data());
      for (int i = 0; i < count; ++i) {
        vmml::Vector<3, int64_t> min(data[6*i+0], data[6*i+1], data[6*i+2]);
        vmml::Vector<3, int64_t> max(data[6*i+3], data[6*i+4], data[6*i+5]);

        boundsVolume.push_back(vmml::AABB<int64_t>(min, max));
        boundsWorld.push_back(vmml::AABB<int64_t>(min * meta.voxel_resolution + meta.physical_offset.getMin(), max * meta.voxel_resolution + meta.physical_offset.getMin()));
      }
      break;
    }
    case MetaDataType::UInt16: {
      const uint16_t * data = reinterpret_cast<const uint16_t *>(raw_bboxes.data());
      for (int i = 0; i < count; ++i) {
        vmml::Vector<3, int64_t> min(data[6*i+0], data[6*i+1], data[6*i+2]);
        vmml::Vector<3, int64_t> max(data[6*i+3], data[6*i+4], data[6*i+5]);

        boundsVolume.push_back(vmml::AABB<int64_t>(min, max));
        boundsWorld.push_back(vmml::AABB<int64_t>(min * meta.voxel_resolution + meta.physical_offset.getMin(), max * meta.voxel_resolution + meta.physical_offset.getMin()));
      }
      break;
    }
    case MetaDataType::UInt32: {
      const uint32_t * data = reinterpret_cast<const uint32_t *>(raw_bboxes.data());
      for (int i = 0; i < count; ++i) {
        vmml::Vector<3, int64_t> min(data[6*i+0], data[6*i+1], data[6*i+2]);
        vmml::Vector<3, int64_t> max(data[6*i+3], data[6*i+4], data[6*i+5]);

        boundsVolume.push_back(vmml::AABB<int64_t>(min, max));
        boundsWorld.push_back(vmml::AABB<int64_t>(min * meta.voxel_resolution + meta.physical_offset.getMin(), max * meta.voxel_resolution + meta.physical_offset.getMin()));
      }
      break;
    }
  }
}

/*****************************************************************/

CVolumeMetadata::CVolumeMetadata(const std::vector<unsigned char> &raw_json, const std::vector<unsigned char> &raw_bboxes, const std::vector<unsigned char> &raw_sizes) {
  auto metadata = json::parse(std::string(raw_json.begin(), raw_json.end()));
  auto tmpVec = metadata["physical_offset_min"];
  physical_offset.setMin(vmml::Vector<3, int64_t>(tmpVec[0], tmpVec[1], tmpVec[2]));

  tmpVec = metadata["physical_offset_max"];
  physical_offset.setMax(vmml::Vector<3, int64_t>(tmpVec[0], tmpVec[1], tmpVec[2]));

  tmpVec = metadata["chunk_voxel_dimensions"];
  volume_dimensions = vmml::Vector<3, int64_t>(tmpVec[0], tmpVec[1], tmpVec[2]);

  tmpVec = metadata["voxel_resolution"];
  voxel_resolution = vmml::Vector<3, int64_t>(tmpVec[0], tmpVec[1], tmpVec[2]);

  resolution_units = metadata["resolution_units"];

  segment_id_type = StringToMetaDataType(metadata["segment_id_type"], &segment_id_type_size);
  segment_bbox_type = StringToMetaDataType(metadata["bounding_box_type"]);
  segment_size_type = StringToMetaDataType(metadata["size_type"]);
  segment_count = metadata["num_segments"];

  segments = new CSegments(*this, raw_bboxes, raw_sizes);
}

/*****************************************************************/
CVolumeMetadata::~CVolumeMetadata() {
    delete segments;
}

/*****************************************************************/

const vmml::AABB<int64_t> & CVolumeMetadata::GetPhysicalBounds() const {
    return physical_offset;
}

/*****************************************************************/

const vmml::Vector<3, int64_t> & CVolumeMetadata::GetVolumeDimensions() const {
    return volume_dimensions;
}

/*****************************************************************/

const vmml::Vector<3, int64_t> & CVolumeMetadata::GetVoxelResolution() const {
    return voxel_resolution;
}

/*****************************************************************/

const std::string & CVolumeMetadata::GetResolutionUnit() const {
    return resolution_units;
}

/*****************************************************************/

uint8_t CVolumeMetadata::GetSegmentTypeSize() const {
    return segment_id_type_size;
}

/*****************************************************************/

int64_t CVolumeMetadata::GetSegmentCount() const {
    return segment_count;
}

/*****************************************************************/