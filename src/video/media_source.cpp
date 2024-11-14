#include "cfgo/video/media_source.hpp"
#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/opt.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/furcate_stream.hpp"

#include <array>

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libswscale/swscale.h"
    #include "libswresample/swresample.h"
}

#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS       SWS_BICUBIC

namespace cfgo
{
    namespace video
    {
        namespace impl
        {
            struct stream_and_media_codec_t
            {
                int stream_idx;
                media_codec_t codec;

                bool operator==(const stream_and_media_codec_t &) const = default;
            }; 
        } // namespace impl
    }
} // namespace cfgo

template<>
struct std::hash<cfgo::video::impl::stream_and_media_codec_t>
{
    std::size_t operator()(const cfgo::video::impl::stream_and_media_codec_t & k) const
    {
        using std::hash;
        return (hash<int>()(static_cast<int>(k.stream_idx)) ^ (hash<cfgo::video::media_codec_t>()(k.codec) << 1)) >> 1;
    }
};

namespace cfgo
{
    namespace video
    {

        namespace impl
        {
            using namespace std::chrono_literals;

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

            template<typename T>
            struct AVCachePool
            {
                using cache_t = std::vector<T>;
                using size_t = cache_t::size_type;
                size_t size;
                cache_t cache;
                int head = 0;
                int count = 0;
                std::mutex mux;
                std::function<void(T &)> deleter;

                AVCachePool(size_t size, std::function<void(T &)> deleter): cache(size), deleter(std::move(deleter)) {}

                ~AVCachePool()
                {
                    for (int i = 0; i < count; i++)
                    {
                        auto & value = cache[(head + i) % size];
                        if (deleter)
                        {
                            deleter(value);
                        }
                    }
                }

                auto borrow() -> T
                {
                    std::lock_guard g(mux);
                    if (!count)
                    {
                        return nullptr;
                    }
                    auto & value = cache[head];
                    head = (head + 1) % size;
                    --count;
                    return std::move(value);
                }

                bool return_back(T value)
                {
                    std::lock_guard g(mux);
                    if (count == size)
                    {
                        return false;
                    }
                    cache[(head + count++) % size] = std::move(value);
                    return true;
                }
            };

            using av_frame_pool_t = AVCachePool<AVFrame *>;
            using av_packet_pool_t = AVCachePool<AVPacket *>;

            using frame_furcate_stream_t = furcate_stream_t<av_frame_ptr_t, 3>;
            using pkt_furcate_stream_t = furcate_stream_t<media_packet_ptr_t, 3>;

            struct MediaReceiver : public cfgo::video::media_receiver_t, public std::enable_shared_from_this<MediaReceiver>
            {

            };

            struct MediaStream;

            struct MediaSubStream
            {
                using branch_node_t = frame_furcate_stream_t::branch_node_t;
                MediaStream * m_stream;                
                media_codec_t m_media_codec;
                branch_node_t m_branch;
                asio::strand<asio::any_io_executor> m_strand;
                pkt_furcate_stream_t::ptr_t m_channel;
                asiochan::unblocked_channel<std::exception_ptr> m_err_ch;
                
                AVFormatContext * m_fmt_ctx;
                AVStream * m_av_stream;
                AVCodecContext * m_enc_ctx;
                AVIOContext * m_io;
                /* pts of the next frame that will be generated */
                int64_t m_next_pts = 0;
                int m_samples_count = 0;

                AVFrame * m_tmp_frame = nullptr;
                AVPacket * m_tmp_pkt = nullptr;

                float m_t = 0.0f, m_tincr = 0.0f, m_tincr2 = 0.0f;

                struct SwsContext * m_sws_ctx = nullptr;
                struct SwrContext * m_swr_ctx = nullptr;
                /*
                  not used yet.
                */
                opt_t m_opts;

