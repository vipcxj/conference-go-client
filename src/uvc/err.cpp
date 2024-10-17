#include "cfgo/uvc/err.hpp"

#include "cfgo/fmt.hpp"

namespace cfgo
{
    namespace uvc
    {
        Error::Error(uvc_error_t err, std::string prefix_msg, bool trace):
            cpptrace::exception_with_message(
                fmt::format(
                    "{}{}",
                    std::move(prefix_msg), uvc_strerror(err)),
                    trace ? cpptrace::detail::get_raw_trace_and_absorb() : cpptrace::raw_trace{}
            ),
            m_err(err)
        {} 

        void check_err(uvc_error_t err, std::string prefix_msg, bool trace)
        {
            if (err < 0)
            {
                throw error_t(err, std::move(prefix_msg), trace);
            }
        }
    } // namespace uvc
    
} // namespace cfgo
