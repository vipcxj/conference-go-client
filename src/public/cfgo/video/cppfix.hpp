#ifndef _CFGO_VIDEO_CPPFIX_HPP_
#define _CFGO_VIDEO_CPPFIX_HPP_

extern "C" {
    #include "libavutil/timestamp.h"
}

#include <string>

#ifdef av_ts2str
#undef av_ts2str
inline std::string av_ts2string(int64_t ts) {
    char buf[AV_TS_MAX_STRING_SIZE] {0};
    return av_ts_make_string(buf, ts);
}
#define av_ts2str(ts) av_ts2string(ts).c_str()
#endif  // av_ts2str

#ifdef av_ts2timestr
#undef av_ts2timestr
inline std::string av_ts2timestring(int64_t ts, const AVRational *tb) {
    char buf[AV_TS_MAX_STRING_SIZE] {0};
    return av_ts_make_time_string(buf, ts, tb);
}
#define av_ts2timestr(ts, tb) av_ts2timestring(ts, tb).c_str()
#endif  // av_ts2timestr

#endif