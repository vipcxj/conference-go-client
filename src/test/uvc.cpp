#include "cfgo/log.hpp"
#include "opencv2/opencv.hpp"
#include "cfgo/video/encoder.hpp"
#include "cfgo/video/ffmpeg_cv.hpp"
#include <csignal>

static volatile sig_atomic_t g_exit = 0;

void exit_handler(int)
{
    g_exit = 1;
}

int main()
{
    signal (SIGINT, exit_handler);
    cfgo::video::encoder_t encoder("test.avi");
    CFGO_INFO("build info: {}", cv::getBuildInformation());
    cv::VideoCapture cap(-1, cv::CAP_ANY);
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
        cfgo::video::cv_mat_to_yuv420p_av_frame(mat, encoder.get_frame());
        encoder.write();
        if (g_exit)
        {
            break;
        }
    } while (true);
}