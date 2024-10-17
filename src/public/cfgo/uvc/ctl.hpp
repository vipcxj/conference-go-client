#ifndef _CFGO_UVC_CTL_HPP_
#define _CFGO_UVC_CTL_HPP_

#include "libuvc/libuvc.h"
#include "cfgo/uvc/dev.hpp"
#include <memory>
#include <functional>

namespace cfgo
{
    namespace uvc
    {
        class StreamControl
        {
        private:
            dev_handle_t m_handle;
            uvc_stream_ctrl_t m_ctl {};
            std::function<void(uvc_frame *frame)> m_cb;
        public:
            StreamControl(dev_handle_t && handle);
            StreamControl(dev_handle_t && handle, enum uvc_frame_format cf, int width, int height, int fps);
        };
        
        using stream_ctl_t = StreamControl;
        using stream_ctl_ptr_t = std::shared_ptr<stream_ctl_t>;
        using stream_ctl_uptr_t = std::unique_ptr<stream_ctl_t>;
    } // namespace uvc
    
} // namespace cfgo


#endif