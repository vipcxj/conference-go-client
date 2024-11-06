#ifndef _CFGO_VIDEO_MEDIA_SOURCE_HPP_
#define _CFGO_VIDEO_MEDIA_SOURCE_HPP_

extern "C" {
    #include "libavformat/avformat.h"
}

#include <string>
#include <memory>
#include <unordered_map>
#include "cfgo/async.hpp"

namespace cfgo
{
    namespace video
    {
        struct MediaCodec
        {
            AVCodecID codec_id;
            std::string profile;

            bool operator==(const MediaCodec &) const = default;
        };

        using media_codec_t = MediaCodec;
    }
} // namespace cfgo

template<>
struct std::hash<cfgo::video::MediaCodec>
{
    std::size_t operator()(const cfgo::video::MediaCodec & k) const
    {
        using std::hash;
        return (hash<int>()(static_cast<int>(k.codec_id)) ^ (hash<std::string>()(k.profile) << 1)) >> 1;
    }
};


namespace cfgo
{
    namespace video
    {
        enum class MediaSourceType
        {
            DEVICE,
            FILE
        };

        using media_source_type_t = MediaSourceType;

        using av_frame_ptr_t = std::shared_ptr<AVFrame>;

        class MediaReceiver
        {
        private:

        public:
            auto request_frame() -> asio::awaitable<av_frame_ptr_t>;
        };

        using media_receiver_t = MediaReceiver;
        using media_receiver_ptr_t = std::shared_ptr<media_receiver_t>;

        class MediaStream
        {
        private:
            std::unordered_map<media_codec_t, media_receiver_ptr_t> m_receivers;
            mutex m_mux;
        public:
            MediaStream(/* args */);
            ~MediaStream() {};

            auto acquire_receiver(const media_codec_t & codec) -> media_receiver_ptr_t;
        };

        using media_stream_t = MediaStream;

        class MediaSource
        {
        private:
            AVFormatContext * m_fmt_ctx = nullptr;
            media_source_type_t m_source_type;
            std::string m_url_or_name;
            std::vector<media_stream_t> m_streams;
        public:
            MediaSource(media_source_type_t source_type, const std::string & url_or_name);
            ~MediaSource();
            auto loop(close_chan closer) -> asio::awaitable<void>;
            auto streams() const -> const std::vector<media_stream_t> &
            {
                return m_streams;
            }
        };

        using media_source_t = MediaSource;
        using media_source_ptr_t = std::shared_ptr<media_source_t>;
        using media_source_wptr_t = std::weak_ptr<media_source_t>;
        
    } // namespace video
    
} // namespace cfgo


#endif