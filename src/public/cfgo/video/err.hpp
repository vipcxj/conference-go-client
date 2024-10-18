#ifndef _CFGO_VIDEO_ERR_HPP_
#define _CFGO_VIDEO_ERR_HPP_

#include "cpptrace/cpptrace.hpp"

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