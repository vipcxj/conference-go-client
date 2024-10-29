#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/str_helper.hpp"
#include <filesystem>
#include <regex>

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
        
        device_info_list_t list_devices()
        {
            avdevice_register_all();
            AVFormatContext * av_ctx = nullptr;
            AVDeviceInfoList * devices = nullptr;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            check_av_err(avformat_alloc_output_context2(&av_ctx, nullptr, "dshow", nullptr), "could not allocate dshow format context, ");
#elif __APPLE__
            return {};
#elif __ANDROID__
            return {};
#elif __linux__
            auto ifmt = av_find_input_format("v4l2");
            if (!ifmt)
            {
                throw cpptrace::runtime_error("could not find v4l2 input format");
            }
            av_ctx = avformat_alloc_context();
            if (!av_ctx)
            {
                throw cpptrace::runtime_error("could not allocate the format context");
            }
            DEFER({
                avformat_free_context(av_ctx);
            });
            check_av_err(avformat_open_input(&av_ctx, "dumy", ifmt, nullptr), "could not allocate video4linux2 format context, ");
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
