#include "cfgo/video/muxer.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/fmt.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/log.hpp"
#include <cstring>

extern "C" {
    #include "libavutil/avassert.h"
    #include "libavutil/opt.h"
    #include "libswscale/swscale.h"
    #include "libswresample/swresample.h"
}

#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */
#define SCALE_FLAGS       SWS_BICUBIC

namespace cfgo
{
    namespace video
    {
        void OutputStream::free() noexcept
        {
            avcodec_free_context(&enc);
            av_frame_free(&frame);
            av_frame_free(&tmp_frame);
            av_packet_free(&tmp_pkt);
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
            swr_free(&swr_ctx);
        }

        bool OutputStream::write_frame(AVFormatContext * fmt_ctx, AVFrame * frame)
        {
            if (frame)
            {
                check_av_err(av_frame_make_writable(frame), "could not make frame writable, ");
                frame->pts = next_pts;
                if (enc->codec->type == AVMEDIA_TYPE_AUDIO)
                {
                    next_pts += frame->nb_samples;
                }
                else
                {
                    ++ next_pts;
                }

                if (sws_ctx)
                {
                    sws_scale(
                        sws_ctx, 
                        (const uint8_t * const *) tmp_frame->data, tmp_frame->linesize, 0, enc->height, 
                        frame->data, frame->linesize
                    );
                }
                else if (swr_ctx)
                {
                    /* convert samples from native format to destination codec format, using the resampler */
                    /* compute destination number of samples */
                    auto dst_nb_samples = swr_get_delay(swr_ctx, enc->sample_rate);
                    av_assert0(dst_nb_samples == frame->nb_samples);

                    /* convert to destination format */
                    check_av_err(swr_convert(
                        swr_ctx,
                        frame->data, dst_nb_samples,
                        (const uint8_t **)frame->data, frame->nb_samples
                    ), "could not convert audio frame, ");

                    frame->pts = av_rescale_q(samples_count, (AVRational){1, enc->sample_rate}, enc->time_base);
                    samples_count += dst_nb_samples;
                }
            }
            check_av_err(avcodec_send_frame(enc, frame), "could not sending a frame for encoding, ");
            int err;
            do
            {
                err = avcodec_receive_packet(enc, tmp_pkt);
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
                {
                    break;
                }
                check_av_err(err, "error during encoding, ");
                /* rescale output packet timestamp values from codec to stream timebase */
                av_packet_rescale_ts(tmp_pkt, enc->time_base, st->time_base);
                tmp_pkt->stream_index = st->index;

                /* Write the compressed frame to the output. */
                check_av_err(av_interleaved_write_frame(fmt_ctx, tmp_pkt), "could not write frame to format context, ");

            } while (true);
            return err == AVERROR_EOF;
        }

        Muxer::Muxer(std::string url, const AVOutputFormat * ofmt): m_url(std::move(url)), m_o_fmt(ofmt)
        {
            if (m_url.empty() && !ofmt)
            {
                throw cpptrace::invalid_argument("url and ofmt at least one of which needs to be provided");
            }
            
            DEFERS_WHEN_FAIL(cleaner);
            check_av_err(avformat_alloc_output_context2(
                &m_fmt_ctx, m_o_fmt, NULL, m_url.empty() ? NULL : m_url.c_str()
            ), "could not alloc the format context, ");
            
            cleaner.add_defer([fmt_ctx = m_fmt_ctx]() {
                avformat_free_context(fmt_ctx);
            });
            if (!strcmp(m_fmt_ctx->oformat->name, "rtp"))
            {
                m_fmt_ctx->packet_size = 1480;
            }
            

            if (!(m_fmt_ctx->oformat->flags & AVFMT_NOFILE))
            {
                if (m_url.empty())
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
                else
                {
                    check_av_err(avio_open(&m_fmt_ctx->pb, m_url.c_str(), AVIO_FLAG_WRITE), "could not open " + m_url + ", ");
                    cleaner.add_defer([fmt_ctx = m_fmt_ctx]() {
                        avio_closep(&fmt_ctx->pb);
                    });
                }
            }
            cleaner.success();
        }

        Muxer::~Muxer()
        {
            for (auto & stream : m_streams)
            {
                stream.write_frame(m_fmt_ctx, nullptr);
                stream.free();
            }
            if (m_head)
            {
                check_av_err(av_write_trailer(m_fmt_ctx), "could not write the trailer, ");
            }
            if (!(m_fmt_ctx->oformat->flags & AVFMT_NOFILE))
            {
                if (m_io)
                {
                    av_freep(&m_io->buffer);
                    avio_context_free(&m_io);
                }
                else
                {
                    avio_closep(&m_fmt_ctx->pb);
                }
            }
            avformat_free_context(m_fmt_ctx);
        }

        int Muxer::_write_packet_handle(void * opaque, buffer_t buf, int buf_size)
        {
            Muxer * muxer = static_cast<Muxer *>(opaque);
            std::lock_guard lk(muxer->m_cb_mux);
            try
            {
                for (auto & cb : muxer->m_cbs)
                {
                    cb.second(buf, buf_size);
                }
                return buf_size;
            }
            catch(...)
            {
                CFGO_ERROR(what());
                return -1;
            }
        }

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

