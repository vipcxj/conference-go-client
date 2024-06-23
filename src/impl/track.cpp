#include "impl/track.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/async.hpp"
#include "cpptrace/cpptrace.hpp"
#include "spdlog/spdlog.h"
#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

#include <tuple>

namespace cfgo
{
    namespace impl
    {
        Track::Track(const msg::Track & msg, WebrtcWPtr webrtc, int cache_capicity): 
            m_logger(Log::instance().create_logger(Log::Category::TRACK)),
            m_weak_webrtc(webrtc),
            m_rtp_cache(cache_capicity), 
            m_rtcp_cache(cache_capicity), 
            m_inited(false), 
            m_seq(0)
        #ifdef CFGO_SUPPORT_GSTREAMER
        , m_gst_media(nullptr)
        #endif
        {
            type = msg.type;
            pubId = msg.pubId;
            globalId = msg.globalId;
            bindId = msg.bindId;
            rid = msg.rid;
            streamId = msg.streamId;
            labels = msg.labels;
        }

        Track::~Track()
        {
            #ifdef CFGO_SUPPORT_GSTREAMER
            if (m_gst_media)
            {
                gst_sdp_media_free(m_gst_media);
            }
            #endif
        }

        #ifdef CFGO_SUPPORT_GSTREAMER
        const GstSDPMedia * get_media_from_sdp(GstSDPMessage *sdp, const char* mid)
        {
            auto media_len = gst_sdp_message_medias_len(sdp);
            for (size_t i = 0; i < media_len; i++)
            {
                auto media = gst_sdp_message_get_media(sdp, i);
                auto media_id = gst_sdp_media_get_attribute_val(media, "mid");
                if (!strcmp(mid, media_id))
                {
                    return media;
                }
            }
            return nullptr;
        }
        #endif

        void Track::prepare_track() {
            if (auto webrtc = m_weak_webrtc.lock())
            {
                #ifdef CFGO_SUPPORT_GSTREAMER
                auto mid = track->mid();
                auto sdp = webrtc->gst_sdp();
                auto media = get_media_from_sdp(sdp, mid.c_str());
                if (!media)
                {
                    throw cpptrace::runtime_error("unable to find the media from sdp message with mid " + mid);
                }
                auto ret = gst_sdp_media_copy(media, &m_gst_media);
                if (ret != GstSDPResult::GST_SDP_OK)
                {
                    throw cpptrace::runtime_error("unable to clone the media from sdp message with mid " + mid);
                }
                #endif
            }
            if (!track)
            {
                throw cpptrace::logic_error("Before call receive_msg, a valid rtc::track should be set.");
            }
            track->onMessage(std::bind(&Track::on_track_msg, this, std::placeholders::_1), [](auto data) {});
            track->onOpen(std::bind(&Track::on_track_open, this));
            track->onClosed(std::bind(&Track::on_track_closed, this));
            track->onError(std::bind(&Track::on_track_error, this, std::placeholders::_1));
            m_inited = true;
        }

        uint32_t Track::makesure_min_seq()
        {
            if (m_rtp_cache.empty() && m_rtcp_cache.empty())
            {
                return 0xffffffff;
            }
            else if (m_rtp_cache.empty())
            {
                auto min_seq = m_rtcp_cache.front().first;
                if (min_seq == 0)
                {
                    m_rtcp_cache.pop_front();
                    return makesure_min_seq();
                }
                else
                {
                    return min_seq;
                }
            }
            else if (m_rtcp_cache.empty())
            {
                auto min_seq = m_rtp_cache.front().first;
                if (min_seq == 0)
                {
                    m_rtp_cache.pop_front();
                    return makesure_min_seq();
                }
                else
                {
                    return min_seq;
                }
            }
            else
            {
                auto min_seq_rtp = m_rtp_cache.front().first;
                auto min_seq_rtcp = m_rtcp_cache.front().first;
                if (min_seq_rtp < min_seq_rtcp)
                {
                    if (min_seq_rtp == 0)
                    {
                        m_rtp_cache.pop_front();
                        return makesure_min_seq();
                    }
                    else
                    {
                        return min_seq_rtp;
                    }
                }
                else
                {
                    if (min_seq_rtcp == 0)
                    {
                        m_rtcp_cache.pop_front();
                        return makesure_min_seq();
                    }
                    else
                    {
                        return min_seq_rtcp;
                    }
                }
            }
        }

