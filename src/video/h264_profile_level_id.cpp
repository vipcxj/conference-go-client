#include "cfgo/video/h264_profile_level_id.hpp"

#include <charconv>
#include <cstdlib>

namespace cfgo
{
    namespace video
    {
        // For level_idc=11 and profile_idc=0x42, 0x4D, or 0x58, the constraint set3
        // flag specifies if level 1b or level 1.1 is used.
        const uint8_t kConstraintSet3Flag = 0x10;

        // Convert a string of 8 characters into a byte where the positions containing
        // character c will have their bit set. For example, c = 'x', str = "x1xx0000"
        // will return 0b10110000. constexpr is used so that the pattern table in
        // kProfilePatterns is statically initialized.
        constexpr uint8_t byte_mask_string(char c, const char (&str)[9])
        {
            return (str[0] == c) << 7 | (str[1] == c) << 6 | (str[2] == c) << 5 |
                   (str[3] == c) << 4 | (str[4] == c) << 3 | (str[5] == c) << 2 |
                   (str[6] == c) << 1 | (str[7] == c) << 0;
        }

        // Class for matching bit patterns such as "x1xx0000" where 'x' is allowed to be
        // either 0 or 1.
        class BitPattern
        {
        public:
            explicit constexpr BitPattern(const char (&str)[9])
                : mask_(~byte_mask_string('x', str)),
                  masked_value_(byte_mask_string('1', str)) {}
            bool is_match(uint8_t value) const { return masked_value_ == (value & mask_); }

        private:
            const uint8_t mask_;
            const uint8_t masked_value_;
        };
        // Table for converting between profile_idc/profile_iop to H264Profile.
        struct ProfilePattern
        {
            const uint8_t profile_idc;
            const BitPattern profile_iop;
            const int profile;
        };
        // This is from https://tools.ietf.org/html/rfc6184#section-8.1.
        constexpr ProfilePattern kProfilePatterns[] = {
            {0x42, BitPattern("x1xx0000"), AV_PROFILE_H264_CONSTRAINED_BASELINE},
            {0x4D, BitPattern("1xxx0000"), AV_PROFILE_H264_CONSTRAINED_BASELINE},
            {0x58, BitPattern("11xx0000"), AV_PROFILE_H264_CONSTRAINED_BASELINE},
            {0x42, BitPattern("x0xx0000"), AV_PROFILE_H264_BASELINE},
            {0x58, BitPattern("10xx0000"), AV_PROFILE_H264_BASELINE},
            {0x4D, BitPattern("0x0x0000"), AV_PROFILE_H264_MAIN},
            {0x58, BitPattern("00xx0000"), AV_PROFILE_H264_EXTENDED},
            {0x64, BitPattern("00000000"), AV_PROFILE_H264_HIGH},
            {0x6E, BitPattern("00000000"), AV_PROFILE_H264_HIGH_10},
            {0x7A, BitPattern("00000000"), AV_PROFILE_H264_HIGH_422},
            {0xF4, BitPattern("00000000"), AV_PROFILE_H264_HIGH_444_PREDICTIVE},
            {0x6E, BitPattern("00010000"), AV_PROFILE_H264_HIGH_10_INTRA},
            {0x7A, BitPattern("00010000"), AV_PROFILE_H264_HIGH_422_INTRA},
            {0xF4, BitPattern("00010000"), AV_PROFILE_H264_HIGH_444_INTRA},
            {0x2C, BitPattern("00010000"), AV_PROFILE_H264_CAVLC_444}};

        struct LevelConstraint
        {
            const int max_macroblocks_per_second;
            const int max_macroblock_frame_size;
            const H264Level level;
        };
        // This is from ITU-T H.264 (02/2016) Table A-1 – Level limits.
        static constexpr LevelConstraint kLevelConstraints[] = {
            {1485, 99, H264Level::kLevel1},
            {1485, 99, H264Level::kLevel1_b},
            {3000, 396, H264Level::kLevel1_1},
            {6000, 396, H264Level::kLevel1_2},
            {11880, 396, H264Level::kLevel1_3},
            {11880, 396, H264Level::kLevel2},
            {19800, 792, H264Level::kLevel2_1},
            {20250, 1620, H264Level::kLevel2_2},
            {40500, 1620, H264Level::kLevel3},
            {108000, 3600, H264Level::kLevel3_1},
            {216000, 5120, H264Level::kLevel3_2},
            {245760, 8192, H264Level::kLevel4},
            {245760, 8192, H264Level::kLevel4_1},
            {522240, 8704, H264Level::kLevel4_2},
            {589824, 22080, H264Level::kLevel5},
            {983040, 36864, H264Level::kLevel5_1},
            {2073600, 36864, H264Level::kLevel5_2},
        };

