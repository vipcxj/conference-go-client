#ifndef _CFGO_VIDEO_FFMPEG_CV_HPP_
#define _CFGO_VIDEO_FFMPEG_CV_HPP_

#include "opencv2/opencv.hpp"
extern "C" {
#include "libavutil//frame.h"
}

namespace cfgo
{
    namespace video
    {
        AVFrame * cv_mat_to_yuv420p_av_frame(cv::Mat & mat);
    } // namespace video
    
} // namespace cfgo


#endif