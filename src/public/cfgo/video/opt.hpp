#ifndef _CFGO_VIDEO_OPT_HPP_
#define _CFGO_VIDEO_OPT_HPP_

#include <unordered_map>
#include <string>

extern "C" {
    #include "libavutil/opt.h"
}

namespace cfgo
{
    namespace video
    {
        using opt_t = std::unordered_map<std::string, std::string>;

        struct AvOpt
        {
            AVDictionary * av_opt = nullptr;

            ~AvOpt()
            {
                av_dict_free(&av_opt);
            }

            AVDictionary * & get() noexcept
            {
                return av_opt;
            }

            AVDictionary * const & get() const noexcept
            {
                return av_opt;
            }

            AVDictionary * operator->() noexcept
            {
                return av_opt;
            }

            const AVDictionary * operator->() const noexcept
            {
                return av_opt;
            }
        };
        

        AvOpt create_av_opt(const opt_t & opts);
    } // namespace video
    
    
} // namespace name


#endif