#ifndef _CFGO_VIDEO_MUXER_HPP_
#define _CFGO_VIDEO_MUXER_HPP_

#include <cstdint>
#include <vector>
extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
}

namespace cfgo
{
    namespace video
    {
        struct OutputStream
        {
            AVStream *st;
            AVCodecContext *enc;

            /* pts of the next frame that will be generated */
            int64_t next_pts;
            int samples_count;

            AVFrame *frame;
            AVFrame *tmp_frame;

            AVPacket *tmp_pkt;

            float t, tincr, tincr2;

            struct SwsContext *sws_ctx;
            struct SwrContext *swr_ctx;
        };
        using output_stream_t = OutputStream;
        

        class Muxer
        {
        private:
            AVFormatContext * m_fmt_ctx;
            std::vector<output_stream_t> m_streams;
        public:
            Muxer(/* args */);
            ~Muxer();

            int add_stream(enum AVCodecID codec_id);
        };
        
        using muxer_t = Muxer;
    } // namespace video
    
} // namespace cfgo


#endif