#include "cfgo/uvc/ctl.hpp"
#include "cfgo/uvc/err.hpp"
#include <utility>

namespace cfgo
{
    namespace uvc
    {
        StreamControl::StreamControl(dev_handle_t && handle): m_handle(std::move(handle))
        {
            m_handle.check_valid();
            check_err(uvc_probe_stream_ctrl(m_handle.unwrap(), &m_ctl), "failed to negotiate a stream control, ");
        }
        StreamControl::StreamControl(dev_handle_t && handle, enum uvc_frame_format cf, int width, int height, int fps): m_handle(std::move(handle))
        {
            m_handle.check_valid();
            check_err(uvc_get_stream_ctrl_format_size(m_handle.unwrap(), &m_ctl, cf, width, height, fps), "failed to negotiate a stream control, ");
        }
    } // namespace uvc
    
} // namespace cfgo
