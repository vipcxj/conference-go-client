#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/opt.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/str_helper.hpp"
#include <filesystem>
#include <regex>
#include <cstring>
#include <algorithm>

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavdevice/avdevice.h"
}

namespace cfgo
{
    namespace video
    {

        bool DeviceInfo::support_media_type(AVMediaType media_type) const noexcept
        {
            return media_types.empty() || std::find(media_types.begin(), media_types.end(), media_type) != media_types.end();
        }

        std::ostream & operator << (std::ostream & os, const DeviceInfo & info)
        {
            return os << "DeviceInfo { "
                << "name: \"" << info.name << "\", "
                << "desc: \"" << info.description << "\", "
                << "media_types: [" << cfgo::str_join(info.media_types, ", ") << "] }";
        }

        std::ostream & operator << (std::ostream & os, const DeviceInfoList & list)
        {
            os << std::endl;
            if (list.ifmt)
            {
                os << "AVInputFormat: " << list.ifmt->name << std::endl;
            }
            if (list.ofmt)
            {
                os << "AVOutputFormat: " << list.ofmt->name << std::endl;
            }
            auto i = 0;
            for (auto & info : list.devices)
            {
                if (i == list.default_device)
                {
                    os << "* ";
                }
                os << info << std::endl;
                ++i;
            }
            return os;
        }

        static int is_v4l2_audio_dev(const char *name)
        {
            return !strncmp(name, "/dev/radio", 10);
        }

        static int is_v4l2_video_dev(const char *name)
        {
            return !strncmp(name, "/dev/video", 10) ||
                !strncmp(name, "/dev/vbi", 8);
        }

        // static int is_v4l2_dev(const char *name)
        // {
        //     return !strncmp(name, "video", 5) ||
        //         !strncmp(name, "radio", 5) ||
        //         !strncmp(name, "vbi", 3) ||
        //         !strncmp(name, "v4l-subdev", 10);
        // }

        // static std::vector<std::string> list_possible_v4l2_devices()
        // {
        //     namespace fs = std::filesystem;
        //     std::vector<std::string> ret;
        //     fs::directory_iterator devs("/dev");
        //     for (auto iter = fs::begin(devs); iter != fs::end(devs); ++ iter)
        //     {
        //         if (is_v4l2_dev(iter->path().filename().string().c_str()))
        //         {
        //             ret.push_back(iter->path().string());
        //         }
        //     }
        //     return ret;
        // }

        constexpr const char* POSSIBLE_I_DEV[] = {"dshow", "avfoundation", "android_camera", "v4l2"};

        device_info_list_t to_devices(AVDeviceInfoList * devices, const AVInputFormat * ifmt, const AVOutputFormat * ofmt)
        {
            device_info_list_t dev_list { ifmt, ofmt };
            AVDeviceInfo * av_info;
            for (int i = 0; i < devices->nb_devices; i++)
            {
                av_info = devices->devices[i];
                DeviceInfo info { .name = av_info->device_name, .description = av_info->device_description };
                for (int j = 0; j < av_info->nb_media_types; j++)
                {
                    info.media_types.push_back(av_info->media_types[j]);
                }
                if (info.media_types.empty())
                {
                    if ((ifmt && !strcmp(ifmt->name, "v4l2")) || (ofmt && !strcmp(ofmt->name, "v4l2")))
                    {
                        if (is_v4l2_audio_dev(av_info->device_name))
                        {
                            info.media_types.push_back(AVMediaType::AVMEDIA_TYPE_AUDIO);
                        }
                        if (is_v4l2_video_dev(av_info->device_name))
                        {
                            info.media_types.push_back(AVMediaType::AVMEDIA_TYPE_VIDEO);
                        }
                    }
                }
                
                dev_list.devices.push_back(std::move(info));
            }
            return std::move(dev_list);
        }
        
        device_info_list_t list_devices(dev_type_t dev_type)
        {
            AVDeviceInfoList * devices = nullptr;
            int index;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            index = 0; // "dshow"
#elif __APPLE__
            index = 1; // "avfoundation"
#elif __ANDROID__
            index = 2; // "android_camera"
#elif __linux__
            index = 3; // "v4l2"
#else
            index = 0;           
#endif
            switch (dev_type)
            {
            case dev_type_t::INPUT:
            {
                auto ifmt = av_find_input_format(POSSIBLE_I_DEV[index]);
                if (!ifmt)
                {
                    for (int i = 0; i < sizeof(POSSIBLE_I_DEV) / size_t(POSSIBLE_I_DEV[0]); i++)
                    {
                        if (i != index)
                        {
                            ifmt = av_find_input_format(POSSIBLE_I_DEV[i]);
                            if (!ifmt)
                            {
                                break;
                            }
                        }
                    }
                }
                if (!ifmt)
                {
                    return { nullptr, nullptr };
                }
                check_av_err(avdevice_list_input_sources(ifmt, nullptr, nullptr, &devices), "could not list input devices, ");
                DEFER({
                    avdevice_free_list_devices(&devices);
                });
                return to_devices(devices, ifmt, nullptr);
            }
            case dev_type_t::OUTPUT:
            {
                auto ofmt = av_guess_format(POSSIBLE_I_DEV[index], nullptr, nullptr);
                if (!ofmt)
                {
                    for (int i = 0; i < sizeof(POSSIBLE_I_DEV) / size_t(POSSIBLE_I_DEV[0]); i++)
                    {
                        if (i != index)
                        {
                            ofmt = av_guess_format(POSSIBLE_I_DEV[i], nullptr, nullptr);
                            if (!ofmt)
                            {
                                break;
                            }
                        }
                    }
                }
                if (!ofmt)
                {
                    return { nullptr, nullptr};
                }
                check_av_err(avdevice_list_output_sinks(ofmt, nullptr, nullptr, &devices), "could not list output devices, ");
                DEFER({
                    avdevice_free_list_devices(&devices);
                });
                return to_devices(devices, nullptr, ofmt);
            }
            default:
                throw cpptrace::invalid_argument(fmt::format("invalid device type {}", (int) dev_type));
            }
        }
    } // namespace video
    
} // namespace cfgo
