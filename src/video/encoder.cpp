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
        Encoder::Encoder(int width, int height, const char *target): m_file(target)
        {
            auto codec = avcodec_find_encoder(OUTPUT_CODEC);
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
            m_codec_ctx->width = 640;
            m_codec_ctx->height = 480;
            /* frames per second */
            m_codec_ctx->time_base = (AVRational){1,25};
            m_codec_ctx->gop_size = 20; /* emit one intra frame every ten frames */
            m_codec_ctx->max_b_frames = 1;
            m_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

            if (OUTPUT_CODEC == AV_CODEC_ID_H264)
            {
                av_opt_set(m_codec_ctx->priv_data, "vprofile", "baseline", 0);
                av_opt_set(m_codec_ctx->priv_data, "tune", "zerolatency", 0);
                av_opt_set(m_codec_ctx->priv_data, "preset", "ultrafast", 0);
            }

            check_av_err(avcodec_open2(m_codec_ctx, codec, NULL), "could not open codec");
            
            av_init_packet(&m_pkt);
            m_pkt.data = NULL;
            m_pkt.size = 0;
        }

        Encoder::~Encoder()
        {
            for (m_got_output = 1; m_got_output; ++m_pts)
            {
                auto err = avcodec_send_frame(m_codec_ctx, NULL);
                if (err < 0)
                {
                    CFGO_ERROR("failed to send frame, {}", av_err2str(err));
                    break;
                }
                
                err = avcodec_receive_packet(m_codec_ctx, NULL);
                if (err == AVERROR_EOF)
                {
                    break;
                }
            }
        }

        // Return 1 if a packet was written. 0 if nothing was done.
        //  return error_code < 0 if there was an error.

        int Encoder::write(AVFrame *frame)
        {

        }
    } // namespace video

} // namespace cfgo
