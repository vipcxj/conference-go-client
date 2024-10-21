#ifndef _CFGO_VIDEO_ENCODER_HPP_
#define _CFGO_VIDEO_ENCODER_HPP_

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
}

#include <string>
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
            int m_width;
            int m_height;
            AVCodecID m_codec_id;
            AVPixelFormat m_format;
            AVCodecContext * m_codec_ctx; // a shortcut to st->codec
            AVFrame * m_frame;
            AVPacket * m_pkt;
            int m_pts = 0;
            std::ofstream m_file;
            int m_got_output = 0;

            void encode(AVFrame * frame, AVFormatContext * fmt_ctx, AVStream * st);

        public:
            Encoder(
                std::string file, 
                int width = 640, 
                int height = 480, 
                AVCodecID codec_id = AV_CODEC_ID_H264, 
                AVPixelFormat format = AV_PIX_FMT_YUV420P
            );
            ~Encoder();
            AVFrame * get_frame() noexcept;
            void write(AVFormatContext * fmt_ctx, AVStream * st);
            void flush(AVFormatContext * fmt_ctx, AVStream * st);
        };

        using encoder_t = Encoder;

    } // namespace video

} // namespace cfgo

#endif