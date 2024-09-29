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
        Track::Track(
            const msg::Track & msg, 
            std::int32_t rtp_cache_min_segments,
            std::int32_t rtp_cache_max_segments,
            std::int32_t rtp_cache_segment_capicity,
            std::int32_t rtcp_cache_min_segments,
            std::int32_t rtcp_cache_max_segments,
            std::int32_t rtcp_cache_segment_capicity
        ): 
            m_logger(Log::instance().create_logger(Log::Category::TRACK)),
            m_rtp_cache(rtp_cache_segment_capicity, rtp_cache_max_segments, rtp_cache_min_segments), 
            m_rtcp_cache(rtcp_cache_segment_capicity, rtcp_cache_max_segments, rtcp_cache_min_segments)
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
            if (m_sdp)
            {
                gst_sdp_message_free(m_sdp);
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

        void Track::prepare_track(
            #ifdef CFGO_SUPPORT_GSTREAMER
            GstSDPMessage *sdp
            #endif
        ) {
            #ifdef CFGO_SUPPORT_GSTREAMER
            auto mid = track->mid();
            if (m_sdp)
            {
                gst_sdp_message_free(m_sdp);
            }
            auto ret = gst_sdp_message_copy(sdp, &m_sdp);
            if (ret != GstSDPResult::GST_SDP_OK)
            {
                throw cpptrace::runtime_error("unable to copy the sdp message with mid " + mid);
            }
            #endif
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

        #ifdef CFGO_SUPPORT_GSTREAMER
        const GstSDPMedia * Track::gst_media() const {
            return get_media_from_sdp(m_sdp, track->mid().c_str());
        }
        #endif

        uint32_t Track::makesure_min_seq()
        {
            if (m_rtp_cache.empty() && m_rtcp_cache.empty())
            {
                return 0xffffffff;
            }
            else if (m_rtp_cache.empty())
            {
                auto min_seq = m_rtcp_cache.queue_head()->first;
                if (min_seq == 0)
                {
                    m_rtcp_cache.dequeue();
                    return makesure_min_seq();
                }
                else
                {
                    return min_seq;
                }
            }
            else if (m_rtcp_cache.empty())
            {
                auto min_seq = m_rtp_cache.queue_head()->first;
                if (min_seq == 0)
                {
                    m_rtp_cache.dequeue();
                    return makesure_min_seq();
                }
                else
                {
                    return min_seq;
                }
            }
            else
            {
                auto min_seq_rtp = m_rtp_cache.queue_head()->first;
                auto min_seq_rtcp = m_rtcp_cache.queue_head()->first;
                if (min_seq_rtp < min_seq_rtcp)
                {
                    if (min_seq_rtp == 0)
                    {
                        m_rtp_cache.dequeue();
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
                        m_rtcp_cache.dequeue();
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
            if (is_rtcp)
            {
                m_first_rtcp_packet_received = true;
            }
            else
            {
                m_first_rtp_packet_received = true;
            }
            
            MsgBuffer & cache = is_rtcp ? m_rtcp_cache : m_rtp_cache;
            {
                std::lock_guard g(m_lock);
                if (m_seq == 0xffffffff)
                {
                    auto offset = makesure_min_seq();
                    for (auto & v : m_rtp_cache) {
                        v.first -= offset;
                    }
                    for (auto & v : m_rtcp_cache) {
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
                        m_statistics.m_rtcp_drops_bytes += cache.queue_head()->second->size();
                        ++m_statistics.m_rtcp_drops_packets;
                    }
                    m_statistics.m_rtcp_cache_size = std::min(cache.size() + 1, cache.capacity());
                }
                else
                {
                    m_statistics.m_rtp_receives_bytes += data.size();
                    ++m_statistics.m_rtp_receives_packets;
                    if (cache.full())
                    {
                        m_statistics.m_rtp_drops_bytes += cache.queue_head()->second->size();
                        ++m_statistics.m_rtp_drops_packets;
                    }
                    m_statistics.m_rtp_cache_size = std::min(cache.size() + 1, cache.capacity());
                }
                if (m_on_data)
                {
                    m_on_data(data, !is_rtcp);
                }
                if (m_on_stat)
                {
                    m_on_stat(m_statistics);
                }
                cache.enqueue(std::make_pair(++m_seq, std::make_unique<rtc::binary>(std::move(data))), true);
            }
            m_state_notifier.notify();
        }

        void Track::on_track_open()
        {
            m_opened = true;
            m_state_notifier.notify();
        }

        void Track::on_track_closed()
        {
            m_closed = true;
            CFGO_THIS_DEBUG("The track is closed.");
            m_state_notifier.notify();
            std::lock_guard g(m_close_cb_lock);
            if (m_on_close)
            {
                m_on_close();
            }
        }

        void Track::on_track_error(std::string error)
        {
            CFGO_THIS_ERROR("{}", error);
        }

        auto Track::await_open_or_close(close_chan closer) -> asio::awaitable<bool>
        {
            if (!m_inited)
            {
                throw cpptrace::logic_error("Before call await_open_or_close, call prepare_track at first.");
            }
            if (m_opened || m_closed)
            {
                co_return true;
            }
            do
            {
                auto ch = m_state_notifier.make_notfiy_receiver();
                if (m_opened || m_closed)
                {
                    co_return true;
                }
                if (!co_await chan_read<void>(ch, closer))
                {
                    co_return false;
                }
            } while (true);
        }

        bool Track::_is_first_msg_received(cfgo::Track::MsgType msg_type) const noexcept
        {
            if (msg_type == cfgo::Track::MsgType::ALL) {
                return m_first_rtp_packet_received && m_first_rtcp_packet_received;
            }
            else if (msg_type == cfgo::Track::MsgType::RTP)
            {
                return m_first_rtp_packet_received;
            }
            else
            {
                return m_first_rtcp_packet_received;
            }
        }

        auto Track::await_first_msg_received(cfgo::Track::MsgType msg_type, close_chan closer) -> asio::awaitable<bool>
        {
            if (!m_inited)
            {
                throw cpptrace::logic_error("Before call await_open_or_close, call prepare_track at first.");
            }
            if (_is_first_msg_received(msg_type))
            {
                co_return true;
            }
            do
            {
                auto ch = m_state_notifier.make_notfiy_receiver();
                if (_is_first_msg_received(msg_type))
                {
                    co_return true;
                }
                if (!co_await chan_read<void>(ch, closer))
                {
                    co_return false;
                }
            } while (true);
        }

        auto Track::await_msg(cfgo::Track::MsgType msg_type, close_chan close_ch) -> asio::awaitable<cfgo::Track::MsgPtr>
        {
            if (!m_inited)
            {
                throw cpptrace::logic_error("Before call await_msg, call prepare_track at first.");
            }
            do
            {
                auto ch = m_state_notifier.make_notfiy_receiver();
                if (m_closed)
                {
                    co_return nullptr;
                }
                else
                {
                    auto msg_ptr = receive_msg(msg_type);
                    if (msg_ptr)
                    {
                        co_return std::move(msg_ptr);
                    }
                }
                if (!co_await chan_read<void>(ch, close_ch))
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
            cfgo::Track::MsgPtr msg_ptr = nullptr;
            if (msg_type == cfgo::Track::MsgType::ALL)
            {
                if (m_rtp_cache.empty() && m_rtcp_cache.empty())
                {
                    return msg_ptr;
                }
                else if (m_rtp_cache.empty())
                {
                    m_rtcp_cache.queue_head()->second.swap(msg_ptr);
                    m_rtcp_cache.dequeue();
                }
                else if (m_rtcp_cache.empty())
                {
                    m_rtp_cache.queue_head()->second.swap(msg_ptr);
                    m_rtp_cache.dequeue();
                }
                else
                {
                    auto rtp = m_rtp_cache.queue_head();
                    auto rtcp = m_rtcp_cache.queue_head();
                    if (rtp->first > rtcp->first)
                    {
                        rtcp->second.swap(msg_ptr);
                        m_rtcp_cache.dequeue();
                    }
                    else
                    {
                        rtp->second.swap(msg_ptr);
                        m_rtp_cache.dequeue();
                    }
                }
            }
            else if (msg_type == cfgo::Track::MsgType::RTP)
            {
                if (m_rtp_cache.empty())
                {
                    return cfgo::Track::MsgPtr();
                }
                m_rtp_cache.queue_head()->second.swap(msg_ptr);
                m_rtp_cache.dequeue();
            }
            else
            {
                if (m_rtcp_cache.empty())
                {
                    return cfgo::Track::MsgPtr();
                }
                m_rtcp_cache.queue_head()->second.swap(msg_ptr);
                m_rtcp_cache.dequeue();
            }
            return msg_ptr;
        }

        void * Track::get_gst_caps(int pt) const
        {
#ifdef CFGO_SUPPORT_GSTREAMER
            if (!m_sdp)
            {
                throw cpptrace::logic_error("No gst sdp found, please call bind_client at first.");
            }
            auto media = gst_media();
            if (!media)
            {
                throw cpptrace::runtime_error(fmt::format("Unable to extract the sdp media from sdp message with mid {}.", track->mid()));
            }
            
            auto caps = gst_sdp_media_get_caps_from_media(media, pt);
            gst_sdp_message_attributes_to_caps(m_sdp, caps);
            gst_sdp_media_attributes_to_caps(media, caps);
            auto s = gst_caps_get_structure(caps, 0);
            gst_structure_set_name(s, "application/x-rtp");
            if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "ULPFEC"))
                gst_structure_set (s, "is-fec", G_TYPE_BOOLEAN, TRUE, NULL);
            return caps;
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

        void Track::set_on_close(const OnCloseCb & cb)
        {
            std::lock_guard g(m_close_cb_lock);
            m_on_close = cb;
        }

        void Track::set_on_close(OnCloseCb && cb)
        {
            std::lock_guard g(m_close_cb_lock);
            m_on_close = std::move(cb);
        }

        void Track::unset_on_close() noexcept
        {
            std::lock_guard g(m_close_cb_lock);
            m_on_close = nullptr;
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

        std::uint16_t Track::get_rtp_cache_size() noexcept
        {
            std::lock_guard g(m_lock);
            return m_rtp_cache.size();
        }
        std::uint16_t Track::get_rtcp_cache_size() noexcept
        {
            std::lock_guard g(m_lock);
            return m_rtcp_cache.size();
        }
        std::uint16_t Track::get_rtp_cache_capicity() const noexcept
        {
            return m_rtp_cache.capacity();
        }
        std::uint16_t Track::get_rtcp_cache_capicity() const noexcept
        {
            return m_rtcp_cache.capacity();
        }
    } // namespace impl
    
} // namespace cfgo