                MediaSubStream(MediaStream * stream, const media_codec_t & media_codec, av_packet_pool_t::size_t pkt_pool_size)
                : m_stream(stream), 
                  m_media_codec(media_codec),
                  m_branch(m_stream->m_source->channel()->create_branch()),
                  m_strand(asio::make_strand(stream->m_source->executor())), 
                  m_channel(pkt_furcate_stream_t::create(m_strand, 50ms))
                {
                    DEFERS_WHEN_FAIL(cleaner);
                    check_av_err(
                        avformat_alloc_output_context2(&m_fmt_ctx, m_media_codec.ofmt, nullptr, nullptr), 
                        fmt::format("could not alloc the format context with output format {}, ", m_media_codec.ofmt->name)
                    );
                    cleaner.add_defer([this]() {
                        avformat_free_context(m_fmt_ctx);
                    });
                    if (!strcmp(m_fmt_ctx->oformat->name, "rtp"))
                    {
                        m_fmt_ctx->packet_size = 1472;
                    }
                    {
                        size_t buffer_size = 4096;
                        uint8_t * buffer = static_cast<uint8_t *>(av_malloc(buffer_size));
                        if (!buffer)
                        {
                            throw cpptrace::runtime_error("could not allocate the buffer of the avio");
                        }
                        m_io = avio_alloc_context(buffer, buffer_size, 1, this, nullptr, &_write_packet_handle, nullptr);
                        if (!m_io)
                        {
                            av_freep(buffer);
                            throw cpptrace::runtime_error("could not allocate the av io context");
                        }
                        else
                        {
                            cleaner.add_defer([this]() {
                                av_freep(m_io->buffer);
                                avio_context_free(&m_io);
                            });
                        }
                        m_fmt_ctx->pb = m_io;
                    }
                    setup_stream(cleaner);
                    asio::co_spawn(m_strand, [weak_src = m_stream->m_source->weak_from_this(), this]() -> asio::awaitable<void> {
                        return loop(std::move(weak_src), this);
                    }, asio::detached);
                    cleaner.success();
                }

