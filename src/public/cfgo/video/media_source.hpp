#ifndef _CFGO_VIDEO_MEDIA_SOURCE_HPP_
#define _CFGO_VIDEO_MEDIA_SOURCE_HPP_

extern "C" {
    #include "libavformat/avformat.h"
}

#include <string>
#include <memory>
#include <unordered_map>
#include "cfgo/async.hpp"
#include "cfgo/smart_list.hpp"
#include "cfgo/video/media_profile.hpp"

namespace cfgo
{
    namespace video
    {
        struct MediaCodecAndProfile
        {
            AVCodecID codec_id;
            media_profile_t profile;

            bool operator==(const MediaCodecAndProfile &) const = default;
        };

        using media_codec_and_profile_t = MediaCodecAndProfile;
    }
} // namespace cfgo

template<>
struct std::hash<cfgo::video::MediaCodecAndProfile>
{
    std::size_t operator()(const cfgo::video::MediaCodecAndProfile & k) const
    {
        using std::hash;
        return (hash<int>()(static_cast<int>(k.codec_id)) 
            ^ (hash<cfgo::video::media_profile_t>()(k.profile) << 1)) >> 1;
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
#if FF_API_AVIO_WRITE_NONCONST
        using raw_buffer_el_t = uint8_t;
#else
        using raw_buffer_el_t = const uint8_t;
#endif
        using raw_buffer_t = raw_buffer_el_t *;
        using media_packet_t = std::vector<std::byte>;
        using media_packet_ptr_t = std::shared_ptr<media_packet_t>;

        class MediaReceiver
        {
        public:
            virtual auto request_pkt(close_chan closer) -> asio::awaitable<media_packet_ptr_t> = 0;
        };

        using media_receiver_t = MediaReceiver;
        using media_receiver_wptr_t = std::weak_ptr<media_receiver_t>;
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
            virtual ~MediaSource() {};
            virtual unsigned int nb_streams() const = 0;
            virtual AVMediaType stream_media_type(int i) const = 0;
            virtual auto acquire_receiver(int stream_id, const media_codec_and_profile_t & codec) -> media_receiver_ptr_t = 0;
        };

        using media_source_t = MediaSource;
        using media_source_ptr_t = std::shared_ptr<media_source_t>;
        using media_source_wptr_t = std::weak_ptr<media_source_t>;

        media_source_ptr_t make_media_source(asio::any_io_executor executor, MediaSourceType source_type, const std::string & url_or_name, media_source_mode_t mode = media_source_mode_t::AUTO, close_chan closer = nullptr);
    } // namespace video
    
} // namespace cfgo


#endif