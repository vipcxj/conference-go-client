#include "cfgo/sink.hpp"
#include "cfgo/video/muxer.hpp"

namespace cfgo
{
    namespace impl
    {
        class BaseSink : public std::enable_shared_from_this<BaseSink>, public cfgo::Sink
        {
        private:
            RTCTracks & m_tracks;
            video::muxer_t m_muxer;
            bool m_started = false;

            void add_stream(AVCodecID codec_id);
        public:
            ~BaseSink() noexcept
            {}

            const RTCTracks & get_rtc_tracks() const {
                return m_tracks;
            }
            void start()
            {}
            void close()
            {}
            auto await() -> asio::awaitable<void>
            {
                co_return;
            }
        };

        void BaseSink::add_stream(AVCodecID codec_id)
        {
            auto sid = m_muxer.add_stream(codec_id);
            auto & stream = m_muxer.get_stream(sid);
            char buf [1024 * 16];
            AVFormatContext * ac[] = {}
            std::string media_type;
            switch (stream.enc->codec_type)
            {
            case AVMEDIA_TYPE_VIDEO:
                media_type = "video";
                break;
            case AVMEDIA_TYPE_AUDIO:
                media_type = "audio";
                break;
            default:
                throw cpptrace::invalid_argument("invalid media type: " + stream.enc->codec_type);
            }
            rtc::Description::Media media();
            
        }
    } // namespace impl
    
} // namespace cfgo
