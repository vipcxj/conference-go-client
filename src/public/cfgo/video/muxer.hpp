#ifndef _CFGO_VIDEO_MUXER_HPP_
#define _CFGO_VIDEO_MUXER_HPP_

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include "cfgo/alias.hpp"

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"

    struct SwsContext;
    struct SwrContext;
}

namespace cfgo
{
    namespace video
    {
        struct OutputStream
        {
            AVStream *st = nullptr;
            AVCodecContext *enc = nullptr;

            /* pts of the next frame that will be generated */
            int64_t next_pts = 0;
            int samples_count = 0;

            AVFrame *frame = nullptr;
            AVFrame *tmp_frame = nullptr;

            AVPacket *tmp_pkt = nullptr;

            float t, tincr, tincr2;

            struct SwsContext *sws_ctx = nullptr;
            struct SwrContext *swr_ctx = nullptr;

            ~OutputStream()
            {
                free();
            }

            AVFrame * get_frame() noexcept
            {
                return frame;
            }

            bool write_frame(AVFormatContext * fmt_ctx, AVFrame * frame);

            void free() noexcept;
        };
        using output_stream_t = OutputStream;
        

        class Muxer
        {
        public:
        #if FF_API_AVIO_WRITE_NONCONST
            using buffer_t = uint8_t *;
        #else
            using buffer_t = const uint8_t *;
        #endif
        private:
            std::string m_url;
            const AVOutputFormat * m_o_fmt;
            AVFormatContext * m_fmt_ctx = nullptr;
            AVIOContext * m_io = nullptr;
            std::vector<output_stream_t> m_streams;
            bool m_head = false;
            std::unordered_map<int, std::function<void(buffer_t buf, int buf_size)>> m_cbs;
            int m_next_cb_id = 0;
            mutex m_cb_mux; 

            void _make_sure_header();
            static int _write_packet_handle(void * opaque, buffer_t buf, int buf_size);
        public:
            Muxer(std::string url, const AVOutputFormat * ofmt);
            ~Muxer();

            int add_stream(enum AVCodecID codec_id);
            output_stream_t & get_stream(int stream_id);
            AVFrame * get_frame(int stream_id);
            AVFormatContext * get_format_context();
            bool write_frame(int stream_id, AVFrame * frame);
            int add_callback(std::function<void(uint8_t *buf, int buf_size)> cb);
            void remove_callback(int cb_id);
        };
        
        using muxer_t = Muxer;
    } // namespace video
    
} // namespace cfgo


#endif