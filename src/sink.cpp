#include "cfgo/async.hpp"
#include "cfgo/sink.hpp"
#include "cfgo/video/muxer.hpp"
#include "cfgo/video/err.hpp"
#include "cfgo/video/ffmpeg_cv.hpp"
#include "cfgo/block_queue.hpp"

#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"

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
            std::atomic_bool m_pli = false;
            bool m_streams_locked = false;
            std::list<RtcTrackWPtr> m_tracks;
            BlockingQueue<std::pair<int, AVFrame *>> m_frames;
            mutex m_mux;
            mutex m_state_mux;
            close_chan m_closer;
            unique_chan<std::exception_ptr> m_err_chan;
            int add_stream(AVCodecID codec_id);
            void mark_pli();
            using std::enable_shared_from_this<Derived>::weak_from_this;
            using std::enable_shared_from_this<Derived>::shared_from_this;
        public:
            BaseSink();
            ~BaseSink() noexcept {}

            RtcTrackPtr create_track(rtc::PeerConnection & peer, const rtc::Description::Media & media);

            bool start(int payload_type);
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
        void BaseSink<Derived>::mark_pli()
        {
            m_pli = true;
        }

        rtc::Description::Video create_video(std::string mid, uint32_t ssrc)
        {
            auto video = rtc::Description::Video(std::move(mid));
            // from pion payload types.

            video.addVP8Codec(96);
            // video.addRtxCodec(97, 96, 90000);
            video.addH264Codec(106, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
            // video.addRtxCodec(107, 106, 90000);
            video.addH264Codec(108, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f");
            // video.addRtxCodec(109, 108, 90000);
            video.addH264Codec(102, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f");
            // video.addRtxCodec(103, 102, 90000);
            video.addH264Codec(104, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f");
            // video.addRtxCodec(105, 104, 90000);
            video.addH264Codec(127, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f");
            // video.addRtxCodec(125, 127, 90000);
            video.addH264Codec(39, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f");
            // video.addRtxCodec(40, 39, 90000);
            video.addAV1Codec(45);
            // video.addRtxCodec(46, 45, 90000);
            video.addVP9Codec(98, "profile-id=0");
            // video.addRtxCodec(99, 98, 90000);
            video.addVP9Codec(100, "profile-id=2");
            // video.addRtxCodec(101, 100, 90000);
            video.addH264Codec(112, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f");
            // video.addRtxCodec(113, 112, 90000);
            video.addSSRC(ssrc, boost::uuids::to_string(boost::uuids::random_generator()()));
            return video;
        }

        rtc::Description::Audio create_audio(std::string mid, uint32_t ssrc)
        {
            auto audio = rtc::Description::Audio(std::move(mid));
            // from pion payload types.
            // acc is not supported by pion, so not list here.
            
            audio.addOpusCodec(111);
            audio.addPCMUCodec(0);
            audio.addPCMACodec(8);
            audio.addSSRC(ssrc, boost::uuids::to_string(boost::uuids::random_generator()()));
            return audio;
        }

        template<typename Derived>
        RtcTrackPtr BaseSink<Derived>::create_track(rtc::PeerConnection & peer, const rtc::Description::Media & media)
        {
            std::lock_guard lk(m_mux);
            m_streams_locked = true;

            auto track = peer.addTrack(media);
            auto pli_handler = std::make_shared<rtc::PliHandler>([weak_self = weak_from_this()]() {
                if (auto self = weak_self.lock())
                {
                    self->mark_pli();
                }
            });
            auto nack_handler = std::make_shared<rtc::RtcpNackResponder>(32);
            nack_handler->addToChain(pli_handler);
            track->setMediaHandler(nack_handler);
            m_tracks.push_back(track);
            return track;
        }

        template<typename Derived>
        bool BaseSink<Derived>::start(int payload_type)
        {
            std::lock_guard lk(m_state_mux);
            if (m_state > 0)
            {
                return false;
            }
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
                // m_stream_id = add_stream(AV_CODEC_ID_H264);
            }

            bool start(int payload_type)
            {
                if (CameraSink::start(payload_type))
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
                                        if (self->m_pli)
                                        {
                                            frame->pict_type = AVPictureType::AV_PICTURE_TYPE_I;
                                            self->m_pli = false;
                                        }
                                        frame = video::cv_mat_to_yuv420p_av_frame(mat, frame);
                                        self->m_muxer.write_frame(self->m_stream_id, frame);
                                        frame->pict_type = AVPictureType::AV_PICTURE_TYPE_NONE;
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
