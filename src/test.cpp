#include <string>
#include <vector>
#include <map>
#include <memory>

#include "Volume.h"
#include "SpawnerWrapper.cpp"

#include <zi/timer.hpp>

/*****************************************************************/

#define PRE_SEGMENTS { 81, 89,183,248,250,258,284,739,794,843,891,946,1047,1272,1340,1402,1443,1645,1703 }

void loadFile(const char * filename, char ** buf, uint32_t * buf_len, bool null_terminate = false) {
  std::ifstream f(filename, std::ifstream::binary);
  f.seekg(0, std::ifstream::end);
  *buf_len = (uint32_t)f.tellg();
  f.seekg(0);

  if (null_terminate) {
    *buf = new char[(*buf_len) + 1];
    (*buf)[*buf_len] = '\0';
  }
  else {
    *buf = new char[*buf_len];
  }

  f.read(*buf, (std::streamsize)(*buf_len));

  if (null_terminate) {
    (*buf_len)++;
  }
}

int main(int argc, char* argv[]) {
  std::vector<uint32_t> seg = PRE_SEGMENTS;

  CInputVolume pre, post;
  char * pre_metadata, * pre_sizes, * pre_bounds, * pre_segmentation, * post_metadata, * post_sizes, * post_bounds, * post_segmentation;
  uint32_t tmp, pre_sizes_len, pre_bounds_len, pre_segmentation_len, post_sizes_len, post_bounds_len, post_segmentation_len;

  loadFile("/tmp/Volume-75853-75854%2Fmetadata.json", &pre_metadata, &tmp, true);
  loadFile("/tmp/Volume-75853-75854%2Fsegmentation.bbox", &pre_bounds, &pre_bounds_len);
  loadFile("/tmp/Volume-75853-75854%2Fsegmentation.size", &pre_sizes, &pre_sizes_len);
  loadFile("/tmp/Volume-75853-75854%2Fsegmentation", &pre_segmentation, &pre_segmentation_len);


  loadFile("/tmp/Volume-75571-75572%2Fmetadata.json", &post_metadata, &tmp, true);
  loadFile("/tmp/Volume-75571-75572%2Fsegmentation.bbox", &post_bounds, &post_bounds_len);
  loadFile("/tmp/Volume-75571-75572%2Fsegmentation.size", &post_sizes, &post_sizes_len);
  loadFile("/tmp/Volume-75571-75572%2Fsegmentation", &post_segmentation, &post_segmentation_len);

  pre.metadata = pre_metadata;
  pre.bboxesLength = pre_bounds_len;
  pre.bboxes = (uint8_t *)pre_bounds;
  pre.sizesLength = pre_sizes_len;
  pre.sizes = (uint8_t *)pre_sizes;
  pre.segmentationLength = pre_segmentation_len;
  pre.segmentation = (uint8_t *)pre_segmentation;

  post.metadata = post_metadata;
  post.bboxesLength = post_bounds_len;
  post.bboxes = (uint8_t *)post_bounds;
  post.sizesLength = post_sizes_len;
  post.sizes = (uint8_t *)post_sizes;
  post.segmentationLength = post_segmentation_len;
  post.segmentation = (uint8_t *)post_segmentation;

  CTaskSpawner * spawn = TaskSpawner_Spawn(&pre, &post, &seg[0], seg.size(), 1.0);

  TaskSpawner_Release(spawn);

}