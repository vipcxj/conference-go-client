#include "cfgo/video/ffmpeg_cv.hpp"
#include "cfgo/video/err.hpp"

extern "C" {
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

namespace cfgo
{
    namespace video
    {

        AVFrame *cv_mat_to_yuv420p_av_frame(const cv::Mat & mat, AVFrame * frame)
        {
            int width = mat.cols;
            int height = mat.rows;
            int cvLinesizes[1];
            cvLinesizes[0] = mat.step1();
            if (frame == NULL)
            {
                frame = av_frame_alloc();
                av_image_alloc(frame->data, frame->linesize, width, height,
                               AVPixelFormat::AV_PIX_FMT_YUV420P, 1);
            }
            check_av_err(av_frame_make_writable(frame), "could not make frame writable, ");
            SwsContext *conversion = sws_getContext(
                width, height, AVPixelFormat::AV_PIX_FMT_BGR24, 
                frame->width, frame->height, (AVPixelFormat) frame->format, 
                SWS_FAST_BILINEAR, NULL, NULL, NULL
            );
            sws_scale(
                conversion,
                &mat.data, cvLinesizes, 0, height, 
                frame->data, frame->linesize
            );
            sws_freeContext(conversion);
            return frame;
        }
    } // namespace video

} // namespace cfgo
