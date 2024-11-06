#include "cfgo/log.hpp"
#include "opencv2/opencv.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/video/muxer.hpp"
#include "cfgo/video/ffmpeg_cv.hpp"
#include "cfgo/video/camera.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/cppfix.hpp"
#include <csignal>
#include <thread>

extern "C"
{
    #include "libavdevice/avdevice.h"
    #include "libavutil/timestamp.h"
}

static volatile sig_atomic_t g_exit = 0;

void exit_handler(int)
{
    g_exit = 1;
}

uint8_t getPayloadType(cfgo::video::muxer_t::buffer_t buf, int buf_size)
{
    if (buf_size < 2)
    {
        return 0;
    }
    return buf[1] & 0x7F;
}

bool isRtcp(uint8_t pt)
{
    
    return pt >= 64 && pt <= 95;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    CFGO_INFO (
        "{}: pts:{} pts_time:{} dts:{} dts_time:{} duration:{} duration_time:{} stream_index:{}",
        tag,
        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
        pkt->stream_index
    );
}

int main()
{
    signal (SIGINT, exit_handler);

    avdevice_register_all();
    
    auto device_list = cfgo::video::list_devices();
    CFGO_INFO("{}", device_list);

    AVFormatContext * fmt_ctx = nullptr;
    auto dev = device_list.select(AVMediaType::AVMEDIA_TYPE_VIDEO);
    cfgo::video::check_av_err(avformat_open_input(&fmt_ctx, dev->name.c_str(), device_list.ifmt, nullptr), "could not open input, ");
    DEFER({
        avformat_close_input(&fmt_ctx);
    });
    cfgo::video::check_av_err(avformat_find_stream_info(fmt_ctx, nullptr), "could not retrieve input stream information");
    
    CFGO_INFO("found {} streams", fmt_ctx->nb_streams);
    for (int i = 0; i < fmt_ctx->nb_streams; i++)
    {
        av_dump_format(fmt_ctx, i, dev->name.c_str(), 0);
    }
    
    // for (int i = 0; i < fmt_ctx->nb_streams; i++)
    // {
    //     AVStream * in_stream = fmt_ctx->streams[i];
    //     auto in_codecpar = in_stream->codecpar;
    //     if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
    //     {
    //         /* code */
    //     }
        
    // }
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
        auto start = std::chrono::high_resolution_clock::now();
        cfgo::video::check_av_err(av_read_frame(fmt_ctx, pkt), "could not read pkt from input device, ");
        auto used = std::chrono::high_resolution_clock::now() - start;
        CFGO_INFO("read pkt cost {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(used).count());
        std::this_thread::sleep_for(std::chrono::milliseconds {30});
        log_packet(fmt_ctx, pkt, "in");
        av_packet_unref(pkt);
        if (g_exit)
        {
            break;
        }
        
    } while (true);
    
    



    // auto ofmt = av_guess_format("rtp", nullptr, nullptr);
    // cfgo::video::opt_t opt = {{"payload_type", "99"}, {"ssrc", "12345"}};
    // cfgo::video::muxer_t muxer("", ofmt, opt);
    // muxer.add_callback([](cfgo::video::muxer_t::buffer_t buf, int buf_size) {
    //     auto pt = getPayloadType(buf, buf_size);
    //     if (isRtcp(pt))
    //     {
    //         CFGO_INFO("accept rtcp buffer with pt {} and size {}", pt, buf_size);
    //     }
    //     else
    //     {
    //         CFGO_INFO("accept rtp buffer with pt {} and size {}", pt, buf_size);
    //     }
        
    // });
    // auto sid = muxer.add_stream(AV_CODEC_ID_H264);
    // CFGO_INFO("build info: {}", cv::getBuildInformation());
    // cv::VideoCapture cap(0, cv::CAP_ANY);
    // CFGO_INFO("open: {}", cap.isOpened());
    // CFGO_INFO("backend: {}", cap.getBackendName());
    // cv::Mat mat;
    // do
    // {
    //     cap >> mat;
    //     if (mat.empty())
    //     {
    //         break;
    //     }
    //     auto frame = muxer.get_frame(sid);
    //     cfgo::video::cv_mat_to_yuv420p_av_frame(mat, frame);
    //     muxer.write_frame(sid, frame);
    //     if (g_exit)
    //     {
    //         break;
    //     }
    // } while (true);
}