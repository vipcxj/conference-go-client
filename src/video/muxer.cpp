#include "cfgo/video/muxer.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/fmt.hpp"

#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

namespace cfgo
{
    namespace video
    {
        int Muxer::add_stream(enum AVCodecID codec_id)
        {
            AVCodecContext *c;

            /* find the encoder */
            auto codec = avcodec_find_encoder(codec_id);
            if (!codec)
            {
                throw cpptrace::runtime_error(fmt::format("could not find encoder for {}", avcodec_get_name(codec_id)));
            }

            auto id = m_streams.size();
            auto & ost = m_streams.emplace_back();

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
                av_channel_layout_copy(&c->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
                ost.st->time_base = (AVRational){1, c->sample_rate};
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
                break;

            default:
                break;
            }

            /* Some formats want stream headers to be separate. */
            if (m_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            return id;
        }
    } // namespace video

} // namespace cfgo
