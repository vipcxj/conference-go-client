#include "cfgo/log.hpp"
#include "opencv2/opencv.hpp"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

AVFrame * CVMat2AVFrame(cv::Mat & mat)
{
    AVPixelFormat dstFormat = AV_PIX_FMT_YUV420P;
    int width = mat.cols;
    int height = mat.rows;

    AVFrame *frame = av_frame_alloc();
    frame->width = width;
    frame->height = height;
    frame->format = dstFormat;

    auto ret = av_frame_get_buffer(frame, 0);
    
}

int main()
{
    CFGO_INFO("build info: {}", cv::getBuildInformation());
    cv::VideoCapture cap(-1, cv::CAP_ANY);
    CFGO_INFO("open: {}", cap.isOpened());
    CFGO_INFO("backend: {}", cap.getBackendName());
    cv::Mat mat;
    cap >> mat;
    sws_getCon
}