        std::optional<H264ProfileLevelId> parse_h264_profile_level_id(std::string_view s)
        {
            // The string should consist of 3 bytes in hexadecimal format.
            if (s.length() != 6u)
                return std::nullopt;
            uint32_t profile_level_id_numeric;
#if __cpp_lib_to_chars >= 202306L
            if (std::from_chars(s.data(), s.data() + s.size(), profile_level_id_numeric))
#else
            if (std::from_chars(s.data(), s.data() + s.size(), profile_level_id_numeric).ec == std::errc{})
#endif
            {
                if (profile_level_id_numeric == 0)
                    return std::nullopt;
                // Separate into three bytes.
                const uint8_t level_idc =
                    static_cast<uint8_t>(profile_level_id_numeric & 0xFF);
                const uint8_t profile_iop =
                    static_cast<uint8_t>((profile_level_id_numeric >> 8) & 0xFF);
                const uint8_t profile_idc =
                    static_cast<uint8_t>((profile_level_id_numeric >> 16) & 0xFF);
                // Parse level based on level_idc and constraint set 3 flag.
                H264Level level_casted = static_cast<H264Level>(level_idc);
                H264Level level;
                switch (level_casted)
                {
                case H264Level::kLevel1_1:
                    level = (profile_iop & kConstraintSet3Flag) != 0 ? H264Level::kLevel1_b
                                                                     : H264Level::kLevel1_1;
                    break;
                case H264Level::kLevel1:
                case H264Level::kLevel1_2:
                case H264Level::kLevel1_3:
                case H264Level::kLevel2:
                case H264Level::kLevel2_1:
                case H264Level::kLevel2_2:
                case H264Level::kLevel3:
                case H264Level::kLevel3_1:
                case H264Level::kLevel3_2:
                case H264Level::kLevel4:
                case H264Level::kLevel4_1:
                case H264Level::kLevel4_2:
                case H264Level::kLevel5:
                case H264Level::kLevel5_1:
                case H264Level::kLevel5_2:
                    level = level_casted;
                    break;
                default:
                    // Unrecognized level_idc.
                    return std::nullopt;
                }
                // Parse profile_idc/profile_iop into a Profile enum.
                for (const ProfilePattern &pattern : kProfilePatterns)
                {
                    if (profile_idc == pattern.profile_idc && pattern.profile_iop.is_match(profile_iop))
                    {
                        return H264ProfileLevelId(pattern.profile, level);
                    }
                }
            }
            // Unrecognized profile_idc/profile_iop combination.
            return std::nullopt;
        }

        std::optional<H264Level> h264_supported_level(int max_frame_pixel_count, float max_fps)
        {
            static const int kPixelsPerMacroblock = 16 * 16;
            for (int i = sizeof(kLevelConstraints) / sizeof(kLevelConstraints[0]) - 1; i >= 0; --i)
            {
                const LevelConstraint &level_constraint = kLevelConstraints[i];
                if (level_constraint.max_macroblock_frame_size * kPixelsPerMacroblock <=
                        max_frame_pixel_count &&
                    level_constraint.max_macroblocks_per_second <=
                        max_fps * level_constraint.max_macroblock_frame_size)
                {
                    return level_constraint.level;
                }
            }
            // No level supported.
            return std::nullopt;
        }

        std::optional<std::string> h264_profile_level_id_to_string(const H264ProfileLevelId &profile_level_id)
        {
            // Handle special case level == 1b.
            if (profile_level_id.level == H264Level::kLevel1_b)
            {
                switch (profile_level_id.profile)
                {
                case AV_PROFILE_H264_CONSTRAINED_BASELINE:
                    return "42f00b";
                case AV_PROFILE_H264_BASELINE:
                    return "42100b";
                case AV_PROFILE_H264_MAIN:
                    return "4d100b";
                // Level 1b is not allowed for other profiles.
                default:
                    return std::nullopt;
                }
            }
            const char *profile_idc_iop_string;
            switch (profile_level_id.profile)
            {
            case AV_PROFILE_H264_CONSTRAINED_BASELINE:
                profile_idc_iop_string = "42e0";
                break;
            case AV_PROFILE_H264_BASELINE:
                profile_idc_iop_string = "4200";
                break;
            case AV_PROFILE_H264_MAIN:
                profile_idc_iop_string = "4d00";
                break;
            case AV_PROFILE_H264_HIGH:
                profile_idc_iop_string = "6400";
                break;
            case AV_PROFILE_H264_HIGH_10:
                profile_idc_iop_string = "6e00";
                break;
            case AV_PROFILE_H264_HIGH_422:
                profile_idc_iop_string = "7a00";
                break;
            case AV_PROFILE_H264_HIGH_444_PREDICTIVE:
                profile_idc_iop_string = "f400";
                break;
            case AV_PROFILE_H264_HIGH_10_INTRA:
                profile_idc_iop_string = "6e10";
                break;
            case AV_PROFILE_H264_HIGH_422_INTRA:
                profile_idc_iop_string = "7a10";
                break;
            case AV_PROFILE_H264_HIGH_444_INTRA:
                profile_idc_iop_string = "f410";
                break;
            case AV_PROFILE_H264_CAVLC_444:
                profile_idc_iop_string = "2c10";
                break;
            // Unrecognized profile.
            default:
                return std::nullopt;
            }
            char str[7];
            snprintf(str, 7u, "%s%02x", profile_idc_iop_string,
                     static_cast<unsigned>(profile_level_id.level));
            return {str};
        }

    } // namespace video

} // namespace cfgo