        int Muxer::add_stream(enum AVCodecID codec_id)
        {
            AVCodecContext *c;
            DEFERS_WHEN_FAIL(cleaner);

            /* find the encoder */
            auto codec = avcodec_find_encoder(codec_id);
            if (!codec)
            {
                throw cpptrace::runtime_error(fmt::format("could not find encoder for {}", avcodec_get_name(codec_id)));
            }

            auto id = m_streams.size();
            auto & ost = m_streams.emplace_back();
            cleaner.add_defer([&ost]() {
                ost.free();
            });

            ost.tmp_pkt = av_packet_alloc();
            if (!ost.tmp_pkt)
            {
                throw cpptrace::runtime_error("could not allocate AVPacket");
            }

            ost.st = avformat_new_stream(m_fmt_ctx, NULL);
            if (!ost.st)
            {
                throw cpptrace::runtime_error("could not allocate stream");
            }
            ost.st->id = m_fmt_ctx->nb_streams - 1;
            c = avcodec_alloc_context3(codec);
            if (!c)
            {
                throw cpptrace::runtime_error("could not alloc an encoding context");
            }
            ost.enc = c;

            switch (codec->type)
            {
            case AVMEDIA_TYPE_AUDIO:
                c->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
                c->bit_rate = 64000;
                c->sample_rate = 44100;
                if (codec->supported_samplerates)
                {
                    c->sample_rate = codec->supported_samplerates[0];
                    for (auto i = 0; codec->supported_samplerates[i]; i++)
                    {
                        if (codec->supported_samplerates[i] == 44100)
                            c->sample_rate = 44100;
                    }
                }
                c->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                check_av_err(avcodec_open2(c, codec, NULL), "could not open codec");

                ost.st->time_base = (AVRational){1, c->sample_rate};
                ost.t = 0;
                ost.tincr = 2 * M_PI * 110.0 / c->sample_rate;
                /* increment frequency by 110 Hz per second */
                ost.tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
                int nb_samples;
                if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
                {
                    nb_samples = 10000;
                }
                else
                {
                    nb_samples = c->frame_size;
                }
                ost.frame = alloc_audio_frame(c->sample_fmt, &c->ch_layout, c->sample_rate, nb_samples);
                ost.tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &c->ch_layout, c->sample_rate, nb_samples);

                ost.swr_ctx = swr_alloc();
                if (!ost.swr_ctx)
                {
                    throw cpptrace::runtime_error("could not allocate resampler context");
                }
                /* set options */
                av_opt_set_chlayout  (ost.swr_ctx, "in_chlayout",       &c->ch_layout,      0);
                av_opt_set_int       (ost.swr_ctx, "in_sample_rate",     c->sample_rate,    0);
                av_opt_set_sample_fmt(ost.swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
                av_opt_set_chlayout  (ost.swr_ctx, "out_chlayout",      &c->ch_layout,      0);
                av_opt_set_int       (ost.swr_ctx, "out_sample_rate",    c->sample_rate,    0);
                av_opt_set_sample_fmt(ost.swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);
                check_av_err(swr_init(ost.swr_ctx), "failed to initialize the resampling context, ");
                break;

            case AVMEDIA_TYPE_VIDEO:
                c->codec_id = codec_id;

                c->bit_rate = 400000;
                /* Resolution must be a multiple of two. */
                c->width = 352;
                c->height = 288;
                /* timebase: This is the fundamental unit of time (in seconds) in terms
                 * of which frame timestamps are represented. For fixed-fps content,
                 * timebase should be 1/framerate and timestamp increments should be
                 * identical to 1. */
                ost.st->time_base = (AVRational){1, STREAM_FRAME_RATE};
                c->time_base = ost.st->time_base;

                c->gop_size = 12; /* emit one intra frame every twelve frames at most */
                c->pix_fmt = STREAM_PIX_FMT;
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

                check_av_err(avcodec_open2(c, codec, NULL), "could not open codec");
                ost.frame = alloc_video_frame(c->pix_fmt, c->width, c->height);
                if (c->pix_fmt != STREAM_PIX_FMT)
                {
                    ost.tmp_frame = alloc_video_frame(STREAM_PIX_FMT, c->width, c->height);
                    ost.sws_ctx = sws_getContext(
                        c->width, c->height, STREAM_PIX_FMT,
                        c->width, c->height, c->pix_fmt,
                        SCALE_FLAGS, NULL, NULL, NULL
                    );
                    if (ost.sws_ctx)
                    {
                        throw cpptrace::runtime_error("could not initialize the conversion context, ");
                    }
                }
                break;

            default:
                break;
            }

            /* copy the stream parameters to the muxer */
            check_av_err(avcodec_parameters_from_context(ost.st->codecpar, c), "could not copy the stream parameters, ");

            /* Some formats want stream headers to be separate. */
            if (m_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            cleaner.success();
            return id;
        }

        output_stream_t & Muxer::get_stream(int stream_id)
        {
            return m_streams[stream_id];
        }

        AVFrame * Muxer::get_frame(int stream_id)
        {
            return get_stream(stream_id).get_frame();
        }

        AVFormatContext * Muxer::get_format_context()
        {
            return m_fmt_ctx;
        }

        void Muxer::_make_sure_header()
        {
            if (!m_head)
            {
                check_av_err(avformat_write_header(m_fmt_ctx, NULL), "could not write header, ");
                m_head = true;
            }
        }

        bool Muxer::write_frame(int stream_id, AVFrame * frame)
        {
            _make_sure_header();
            return get_stream(stream_id).write_frame(m_fmt_ctx, frame);
        }

        int Muxer::add_callback(std::function<void(buffer_t buf, int buf_size)> cb)
        {
            std::lock_guard lk(m_cb_mux);
            auto cb_id = m_next_cb_id ++;
            m_cbs[cb_id] = std::move(cb);
            return cb_id;
        }
            
        void Muxer::remove_callback(int cb_id)
        {
            std::lock_guard lk(m_cb_mux);
            m_cbs.erase(cb_id);
        }
    } // namespace video

} // namespace cfgo