        void Track::on_track_msg(rtc::binary data) {
            bool is_rtcp = rtc::IsRtcp(data);
            MsgBuffer & cache = is_rtcp ? m_rtcp_cache : m_rtp_cache;
            {
                std::lock_guard g(m_lock);
                if (m_seq == 0xffffffff)
                {
                    auto offset = makesure_min_seq();
                    for (auto &&v : m_rtcp_cache)
                    {
                        v.first -= offset;
                    }
                    for (auto &&v : m_rtp_cache)
                    {
                        v.first -= offset;
                    }
                    m_seq -= offset;
                }
                if (is_rtcp)
                {
                    m_statistics.m_rtcp_receives_bytes += data.size();
                    ++m_statistics.m_rtcp_receives_packets;
                    if (cache.full())
                    {
                        m_statistics.m_rtcp_drops_bytes += cache.front().second->size();
                        ++m_statistics.m_rtcp_drops_packets;
                    }
                }
                else
                {
                    m_statistics.m_rtp_receives_bytes += data.size();
                    ++m_statistics.m_rtp_receives_packets;
                    if (cache.full())
                    {
                        m_statistics.m_rtp_drops_bytes += cache.front().second->size();
                        ++m_statistics.m_rtp_drops_packets;
                    }
                }
                if (m_on_data)
                {
                    m_on_data(data, !is_rtcp);
                }
                if (m_on_stat)
                {
                    m_on_stat(m_statistics);
                }
                cache.push_back(std::make_pair(++m_seq, std::make_unique<rtc::binary>(std::move(data))));
            }
            chan_maybe_write(m_msg_notify);
        }

        void Track::on_track_open()
        {
            chan_must_write(m_open_notify);
        }

        void Track::on_track_closed()
        {
            CFGO_THIS_DEBUG("The track is closed.");
            chan_must_write(m_closed_notify);
        }

        void Track::on_track_error(std::string error)
        {
            CFGO_THIS_ERROR("{}", error);
        }

        auto Track::await_open_or_closed(close_chan close_ch) -> asio::awaitable<bool>
        {
            if (track->isOpen() || track->isClosed())
            {
                co_return true;
            }
            auto res = co_await cfgo::select(
                close_ch,
                asiochan::ops::read(m_open_notify, m_closed_notify)
            );
            if (!res)
            {
                co_return false;
            }
            else if (res.received_from(m_open_notify))
            {
                chan_must_write(m_open_notify);
            }
            else
            {
                chan_must_write(m_closed_notify);
            }
            co_return true;
        }

        auto Track::await_msg(cfgo::Track::MsgType msg_type, close_chan close_ch) -> asio::awaitable<cfgo::Track::MsgPtr>
        {
            if (!m_inited)
            {
                throw cpptrace::logic_error("Before call await_msg, call prepare_track at first.");
            }
            auto msg_ptr = receive_msg(msg_type);
            if (msg_ptr)
            {
                co_return std::move(msg_ptr);
            }
            if (is_valid_close_chan(close_ch) && close_ch.is_closed())
            {
                co_return nullptr;
            }

            if (!co_await await_open_or_closed(close_ch))
            {
                co_return nullptr;
            }
            if (is_valid_close_chan(close_ch) && close_ch.is_closed())
            {
                co_return nullptr;
            }
            do
            {
                auto res = co_await cfgo::select(
                    close_ch,
                    asiochan::ops::read(m_msg_notify, m_closed_notify)
                );
                if (!res)
                {
                    co_return nullptr;
                }
                else if (res.received_from(m_closed_notify))
                {
                    chan_must_write(m_closed_notify);
                }

                msg_ptr = receive_msg(msg_type);
                if (msg_ptr)
                {
                    co_return std::move(msg_ptr);
                }
                if (track->isClosed())
                {
                    co_return nullptr;
                }
            } while (true);
        }

        cfgo::Track::MsgPtr Track::receive_msg(cfgo::Track::MsgType msg_type) {
            if (!m_inited)
            {
                throw cpptrace::logic_error("Before call receive_msg, call prepare_track at first.");
            }

            std::lock_guard g(m_lock);
            cfgo::Track::MsgPtr msg_ptr;
            if (msg_type == cfgo::Track::MsgType::ALL)
            {
                if (m_rtp_cache.empty() && m_rtcp_cache.empty())
                {
                    return cfgo::Track::MsgPtr();
                }
                else if (m_rtp_cache.empty())
                {
                    m_rtcp_cache.front().second.swap(msg_ptr);
                    m_rtcp_cache.pop_front();
                }
                else if (m_rtcp_cache.empty())
                {
                    m_rtp_cache.front().second.swap(msg_ptr);
                    m_rtp_cache.pop_front();
                }
                else
                {
                    auto & rtp = m_rtp_cache.front();
                    auto & rtcp = m_rtcp_cache.front();
                    if (rtp.first > rtcp.first)
                    {
                        rtcp.second.swap(msg_ptr);
                        m_rtcp_cache.pop_front();
                    }
                    else
                    {
                        rtp.second.swap(msg_ptr);
                        m_rtp_cache.pop_front();
                    }
                }
            }
            else if (msg_type == cfgo::Track::MsgType::RTP)
            {
                if (m_rtp_cache.empty())
                {
                    return cfgo::Track::MsgPtr();
                }
                m_rtp_cache.front().second.swap(msg_ptr);
                m_rtp_cache.pop_front();
            }
            else
            {
                if (m_rtcp_cache.empty())
                {
                    return cfgo::Track::MsgPtr();
                }
                m_rtcp_cache.front().second.swap(msg_ptr);
                m_rtcp_cache.pop_front();
            }
            return msg_ptr;
        }