                void setup_stream(defers_when_fail & cleaner)
                {
                    AVCodecContext *c;

                    /* find the encoder */
                    auto codec = avcodec_find_encoder(m_media_codec.codec_id);
                    if (!codec)
                    {
                        throw cpptrace::runtime_error(fmt::format("could not find encoder for {}", avcodec_get_name(m_media_codec.codec_id)));
                    }

                    m_tmp_pkt = av_packet_alloc();
                    if (!m_tmp_pkt)
                    {
                        throw cpptrace::runtime_error("could not allocate AVPacket");
                    }
                    cleaner.add_defer([this]() {
                        av_packet_free(&m_tmp_pkt);
                    });

                    m_av_stream = avformat_new_stream(m_fmt_ctx, NULL);
                    if (!m_av_stream)
                    {
                        throw cpptrace::runtime_error("could not allocate stream");
                    }
                    m_av_stream->id = m_fmt_ctx->nb_streams - 1;
                    m_enc_ctx = avcodec_alloc_context3(codec);
                    if (!m_enc_ctx)
                    {
                        throw cpptrace::runtime_error("could not alloc an encoding context");
                    }
                    cleaner.add_defer([this]() {
                        avcodec_free_context(&m_enc_ctx);
                    });
     
                    switch (codec->type)
                    {
                    case AVMEDIA_TYPE_AUDIO:
                    {
                        const enum AVSampleFormat * sample_fmts = nullptr;
                        auto ret = avcodec_get_supported_config(c, nullptr, AVCodecConfig::AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **) &sample_fmts, nullptr);
                        c->sample_fmt = (ret >= 0 && sample_fmts) ? sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
                        c->bit_rate = 64000;
                        c->sample_rate = 44100;
                        const int * supported_samplerates = nullptr;
                        ret = avcodec_get_supported_config(c, nullptr, AVCodecConfig::AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void **) &supported_samplerates, nullptr);
                        if (ret >= 0 && supported_samplerates)
                        {
                            c->sample_rate = supported_samplerates[0];
                            for (auto i = 0; supported_samplerates[i]; i++)
                            {
                                if (supported_samplerates[i] == 44100)
                                    c->sample_rate = 44100;
                            }
                        }
                        c->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                        auto opt = create_av_opt(m_opts);
                        check_av_err(avcodec_open2(c, codec, &opt.get()), "could not open codec");

                        m_av_stream->time_base = {1, c->sample_rate};
                        m_t = 0;
                        m_tincr = 2 * M_PI * 110.0 / c->sample_rate;
                        /* increment frequency by 110 Hz per second */
                        m_tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
                        int nb_samples;
                        if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
                        {
                            nb_samples = 10000;
                        }
                        else
                        {
                            nb_samples = c->frame_size;
                        }
                        if (c->sample_fmt != AV_SAMPLE_FMT_S16)
                        {
                            m_tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout, c->sample_rate, nb_samples);
                            assert(m_tmp_frame);
                            cleaner.add_defer([this]() {
                                av_frame_free(&m_tmp_frame);
                            });
                            m_swr_ctx = swr_alloc();
                            if (!m_swr_ctx)
                            {
                                throw cpptrace::runtime_error("could not allocate resampler context");
                            }
                            cleaner.add_defer([this]() {
                                swr_free(&m_swr_ctx);
                            });
                            /* set options */
                            av_opt_set_chlayout  (m_swr_ctx, "in_chlayout",       &c->ch_layout,      0);
                            av_opt_set_int       (m_swr_ctx, "in_sample_rate",     c->sample_rate,    0);
                            av_opt_set_sample_fmt(m_swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
                            av_opt_set_chlayout  (m_swr_ctx, "out_chlayout",      &c->ch_layout,      0);
                            av_opt_set_int       (m_swr_ctx, "out_sample_rate",    c->sample_rate,    0);
                            av_opt_set_sample_fmt(m_swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);
                            check_av_err(swr_init(m_swr_ctx), "failed to initialize the resampling context, ");
                        }
                        break;
                    }
                    case AVMEDIA_TYPE_VIDEO:
                    {
                        c->codec_id = codec->id;

                        c->bit_rate = 400000;
                        /* Resolution must be a multiple of two. */
                        c->width = 352;
                        c->height = 288;
                        /* timebase: This is the fundamental unit of time (in seconds) in terms
                        * of which frame timestamps are represented. For fixed-fps content,
                        * timebase should be 1/framerate and timestamp increments should be
                        * identical to 1. */
                        m_av_stream->time_base = {1, m_media_codec.fps};
                        c->time_base = m_av_stream->time_base;

                        c->gop_size = 12; /* emit one intra frame every twelve frames at most */
                        const enum AVPixelFormat * pix_fmts = nullptr;
                        auto ret = avcodec_get_supported_config(c, nullptr, AVCodecConfig::AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void **) &pix_fmts, nullptr);
                        if (ret >= 0 && pix_fmts)
                        {
                            c->pix_fmt = pix_fmts[0];
                            for (int i = 0; pix_fmts[i]; i++)
                            {
                                if (pix_fmts[i] == STREAM_PIX_FMT)
                                {
                                    c->pix_fmt = pix_fmts[i];
                                    break;
                                }
                            }
                        }
                        else
                        {
                            c->pix_fmt = STREAM_PIX_FMT;
                        }
                        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO)
                        {
                            /* just for testing, we also add B-frames */
                            c->max_b_frames = 2;
                        }
                        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO)
                        {
                            /* Needed to avoid using macroblocks in which some coeffs overflow.
                            * This does not happen with normal video, it just happens here as
                            * the motion of the chroma plane does not match the luma plane. */
                            c->mb_decision = 2;
                        }
                        auto opt = create_av_opt(m_opts);
                        check_av_err(avcodec_open2(c, codec, &opt.get()), "could not open codec");
                        if (c->pix_fmt != STREAM_PIX_FMT)
                        {
                            m_tmp_frame = alloc_video_frame(STREAM_PIX_FMT, c->width, c->height);
                            assert(m_tmp_frame);
                            cleaner.add_defer([this]() {
                                av_frame_free(&m_tmp_frame);
                            });
                            m_sws_ctx = sws_getContext(
                                c->width, c->height, STREAM_PIX_FMT,
                                c->width, c->height, c->pix_fmt,
                                SCALE_FLAGS, NULL, NULL, NULL
                            );
                            if (!m_sws_ctx)
                            {
                                throw cpptrace::runtime_error("could not initialize the conversion context, ");
                            }
                            cleaner.add_defer([this]() {
                                sws_freeContext(m_sws_ctx);
                                m_sws_ctx = nullptr;
                            });
                        }
                        break;
                    }
                    default:
                        break;
                    }

                    /* copy the stream parameters to the muxer */
                    check_av_err(avcodec_parameters_from_context(m_av_stream->codecpar, c), "could not copy the stream parameters, ");

                    /* Some formats want stream headers to be separate. */
                    if (m_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                }

                static int _write_packet_handle(void * opaque, raw_buffer_t buf, int buf_size)
                {
                    auto self = static_cast<MediaSubStream *>(opaque);
                    auto buf_ptr = std::make_shared<media_packet_t>(buf, buf + buf_size);
                    asio::co_spawn(self->m_strand, [weak_src = self->m_stream->m_source->weak_from_this(), channel = self->m_channel, buf_ptr]() -> asio::awaitable<void> {
                        if (auto src = weak_src.lock())
                        {
                            /* code */
                        }
                        else
                        {
                            co_await channel->send_async(nullptr);
                        }
                    }, asio::detached);
                }

                static auto loop(std::weak_ptr<MediaSource> weak_src, MediaSubStream * self) -> asio::awaitable<void>
                {
                    try
                    {
                        do
                        {
                            if (auto src = weak_src.lock())
                            {
                                auto opt_frame = co_await self->m_branch->value().receive_async(src->m_closer);
                                if (!opt_frame)
                                {
                                    break;
                                }
                                auto frame = *opt_frame;
                                if (frame)
                                {
                                    check_av_err(av_frame_make_writable(frame.get()), "could not make frame writable, ");
                                    frame->pts = self->m_next_pts;
                                    if (self->m_enc_ctx->codec->type == AVMEDIA_TYPE_AUDIO)
                                    {
                                        self->m_next_pts += frame->nb_samples;
                                    }
                                    else
                                    {
                                        ++ self->m_next_pts;
                                    }
                                    if (self->m_sws_ctx)
                                    {
                                        assert(self->m_tmp_frame);
                                        sws_scale(
                                            self->m_sws_ctx, 
                                            (const uint8_t * const *) self->m_tmp_frame->data, self->m_tmp_frame->linesize, 0, self->m_enc_ctx->height, 
                                            frame->data, frame->linesize
                                        );
                                    }
                                    else if (self->m_swr_ctx)
                                    {
                                        assert(self->m_tmp_frame);
                                        /* convert samples from native format to destination codec format, using the resampler */
                                        /* compute destination number of samples */
                                        auto dst_nb_samples = swr_get_delay(self->m_swr_ctx, self->m_enc_ctx->sample_rate);
                                        assert(dst_nb_samples == frame->nb_samples);

                                        /* convert to destination format */
                                        check_av_err(swr_convert(
                                            self->m_swr_ctx,
                                            frame->data, dst_nb_samples,
                                            (const uint8_t **)frame->data, frame->nb_samples
                                        ), "could not convert audio frame, ");

                                        frame->pts = av_rescale_q(self->m_samples_count, AVRational {1, self->m_enc_ctx->sample_rate}, self->m_enc_ctx->time_base);
                                        self->m_samples_count += dst_nb_samples;
                                    }
                                }
                                check_av_err(avcodec_send_frame(self->m_enc_ctx, frame.get()), "could not sending a frame for encoding, ");
                                do
                                {
                                    auto err = avcodec_receive_packet(self->m_enc_ctx, self->m_tmp_pkt);
                                    if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
                                    {
                                        break;
                                    }
                                    check_av_err(err, "error during encoding, ");
                                    /* rescale output packet timestamp values from codec to stream timebase */
                                    av_packet_rescale_ts(self->m_tmp_pkt, self->m_enc_ctx->time_base, self->m_av_stream->time_base);
                                    self->m_tmp_pkt->stream_index = self->m_av_stream->index;

                                    /* Write the compressed frame to the output. */
                                    check_av_err(av_interleaved_write_frame(self->m_fmt_ctx, self->m_tmp_pkt), "could not write frame to format context, ");

                                } while (true);
                            }
                        } while (true);
                        if (auto src = weak_src.lock())
                        {
                            self->m_err_ch.write(nullptr);
                        }
                    }
                    catch(const CancelError &) {
                        if (auto src = weak_src.lock())
                        {
                            self->m_err_ch.write(nullptr);
                        }
                    }
                    catch(...)
                    {
                        if (auto src = weak_src.lock())
                        {
                            self->m_err_ch.write(std::current_exception());
                        }
                    }
                }
            };

            class MediaSource;

            struct MediaStream
            {
                MediaSource * m_source;
                AVStream * m_stream;
                av_frame_pool_t m_frame_pool;
                const AVCodec * m_dec;
                AVCodecContext * m_dec_ctx;
                std::unordered_map<media_codec_t, MediaSubStream> m_sub_streams;

                MediaStream(MediaSource * source, AVStream * stream, av_frame_pool_t::size_t frame_pool_size)
                : m_source(source), m_stream(stream), m_frame_pool(frame_pool_size, [](AVFrame *& frame) { av_frame_free(&frame); })
                {
                    assert(m_stream);
                    assert(m_stream->codecpar);
                    m_dec = avcodec_find_decoder(m_stream->codecpar->codec_id);
                    if (!m_dec)
                    {
                        throw cpptrace::invalid_argument(fmt::format("could not find codec from id {}", (int) m_stream->codecpar->codec_id));
                    }
                    DEFERS_WHEN_FAIL(cleaner);
                    m_dec_ctx = avcodec_alloc_context3(m_dec);
                    if (!m_dec_ctx)
                    {
                        throw cpptrace::runtime_error(fmt::format("could not allocate the codec context for codec {}", m_dec->name));
                    }
                    cleaner.add_defer([this]() {
                        avcodec_free_context(&m_dec_ctx);
                    });
                    check_av_err(avcodec_parameters_to_context(m_dec_ctx, m_stream->codecpar), fmt::format("could not copy codec parameters to decoder context, the codec is {}, ", m_dec->name));
                    check_av_err(avcodec_open2(m_dec_ctx, m_dec, nullptr), fmt::format("could not open the codec {}, ", m_dec->name));
                    cleaner.success();
                }

                ~MediaStream()
                {
                    avcodec_free_context(&m_dec_ctx);
                }

                auto request_frame() -> av_frame_ptr_t
                {
                    auto raw_frame = m_frame_pool.borrow();
                    if (!raw_frame)
                    {
                        raw_frame = allocate_frame(m_stream);
                    }
                    return av_frame_ptr_t(raw_frame, [src = m_source->weak_from_this(), this](AVFrame *& ptr) {
                        if (auto source = src.lock())
                        {
                            if (!m_frame_pool.return_back(ptr))
                            {
                                av_frame_free(&ptr);
                            }
                        }
                        else
                        {
                            av_frame_free(&ptr);
                        }
                    });
                }

                bool decode(const AVPacket * pkt)
                {
                    int ret = 0;
                    check_av_err(avcodec_send_packet(m_dec_ctx, pkt), fmt::format("could not submit a packet for decoding, "));
                    do
                    {
                        auto frame = request_frame();
                        ret = avcodec_receive_frame(m_dec_ctx, frame.get());
                        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                        {
                            return ret != AVERROR_EOF;
                        }
                        check_av_err(ret, "Decoding failed, ");

                        
                    } while (ret >= 0);
                    
                }
            };

            using media_stream_t = MediaStream;

            struct MediaReceiver : public cfgo::video::MediaReceiver, public std::enable_shared_from_this<MediaReceiver>
            {
                media_source_wptr_t m_source;
                int m_stream_idx;
                media_codec_t m_codec;
            };

            class MediaSource : public cfgo::video::MediaSource, public std::enable_shared_from_this<MediaSource>
            {
                using ptr_t = std::shared_ptr<MediaSource>;
                using wptr_t = std::weak_ptr<MediaSource>;
                using channel_t = furcate_stream_t<av_frame_ptr_t, 3>;
                using channel_ptr_t = channel_t::ptr_t;
                using receiver_key_t = stream_and_media_codec_t;
                using receiver_map_t = std::unordered_map<receiver_key_t, media_receiver_ptr_t>;
            private:
                AVFormatContext * m_fmt_ctx = nullptr;
                asio::any_io_executor m_executor;
                media_source_type_t m_source_type;
                std::string m_url_or_name;
                media_source_mode_t m_mode;
                std::vector<MediaStream> m_streams;
                channel_ptr_t m_channel;
                receiver_map_t m_receivers;
                std::mutex m_setup_mux;
                bool m_has_setup = false;
                close_chan m_closer;
                std::exception_ptr m_background_err;

                void setup();
                static void loop(wptr_t weak_self);
            public:
                MediaSource(asio::any_io_executor executor, media_source_type_t source_type, const std::string & url_or_name, media_source_mode_t mode = media_source_mode_t::AUTO, close_chan closer = nullptr);
                ~MediaSource();

                asio::any_io_executor & executor()
                {
                    return m_executor;
                }
                const asio::any_io_executor & executor() const
                {
                    return m_executor;
                }
                channel_ptr_t & channel()
                {
                    return m_channel;
                }
                const channel_ptr_t & channel() const
                {
                    return m_channel;
                }

                unsigned int nb_streams() override
                {
                    return m_fmt_ctx->nb_streams;
                }
                auto acquire_receiver(int stream_id, const media_codec_t & codec) -> media_receiver_ptr_t override;

                friend struct MediaSubStream;
            };
            

            MediaSource::MediaSource(asio::any_io_executor executor, MediaSourceType source_type, const std::string & url_or_name, media_source_mode_t mode, close_chan closer)
            : m_executor(executor), m_source_type(source_type), m_url_or_name(url_or_name), m_mode(mode), m_channel(channel_t::create(m_executor, 50ms)), m_closer(closer.create_child())
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

            void MediaSource::setup()
            {
                std::lock_guard g(m_setup_mux);
                if (m_has_setup)
                {
                    return;
                }
                m_has_setup = true;
                for (int i = 0; i < m_fmt_ctx->nb_streams; i++)
                {
                    m_streams.emplace_back(weak_from_this(), m_fmt_ctx->streams[i]);
                }
                std::thread t([weak_self = weak_from_this()]() {
                    loop(std::move(weak_self));
                });
                t.detach();
            }

            void MediaSource::loop(wptr_t weak_self)
            {
                try
                {
                    AVPacket * pkt = av_packet_alloc();
                    if (!pkt)
                    {
                        throw cpptrace::runtime_error("could not allocate the packet");
                    }
                    DEFER({
                        av_packet_free(&pkt);
                    });
                    do
                    {
                        if (auto self = weak_self.lock())
                        {
                            cfgo::video::check_av_err(av_read_frame(self->m_fmt_ctx, pkt), "could not read pkt, ");
                            auto & stream = self->m_streams[pkt->stream_index];
                            if (!stream.decode(pkt))
                            {
                                break;
                            }
                        }
                    } while (true);
                }
                catch(const CancelError &) {}
                catch(...)
                {
                    if (auto self = weak_self.lock())
                    {
                        self->m_background_err = std::current_exception();
                        self->m_closer.close(what(self->m_background_err));
                    }
                }
            }

            auto MediaSource::acquire_receiver(int stream_id, const media_codec_t & codec) -> media_receiver_ptr_t
            {

            }
        } // namespace impl
        
    } // namespace video
    
} // namespace cfgo
