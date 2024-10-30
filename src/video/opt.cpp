#include "cfgo/video/opt.hpp"

namespace cfgo
{
    namespace video
    {
        AvOpt create_av_opt(const opt_t & opts)
        {
            AVDictionary * av_opt = nullptr;
            for (auto & [key, value] : opts)
            {
                av_dict_set(&av_opt, key.c_str(), value.c_str(), 0);
            }
            return { av_opt };
        }
    } // namespace video
    
} // namespace cfgo
