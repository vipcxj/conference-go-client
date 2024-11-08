#include "cfgo/video/media_source.hpp"
#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/defer.hpp"

#include <array>

namespace cfgo
{
    namespace video
    {

        namespace impl
        {
            static AVFrame *alloc_audio_frame(
                enum AVSampleFormat sample_fmt,
                const AVChannelLayout *channel_layout,
                int sample_rate, int nb_samples
            )
            {
                DEFERS_WHEN_FAIL(cleaner);
                AVFrame *frame = av_frame_alloc();
                if (!frame) {
                    throw cpptrace::runtime_error("could not allocate an audio frame");
                }
                cleaner.add_defer([&frame]() {
                    av_frame_free(&frame);
                });

                frame->format = sample_fmt;
                check_av_err(av_channel_layout_copy(&frame->ch_layout, channel_layout), "could not copy channel layout of codec context to frame, ");
                frame->sample_rate = sample_rate;
                frame->nb_samples = nb_samples;

                if (nb_samples) {
                    check_av_err(av_frame_get_buffer(frame, 0), "could not allocate data for frame, ");
                }

                cleaner.success();
                return frame;
            }

            static AVFrame *alloc_video_frame(enum AVPixelFormat pix_fmt, int width, int height)
            {
                DEFERS_WHEN_FAIL(cleaner);
                AVFrame *frame = av_frame_alloc();
                if (!frame)
                    throw cpptrace::runtime_error("could not allocate an video frame");
                cleaner.add_defer([&frame]() {
                    av_frame_free(&frame);
                });
                frame->format = pix_fmt;
                frame->width  = width;
                frame->height = height;

                check_av_err(av_frame_get_buffer(frame, 0), "could not allocate data for frame, ");
                cleaner.success();
                return frame;
            }

            AVFrame * allocate_frame(const AVStream * stream)
            {
                auto par = stream->codecpar;
                auto codec = avcodec_find_encoder(par->codec_id);
                switch (stream->codecpar->codec_type)
                {
                case AVMEDIA_TYPE_VIDEO:
                {
                    return alloc_video_frame((AVPixelFormat) par->format, par->width, par->height);
                }
                case AVMEDIA_TYPE_AUDIO:
                {
                    int nb_samples;
                    if (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
                    {
                        nb_samples = 10000;
                    }
                    else
                    {
                        nb_samples = par->frame_size;
                    }
                    return alloc_audio_frame((AVSampleFormat) par->format, &par->ch_layout, par->sample_rate, nb_samples);
                }
                default:
                    throw cpptrace::runtime_error(fmt::format("could not allocate frame from stream with media type {}", (int) stream->codecpar->codec_type));
                }
            }

            template<int size>
            struct AVFramePool
            {
                std::array<AVFrame *, size> pool;
                int head;
                int count;
                std::mutex mux;

                ~AVFramePool()
                {
                    for (int i = 0; i < count; i++)
                    {
                        auto & frame = pool[(head + i) % size];
                        assert(frame);
                        av_frame_free(&frame);
                    }
                }

                auto borrow_frame() -> AVFrame *
                {
                    std::lock_guard g(mux);
                    if (!count)
                    {
                        return nullptr;
                    }
                    auto frame = pool[head];
                    head = (head + 1) % size;
                    --count;
                    return frame;
                }

                bool return_back_frame(AVFrame * frame)
                {
                    assert(frame);
                    std::lock_guard g(mux);
                    if (count == size)
                    {
                        return false;
                    }
                    pool[(head + count++) % size] = frame;
                    return true;
                }
            };

            template<int size>
            using av_frame_pool_t = AVFramePool<size>;

            struct MediaStream
            {
                media_source_wptr_t m_source;
                AVStream * m_stream;
                std::unordered_map<media_codec_t, media_receiver_ptr_t> m_receivers;
                asiochan::unblocked_channel<av_frame_ptr_t> m_frame_ch;
                av_frame_pool_t<16> m_frame_pool;
                auto acquire_receiver(const media_codec_t & codec) -> media_receiver_ptr_t;

                auto request_frame() -> av_frame_ptr_t
                {
                    auto raw_frame = m_frame_pool.borrow_frame();
                    if (!raw_frame)
                    {
                        raw_frame = allocate_frame(m_stream);
                    }
                    return av_frame_ptr_t(raw_frame, [src = m_source, this](AVFrame * ptr) {
                        if (auto source = src.lock())
                        {
                            if (!m_frame_pool.return_back_frame(ptr))
                            {
                                auto ptr_c = ptr;
                                av_frame_free(&ptr_c);
                            }
                        }
                        else
                        {
                            auto ptr_c = ptr;
                            av_frame_free(&ptr_c);
                        }
                    });
                }
            };

            using media_stream_t = MediaStream;

            class MediaSource : cfgo::video::MediaSource
            {
            private:
                AVFormatContext * m_fmt_ctx = nullptr;
                media_source_type_t m_source_type;
                std::string m_url_or_name;
                media_source_mode_t m_mode;
                std::vector<media_stream_t> m_streams;
            public:
                MediaSource(media_source_type_t source_type, const std::string & url_or_name, media_source_mode_t mode = media_source_mode_t::AUTO);
                ~MediaSource();

                auto acquire_receiver(int stream_id, const media_codec_t & codec) -> media_receiver_ptr_t;
            };
            

            MediaSource::MediaSource(MediaSourceType source_type, const std::string & url_or_name, media_source_mode_t mode)
            : m_source_type(source_type), m_url_or_name(url_or_name), m_mode(mode)
            {
                const AVInputFormat * ifmt;
                std::string name;
                switch (m_source_type)
                {
                case MediaSourceType::DEVICE:
                {
                    auto device_list = list_devices();
                    ifmt = device_list.ifmt;
                    if (!ifmt)
                    {
                        throw cpptrace::runtime_error("could not find any input device");
                    }
                    const DeviceInfo * device;
                    if (m_url_or_name.empty())
                    {
                        device = device_list.select();
                    }
                    else
                    {
                        device = device_list.select(m_url_or_name);
                    }
                    if (!device)
                    {
                        if (m_url_or_name.empty())
                        {
                            throw cpptrace::invalid_argument("could not find any input device");
                        }
                        else
                        {
                            throw cpptrace::invalid_argument(fmt::format("could not find the input device with name {}", m_url_or_name));
                        }
                    }
                    name = device->name;
                    if (m_mode == media_source_mode_t::AUTO)
                    {
                        m_mode = media_source_mode_t::PUSH;
                    }
                    break;
                }
                case MediaSourceType::FILE:
                {
                    ifmt = nullptr;
                    name = m_url_or_name;
                    if (m_mode == media_source_mode_t::AUTO)
                    {
                        m_mode = media_source_mode_t::PULL;
                    }
                    break;
                }
                default:
                    throw cpptrace::invalid_argument(fmt::format("invalid media source type {}", (int) m_source_type));
                }
                DEFERS_WHEN_FAIL(cleaner);
                check_av_err(avformat_open_input(&m_fmt_ctx, name.c_str(), ifmt, nullptr), fmt::format("could not open input{}", name.empty() ? ", " : (" " + name + ", ")));
                cleaner.add_defer([&]() {
                    avformat_close_input(&m_fmt_ctx);
                });
                check_av_err(avformat_find_stream_info(m_fmt_ctx, nullptr), "could not retrieve input stream information");
                cleaner.success();
            }

            MediaSource::~MediaSource()
            {

            }
        } // namespace impl
        
    } // namespace video
    
} // namespace cfgo
