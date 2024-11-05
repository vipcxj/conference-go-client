#ifndef _CFGO_VIDEO_MEDIA_SOURCE_HPP_
#define _CFGO_VIDEO_MEDIA_SOURCE_HPP_

extern "C" {
    #include "libavformat/avformat.h"
}

#include <string>
#include <memory>
#include "cfgo/async.hpp"

namespace cfgo
{
    namespace video
    {
        enum class MediaSourceType
        {
            DEVICE,
            FILE
        };

        class MediaSource
        {
        private:
            AVFormatContext * m_fmt_ctx = nullptr;
            MediaSourceType m_source_type;
            std::string m_url_or_name;
            asiochan::unbounded_channel<AVFrame *> m_frame_ch;
        public:
            MediaSource(MediaSourceType source_type, const std::string & url_or_name);
            ~MediaSource();
        };

        using media_source_t = MediaSource;
        using media_source_ptr_t = std::shared_ptr<media_source_t>;
        using media_source_wptr_t = std::weak_ptr<media_source_t>;

        class MediaStream
        {
        private:
            media_source_wptr_t m_src;
        public:
            MediaStream(/* args */);
            ~MediaStream();
        };

        using media_stream_t = MediaStream;
        
    } // namespace video
    
} // namespace cfgo


#endif