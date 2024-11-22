#ifndef _CFGO_VIDEO_H264_PROFILE_LEVEL_ID_HPP_
#define _CFGO_VIDEO_H264_PROFILE_LEVEL_ID_HPP_

extern "C"
{
#include "libavcodec/avcodec.h"
}

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cfgo
{
    namespace video
    {
        // All values are equal to ten times the level number, except level 1b which is
        // special.
        enum class H264Level
        {
            kLevel1_b = 0,
            kLevel1 = 10,
            kLevel1_1 = 11,
            kLevel1_2 = 12,
            kLevel1_3 = 13,
            kLevel2 = 20,
            kLevel2_1 = 21,
            kLevel2_2 = 22,
            kLevel3 = 30,
            kLevel3_1 = 31,
            kLevel3_2 = 32,
            kLevel4 = 40,
            kLevel4_1 = 41,
            kLevel4_2 = 42,
            kLevel5 = 50,
            kLevel5_1 = 51,
            kLevel5_2 = 52,
            kLevel6 = 60,
            kLevel6_1 = 61,
            kLevel6_2 = 62
        };

        struct H264ProfileLevelId
        {
            constexpr H264ProfileLevelId(int profile, H264Level level)
                : profile(profile), level(level) {}
            int profile;
            H264Level level;

            int64_t max_bit_rate() const noexcept;
        };

        std::optional<H264ProfileLevelId> parse_h264_profile_level_id(std::string_view str);
        std::optional<H264Level> h264_supported_level(int max_frame_pixel_count, float max_fps);
        void h264_max_resolution(H264Level level, int & width, int & height);
        std::optional<std::string> h264_profile_level_id_to_string(const H264ProfileLevelId &profile_level_id);
    } // namespace video
} // namespace cfgo

#endif