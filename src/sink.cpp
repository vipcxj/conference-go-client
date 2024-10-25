#include "cfgo/async.hpp"
#include "cfgo/sink.hpp"
#include "cfgo/video/muxer.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/ffmpeg_cv.hpp"
#include "cfgo/block_queue.hpp"

#include "opencv2/opencv.hpp"

#include <list>
#include <atomic>

namespace cfgo
{
    namespace impl
    {
        template<typename Derived>
        class BaseSink : public std::enable_shared_from_this<Derived>, public cfgo::Sink
        {
        protected:
            video::muxer_t m_muxer;
            // 0 not start
            // 1 start
            // 2 closed
            std::atomic_int m_state {0};
            bool m_streams_locked = false;
            std::list<RtcTrackWPtr> m_tracks;
            BlockingQueue<std::pair<int, AVFrame *>> m_frames;
            mutex m_mux;
            mutex m_state_mux;
            close_chan m_closer;
            unique_chan<std::exception_ptr> m_err_chan;
            int add_stream(AVCodecID codec_id);
            using std::enable_shared_from_this<Derived>::weak_from_this;
            using std::enable_shared_from_this<Derived>::shared_from_this;
        public:
            BaseSink();
            ~BaseSink() noexcept {}

            RtcTrackPtr create_track(rtc::PeerConnection & peer);

            bool start();
            bool close();
            auto await() -> asio::awaitable<void>
            {
                auto err_c = co_await chan_read<std::exception_ptr>(m_err_chan, m_closer);
                if (err_c)
                {
                    close();
                    std::rethrow_exception(std::move(err_c.value()));
                }
                else if (m_closer.is_timeout())
                {
                    throw CancelError(m_closer);
                }
            }
        };

        template<typename Derived>
        BaseSink<Derived>::BaseSink(): m_muxer("", av_guess_format("rtp", nullptr, nullptr)) {
            // called in weak_self.lock() block, so "this" always valied
            m_muxer.add_callback([this](video::Muxer::buffer_t buf, int buf_size) {
                try
                {
                    std::lock_guard lk(m_mux);
                    for (auto iter = m_tracks.cbegin(); iter != m_tracks.cend();)
                    {
                        if (auto track = iter->lock())
                        {
                            track->send((const std::byte *) buf, buf_size);
                            ++iter;
                        }
                        else
                        {
                            iter = m_tracks.erase(iter);
                        }
                    }
                }
                catch(...)
                {
                    chan_maybe_write(m_err_chan, std::current_exception());
                }
            });
        }

        template<typename Derived>
        int BaseSink<Derived>::add_stream(AVCodecID codec_id)
        {
            std::lock_guard lk(m_mux);
            if (m_streams_locked)
            {
                throw cpptrace::runtime_error("add_stream should not called after create_track");
            }  
            return m_muxer.add_stream(codec_id);
        }

        template<typename Derived>
        RtcTrackPtr BaseSink<Derived>::create_track(rtc::PeerConnection & peer)
        {
            std::lock_guard lk(m_mux);
            m_streams_locked = true;
            char buf [1024 * 16] = {};
            AVFormatContext * ac[] = { m_muxer.get_format_context() };
            video::check_av_err(av_sdp_create(ac, 1, buf, sizeof(buf)/sizeof(char)), "could not create sdp from format context, ");
            rtc::Description::Media media(buf);
            auto track = peer.addTrack(media);
            m_tracks.push_back(track);
            return track;
        }

        template<typename Derived>
        bool BaseSink<Derived>::start()
        {
            std::lock_guard lk(m_state_mux);
            if (m_state > 0)
            {
                return false;
            }
            std::thread t([weak_self = weak_from_this()]() {
                try
                {
                    do
                    {
                        if (auto self = weak_self.lock())
                        {
                            if (self->m_state == 2)
                            {
                                break;
                            }
                            std::pair<int, AVFrame *> stream_frame_pair;
                            self->m_frames.take(stream_frame_pair);
                            {
                                std::lock_guard lk(self->m_mux);
                                self->m_muxer.write_frame(stream_frame_pair.first, stream_frame_pair.second);
                            }
                        }
                        else
                        {
                            break;
                        }
                    } while (true);
                }
                catch(...)
                {
                    if (auto self = weak_self.lock())
                    {
                        chan_maybe_write(self->m_err_chan, std::current_exception());
                    }
                }
            });
            t.detach();
            m_state = 1;
            return true;
        }

        template<typename Derived>
        bool BaseSink<Derived>::close()
        {
            std::lock_guard lk(m_state_mux);
            if (m_state == 2)
            {
                return false;
            }
            else
            {
                m_state = 2;
                m_closer.close();
                return true;
            }
        }

        class CameraSink : public BaseSink<CameraSink>
        {
        private:
            int m_device;
            int m_stream_id;
        public:
            CameraSink(int device_id = -1): m_device(device_id) {
                m_stream_id = add_stream(AV_CODEC_ID_H264);
            }

            bool start()
            {
                if (CameraSink::start())
                {
                    std::thread t([weak_self = weak_from_this(), device_id = m_device]() {
                        try
                        {
                            cv::VideoCapture cap(device_id, cv::CAP_ANY);
                            cv::Mat mat;
                            do
                            {
                                if (auto self = weak_self.lock())
                                {
                                    if (self->m_state == 2)
                                    {
                                        break;
                                    }
                                    cap >> mat;
                                    if (mat.empty())
                                    {
                                        break;
                                    }
                                    {
                                        std::lock_guard lk(self->m_mux);
                                        auto frame = self->m_muxer.get_frame(self->m_stream_id);
                                        frame = video::cv_mat_to_yuv420p_av_frame(mat, frame);
                                        self->m_frames.put(std::make_pair(self->m_stream_id, frame));
                                    }
                                }
                                else
                                {
                                    break;
                                }
                            } while (true);
                            if (auto self = weak_self.lock())
                            {
                                self->close();
                            }
                        }
                        catch(...)
                        {
                            if (auto self = weak_self.lock())
                            {
                                chan_maybe_write(self->m_err_chan, std::current_exception());
                            }
                        }
                    });
                    t.detach();
                    return true;
                }
                else
                {
                    return false;
                }
            }
        };
        
    } // namespace impl

    SinkPtr make_camera_sink(int device_id)
    {
        return std::make_shared<impl::CameraSink>(device_id);
    }
    
} // namespace cfgo