        void * Track::get_gst_caps(int pt) const
        {
#ifdef CFGO_SUPPORT_GSTREAMER
            if (!m_gst_media)
            {
                throw cpptrace::logic_error("No gst sdp media found, please call bind_client at first.");
            }
            if (auto webrtc = m_weak_webrtc.lock())
            {
                auto caps = gst_sdp_media_get_caps_from_media(m_gst_media, pt);
                gst_sdp_message_attributes_to_caps(webrtc->gst_sdp(), caps);
                gst_sdp_media_attributes_to_caps(m_gst_media, caps);
                auto s = gst_caps_get_structure(caps, 0);
                gst_structure_set_name(s, "application/x-rtp");
                if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "ULPFEC"))
                    gst_structure_set (s, "is-fec", G_TYPE_BOOLEAN, TRUE, NULL);
                return caps;
            }
            else
            {
                return nullptr;
            }
#else
            throw cpptrace::logic_error("The gstreamer support is disabled, so to_gst_caps method is not supported. Please enable gstreamer support by set cmake GSTREAMER_SUPPORT option to ON.");
#endif
        }

        void Track::set_on_data(const OnDataCb & cb)
        {
            std::lock_guard g(m_lock);
            m_on_data = cb;
        }

        void Track::set_on_data(OnDataCb && cb)
        {
            std::lock_guard g(m_lock);
            m_on_data = std::move(cb);
        }

        void Track::unset_on_data() noexcept
        {
            std::lock_guard g(m_lock);
            m_on_data = nullptr;
        }

        void Track::set_on_stat(const OnStatCb & cb)
        {
            std::lock_guard g(m_lock);
            m_on_stat = cb;
        }

        void Track::set_on_stat(OnStatCb && cb)
        {
            std::lock_guard g(m_lock);
            m_on_stat = std::move(cb);
        }

        void Track::unset_on_stat() noexcept
        {
            std::lock_guard g(m_lock);
            m_on_stat = nullptr;
        }

        std::uint64_t Track::get_rtp_drops_bytes() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtp_drops_bytes;
        }

        std::uint32_t Track::get_rtp_drops_packets() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtp_drops_packets;
        }

        std::uint64_t Track::get_rtp_receives_bytes() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtp_receives_bytes;
        }

        std::uint32_t Track::get_rtp_receives_packets() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtp_receives_packets;
        }

        float Track::get_rtp_drop_bytes_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtp_drop_bytes_rate();
        }

        float Track::get_rtp_drop_packets_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtp_drop_packets_rate();
        }

        std::uint32_t Track::get_rtp_packet_mean_size() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtp_packet_mean_size();
        }

        void Track::reset_rtp_data() noexcept
        {
            std::lock_guard g(m_lock);
            m_statistics.m_rtp_drops_bytes = 0;
            m_statistics.m_rtp_drops_packets = 0;
            m_statistics.m_rtp_receives_bytes = 0;
            m_statistics.m_rtp_receives_packets = 0;
        }

        std::uint64_t Track::get_rtcp_drops_bytes() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtcp_drops_bytes;
        }

        std::uint32_t Track::get_rtcp_drops_packets() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtcp_drops_packets;
        }

        std::uint64_t Track::get_rtcp_receives_bytes() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtcp_receives_bytes;
        }

        std::uint32_t Track::get_rtcp_receives_packets() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.m_rtcp_receives_packets;
        }

        float Track::get_rtcp_drop_bytes_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtcp_drop_bytes_rate();
        }

        float Track::get_rtcp_drop_packets_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtcp_drop_packets_rate();
        }

        std::uint32_t Track::get_rtcp_packet_mean_size() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.rtcp_packet_mean_size();
        }

        void Track::reset_rtcp_data() noexcept
        {
            std::lock_guard g(m_lock);
            m_statistics.m_rtcp_drops_bytes = 0;
            m_statistics.m_rtcp_drops_packets = 0;
            m_statistics.m_rtcp_receives_bytes = 0;
            m_statistics.m_rtcp_receives_packets = 0;
        }

        float Track::get_drop_bytes_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.drop_bytes_rate();
        }

        float Track::get_drop_packets_rate() noexcept
        {
            std::lock_guard g(m_lock);
            return m_statistics.drop_packets_rate();
        }
    } // namespace impl
    
} // namespace cfgo
