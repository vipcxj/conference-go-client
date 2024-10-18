#include "cfgo/video/ffmpeg_cv.hpp"
#include "cfgo/video/err.hpp"

namespace cfgo
{
    namespace video
    {

        AVFrame * cv_mat_to_yuv420p_av_frame(cv::Mat & mat)
        {
            auto width = mat.cols;
            auto height = mat.rows;
            auto frame = av_frame_alloc();
            frame->width = width;
            frame->height = height;
            frame->format = AV_PIX_FMT_YUV420P;

            check_av_err(av_frame_get_buffer(frame, 0));
            av_frame_make_writable(frame);

            cv::cvtColor(mat, mat, cv::COLOR_BGR2YUV_I420);

            int frame_size = width * height;
            auto data = mat.data;
            memcpy(frame->data[0], data, frame_size);
            memcpy(frame->data[1], data + frame_size, frame_size / 4);
            memcpy(frame->data[2], data + frame_size * 5/4, frame_size / 4);

            return frame;
        }
    } // namespace video
    
} // namespace cfgo
