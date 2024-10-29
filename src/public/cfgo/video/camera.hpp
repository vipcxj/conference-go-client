#ifndef _CFGO_VIDEO_CAMERA_HPP_
#define _CFGO_VIDEO_CAMERA_HPP_

#include <vector>
#include <string>
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
        };

        std::ostream & operator << (std::ostream & os, const DeviceInfo & info);

        struct DeviceInfoList
        {
            std::vector<DeviceInfo> devices;
            int default_device = -1;
        };

        std::ostream & operator << (std::ostream & os, const DeviceInfoList & list);
        
        using device_info_t = DeviceInfo;
        using device_info_list_t = DeviceInfoList;

        device_info_list_t list_devices();
        
    } // namespace video
    
} // namespace cfgo

template<>
struct fmt::formatter<cfgo::video::DeviceInfo> : fmt::ostream_formatter {};

template<>
struct fmt::formatter<cfgo::video::DeviceInfoList> : fmt::ostream_formatter {};


#endif