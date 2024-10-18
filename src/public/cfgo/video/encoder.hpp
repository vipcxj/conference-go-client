#ifndef _CFGO_VIDEO_ENCODER_HPP_
#define _CFGO_VIDEO_ENCODER_HPP_

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
}

#include <fstream>

namespace cfgo
{
    namespace video
    {

#define OUTPUT_CODEC AV_CODEC_ID_H264
// Input pix fmt is set to BGR24
#define OUTPUT_PIX_FMT AV_PIX_FMT_YUV420P
        class Encoder
        {
        private:
            AVCodecContext * m_codec_ctx; // a shortcut to st->codec
            AVPacket m_pkt;
            int m_pts = 0;
            std::ofstream m_file;
            int m_got_output = 0;

        public:
            Encoder(int width, int height, const char *target);
            ~Encoder();
            int write(AVFrame *frame);
        };

        using encoder_t = Encoder;

    } // namespace video

} // namespace cfgo

#endif