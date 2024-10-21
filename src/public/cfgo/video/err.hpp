#ifndef _CFGO_VIDEO_ERR_HPP_
#define _CFGO_VIDEO_ERR_HPP_

#include "cpptrace/cpptrace.hpp"
extern "C" {
#include "libavutil/error.h"
}

#ifdef av_err2str
#undef av_err2str
#include <string>
inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif  // av_err2str

namespace cfgo
{
    namespace video
    {
        class AVError : public cpptrace::exception_with_message
        {
        public:
            AVError(int err, std::string prefix_msg, bool trace = false);

            int err() const noexcept
            {
                return m_err;
            }
        private:
            int m_err;
        };

        using av_error_t = AVError;

        void check_av_err(int err, std::string prefix_msg = "", bool trace = true);
    } // namespace video
    
} // namespace cfgo


#endif