#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/opt.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/str_helper.hpp"
#include <filesystem>
#include <regex>
#include <cstring>

extern "C" {
    #include "libavformat/avformat.h"
    #include "libavdevice/avdevice.h"
}

namespace cfgo
{
    namespace video
    {

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

        // void init_fmt_ctx_priv_data(AVFormatContext * s)
        // {
        //     /* Allocate private data. */
        //     if (ffifmt(s->iformat)->priv_data_size > 0) {
        //         if (!(s->priv_data = av_mallocz(ffifmt(s->iformat)->priv_data_size))) {
        //             ret = AVERROR(ENOMEM);
        //             goto fail;
        //         }
        //         if (s->iformat->priv_class) {
        //             *(const AVClass **) s->priv_data = s->iformat->priv_class;
        //             av_opt_set_defaults(s->priv_data);
        //             if ((ret = av_opt_set_dict(s->priv_data, &tmp)) < 0)
        //                 goto fail;
        //         }
        //     }
        // }

        static int is_v4l2_dev(const char *name)
        {
            return !strncmp(name, "video", 5) ||
                !strncmp(name, "radio", 5) ||
                !strncmp(name, "vbi", 3) ||
                !strncmp(name, "v4l-subdev", 10);
        }

        static std::vector<std::string> list_possible_v4l2_devices()
        {
            namespace fs = std::filesystem;
            std::vector<std::string> ret;
            fs::directory_iterator devs("/dev");
            for (auto iter = fs::begin(devs); iter != fs::end(devs); ++ iter)
            {
                if (is_v4l2_dev(iter->path().filename().c_str()))
                {
                    ret.push_back(iter->path().string());
                }
            }
            return ret;
        }
        
        device_info_list_t list_devices()
        {
            AVDeviceInfoList * devices = nullptr;
            auto av_ctx = avformat_alloc_context();
            if (!av_ctx)
            {
                throw cpptrace::runtime_error("could not allocate the format context");
            }
            DEFER({
                avformat_free_context(av_ctx);
            });

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            auto ifmt = av_find_input_format("dshow");
            if (!ifmt)
            {
                throw cpptrace::runtime_error("could not find input format dshow");
            }
            return {};
#elif __APPLE__
            auto ifmt = av_find_input_format("avfoundation");
            if (!ifmt)
            {
                throw cpptrace::runtime_error("could not find input format avfoundation");
            }
            return {};
#elif __ANDROID__
            auto ifmt = av_find_input_format("android_camera");
            if (!ifmt)
            {
                throw cpptrace::runtime_error("could not find input format android_camera, this api require android level >= 24 (Android 7.0)");
            }
            return {};
#elif __linux__
            auto ifmt = av_find_input_format("v4l2");
            if (!ifmt)
            {
                throw cpptrace::runtime_error("could not find input format v4l2");
            }
            int err;
            for (auto & possible_device : list_possible_v4l2_devices())
            {
                err = avformat_open_input(&av_ctx, possible_device.c_str(), ifmt, nullptr);
                if (err >= 0)
                {
                    break;
                }
            }
            check_av_err(err, "could not open v4l2 input device, ");
            DEFER({
                avformat_close_input(&av_ctx);
            });
#else
            return {};            
#endif
            check_av_err(avdevice_list_devices(av_ctx, &devices), "could not list devices, ");
            DEFER({
                avdevice_free_list_devices(&devices);
            });
            device_info_list_t dev_list {};
            AVDeviceInfo * av_info;
            for (int i = 0; i < devices->nb_devices; i++)
            {
                av_info = devices->devices[i];
                DeviceInfo info { .name = av_info->device_name, .description = av_info->device_description };
                for (int j = 0; j < av_info->nb_media_types; j++)
                {
                    info.media_types.push_back(av_info->media_types[j]);
                }
                dev_list.devices.push_back(std::move(info));
            }
            return std::move(dev_list);
        }
    } // namespace video
    
} // namespace cfgo
