#ifndef _CFGO_UVC_ERR_HPP_
#define _CFGO_UVC_ERR_HPP_

#include "cpptrace/cpptrace.hpp"
#include "libuvc/libuvc.h"

namespace cfgo
{
    namespace uvc
    {
        class Error : public cpptrace::exception_with_message
        {
        public:
            Error(uvc_error_t err, std::string prefix_msg, bool trace = false);

            uvc_error_t err() const noexcept
            {
                return m_err;
            }
        private:
            uvc_error_t m_err;
        };

        using error_t = Error;

        void check_err(uvc_error_t err, std::string prefix_msg = "", bool trace = true);
    } // namespace uvc
    
} // namespace cfgo


#endif