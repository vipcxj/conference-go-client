#include "cfgo/video/encoder.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/log.hpp"

extern "C"
{
    #include "libavutil/opt.h"
}

namespace cfgo
{
    namespace video
    {
        Encoder::Encoder(std::string file, int width, int height, AVCodecID codec_id, AVPixelFormat format):
            m_width(width), m_height(height), m_codec_id(codec_id), m_format(format), m_file(file)
        {
            auto codec = avcodec_find_encoder(m_codec_id);
            if (!codec)
            {
                throw cpptrace::runtime_error("can't find encoder");
            }
            m_codec_ctx = avcodec_alloc_context3(codec);
            if (!m_codec_ctx)
            {
                throw cpptrace::range_error("could not allocate video codec context");
            }

            /* put sample parameters */
            m_codec_ctx->bit_rate = 400000;
            /* resolution must be a multiple of two */
            m_codec_ctx->width = m_width;
            m_codec_ctx->height = m_height;
            /* frames per second */
            m_codec_ctx->time_base = AVRational {1, 25};
            m_codec_ctx->framerate = AVRational {25, 1};
            /* emit one intra frame every 20 frames
            * check frame pict_type before passing frame
            * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
            * then gop_size is ignored and the output of encoder
            * will always be I frame irrespective to gop_size
            */
            m_codec_ctx->gop_size = 20; /* emit one intra frame every ten frames */
            m_codec_ctx->max_b_frames = 1;
            m_codec_ctx->pix_fmt = m_format;

            if (m_codec_id == AV_CODEC_ID_H264)
            {
                av_opt_set(m_codec_ctx->priv_data, "vprofile", "baseline", 0);
                av_opt_set(m_codec_ctx->priv_data, "tune", "zerolatency", 0);
                av_opt_set(m_codec_ctx->priv_data, "preset", "ultrafast", 0);
            }

            check_av_err(avcodec_open2(m_codec_ctx, codec, NULL), "could not open codec");

            m_pkt = av_packet_alloc();
            if (!m_pkt)
            {
                throw cpptrace::runtime_error("could not allocate the package");
            }

            m_frame = av_frame_alloc();
            if (!m_frame)
            {
                throw cpptrace::runtime_error("could not allocate video frame");
            }
            m_frame->format = m_codec_ctx->pix_fmt;
            m_frame->width = m_codec_ctx->width;
            m_frame->height = m_codec_ctx->height;
            check_av_err(av_frame_get_buffer(m_frame, 0), "could not allocate data for frame, ");
        }

        Encoder::~Encoder()
        {
            flush(NULL, NULL);

            avcodec_free_context(&m_codec_ctx);
            av_frame_free(&m_frame);
            av_packet_free(&m_pkt);
        }

        AVFrame * Encoder::get_frame() noexcept
        {
            return m_frame;
        }

        void Encoder::encode(AVFrame * frame, AVFormatContext * fmt_ctx, AVStream * st)
        {
            check_av_err(av_frame_make_writable(frame), "could not make frame writable, ");
            frame->pts = m_pts ++;
            check_av_err(avcodec_send_frame(m_codec_ctx, frame), "could not sending a frame for encoding, ");
            int err;
            do
            {
                err = avcodec_receive_packet(m_codec_ctx, m_pkt);
                if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
                {
                    return;
                }
                check_av_err(err, "error during encoding, ");
                if (fmt_ctx && st)
                {
                    av_packet_rescale_ts(m_pkt, m_codec_ctx->time_base, st->time_base);
                    check_av_err(av_interleaved_write_frame(fmt_ctx, m_pkt), "could not writing output packet, ");
                    /* pkt is now blank (av_interleaved_write_frame() takes ownership of
                    * its contents and resets pkt), so that no unreferencing is necessary.
                    * This would be different if one used av_write_frame().
                    * */
                }
                else
                {
                    av_packet_unref(m_pkt);
                }
            } while (true);
        }

        void Encoder::write(AVFormatContext * fmt_ctx, AVStream * st)
        {
            encode(m_frame, fmt_ctx, st);
        }

        void Encoder::flush(AVFormatContext * fmt_ctx, AVStream * st)
        {
            encode(NULL, fmt_ctx, st);
        }
    } // namespace video

} // namespace cfgo
