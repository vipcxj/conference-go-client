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

        enum class MediaSourceMode
        {
            AUTO = 0,
            PUSH = 1,
            PULL = 2
        };

        using media_source_mode_t = MediaSourceMode;

        class MediaSource
        {
        public:
            virtual ~MediaSource() = 0;
            virtual auto acquire_receiver(int stream_id, const media_codec_t & codec) -> media_receiver_ptr_t = 0;
        };

        using media_source_t = MediaSource;
        using media_source_ptr_t = std::shared_ptr<media_source_t>;
        using media_source_wptr_t = std::weak_ptr<media_source_t>;
        
    } // namespace video
    
} // namespace cfgo


#endif