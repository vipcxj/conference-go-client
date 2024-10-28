#include "cfgo/log.hpp"
#include "opencv2/opencv.hpp"
#include "cfgo/video/muxer.hpp"
#include "cfgo/video/ffmpeg_cv.hpp"
#include <csignal>

static volatile sig_atomic_t g_exit = 0;

void exit_handler(int)
{
    g_exit = 1;
}

uint8_t getPayloadType(uint8_t * buf, int buf_size)
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

int main()
{
    signal (SIGINT, exit_handler);
    auto ofmt = av_guess_format("rtp", nullptr, nullptr);
    cfgo::video::muxer_t::opt_t opt = {{"payload_type", "99"}, {"ssrc", "12345"}};
    cfgo::video::muxer_t muxer("", ofmt, opt);
    muxer.add_callback([](uint8_t * buf, int buf_size) {
        auto pt = getPayloadType(buf, buf_size);
        if (isRtcp(pt))
        {
            CFGO_INFO("accept rtcp buffer with pt {} and size {}", pt, buf_size);
        }
        else
        {
            CFGO_INFO("accept rtp buffer with pt {} and size {}", pt, buf_size);
        }
        
    });
    auto sid = muxer.add_stream(AV_CODEC_ID_H264);
    CFGO_INFO("build info: {}", cv::getBuildInformation());
    cv::VideoCapture cap(0, cv::CAP_ANY);
    CFGO_INFO("open: {}", cap.isOpened());
    CFGO_INFO("backend: {}", cap.getBackendName());
    cv::Mat mat;
    do
    {
        cap >> mat;
        if (mat.empty())
        {
            break;
        }
        auto frame = muxer.get_frame(sid);
        cfgo::video::cv_mat_to_yuv420p_av_frame(mat, frame);
        muxer.write_frame(sid, frame);
        if (g_exit)
        {
            break;
        }
    } while (true);
}