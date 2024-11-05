#ifndef _CFGO_VIDEO_CAMERA_HPP_
#define _CFGO_VIDEO_CAMERA_HPP_

#include <vector>
#include <string>
#include <string_view>
#include <ostream>

extern "C" {
    #include "libavformat/avformat.h"
}

#include "cfgo/fmt.hpp"

namespace cfgo
{
    namespace video
    {
        struct DeviceInfo
        {
            std::string name;
            std::string description;
            std::vector<AVMediaType> media_types;

            bool support_media_type(AVMediaType media_type) const noexcept;
        };

        std::ostream & operator << (std::ostream & os, const DeviceInfo & info);

        struct DeviceInfoList
        {
            std::vector<DeviceInfo> devices;
            int default_device = -1;
            const AVInputFormat * ifmt;
            const AVOutputFormat * ofmt;

            DeviceInfoList(const AVInputFormat * ifmt, const AVOutputFormat * ofmt): ifmt(ifmt), ofmt(ofmt) {}

            const DeviceInfo * select(int i = -1, AVMediaType media_type = AVMEDIA_TYPE_UNKNOWN) const
            {
                if (i < 0)
                {
                    i = default_device;
                }
                
                if (i >= 0)
                {
                    if (i < devices.size())
                    {
                        if (media_type == AVMEDIA_TYPE_UNKNOWN)
                        {
                            return &devices.at(i);
                        }
                        else
                        {
                            int j = 0;
                            for (auto & device : devices)
                            {
                                if (device.support_media_type(media_type))
                                {
                                    if (j ++ == i)
                                    {
                                        return &device;
                                    }
                                }
                            }
                            return nullptr;
                        }
                    }
                    else
                    {
                        return nullptr;
                    }
                }
                else if (!devices.empty())
                {
                    return &devices.at(0);
                }
                else
                {
                    return nullptr;
                }
            }

            const DeviceInfo * select(std::string_view name, AVMediaType media_type = AVMEDIA_TYPE_UNKNOWN) const
            {
                for (auto & device : devices)
                {
                    if (media_type == AVMEDIA_TYPE_UNKNOWN || device.support_media_type(media_type))
                    {
                        if (device.name == name)
                        {
                            return &device;
                        }
                    }
                }
                return nullptr;
            }
        };

        std::ostream & operator << (std::ostream & os, const DeviceInfoList & list);
        
        using device_info_t = DeviceInfo;
        using device_info_list_t = DeviceInfoList;

        enum class DevType
        {
            INPUT = 0,
            OUTPUT = 1
        };

        using dev_type_t = DevType; 

        device_info_list_t list_devices(dev_type_t dev_type = dev_type_t::INPUT);
        
    } // namespace video
    
} // namespace cfgo

template<>
struct fmt::formatter<cfgo::video::DeviceInfo> : fmt::ostream_formatter {};

template<>
struct fmt::formatter<cfgo::video::DeviceInfoList> : fmt::ostream_formatter {};


#endif