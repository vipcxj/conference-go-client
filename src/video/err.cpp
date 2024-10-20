#include "cfgo/video/err.hpp"
#include "cfgo/fmt.hpp"

namespace cfgo
{
    namespace video
    {
        AVError::AVError(int err, std::string prefix_msg, bool trace):
            cpptrace::exception_with_message(
                fmt::format(
                    "{}{}",
                    std::move(prefix_msg), av_err2string(err)
                ),
                trace ? cpptrace::detail::get_raw_trace_and_absorb() : cpptrace::raw_trace{}
            ),
            m_err(err)
        {} 

        void check_av_err(int err, std::string prefix_msg, bool trace)
        {
            if (err < 0)
            {
                throw av_error_t(err, std::move(prefix_msg), trace);
            }
        }
    } // namespace uvc
    
} // namespace cfgo