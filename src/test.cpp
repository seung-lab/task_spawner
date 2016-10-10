#include <string>
#include <vector>
#include <map>
#include <memory>

#include "CurlObject.h"
#include "LZMADecode.h"
#include "Volume.h"
#include "SpawnHelper.h"

#include <zi/timer.hpp>

/*****************************************************************/

const char PRE_PATH[] = "https://storage.googleapis.com/pinky_3x3x2_2/pinky/3x3x2_2/hypersquare/chunk_17107-18130_23251-24274_4003-4130/";
const char POST_PATH[] = "https://storage.googleapis.com/pinky_3x3x2_2/pinky/3x3x2_2/hypersquare/chunk_17107-18130_23251-24274_4115-4242/";

const int16_t PRE_SEGMENTS[] = { 318,324,348,396,406,448,452,453,520,534,623,625,698,715,786,787,788,789,793,885,887,971,977,978,980,982,1724,2303,2304,2365,2535,2542,2624,2728,2834,2925,2989,3001,3021,3071,3072,3074,3109,3160,3171,3256,3278,3421,3434,3511,3536,3685,3714,3715,3718,3760,3767,3769,3770,3807,3843,3878,3890,3891,3964,4045,4090,4199,4284,4357,4358,4446,4541,4603,4716,4772,4773,4968,5033,5213,5216,5217,5270,5313,5361,5417,5534,5645,5653,5766,6050,6938,7272,8269,8373,8461,8462,8663,8664,8667,8876,9084,9093,9266,9267,9395,9403,9495,9497,9537,9539,9545,9749,9751,9761,9791,9859,9885,9899,9900,9905,9972,10016,10018,10094,10099,10100,10246,10253,10295,10296,10384,10388,10389,10467,10473,10503,10531,10581 };


int main(int argc, char* argv[]) {
  std::set<uint32_t> selected(std::begin(PRE_SEGMENTS), std::end(PRE_SEGMENTS));
  std::string pre = std::string(PRE_PATH);
  std::string post = std::string(POST_PATH);

  // TODO
}