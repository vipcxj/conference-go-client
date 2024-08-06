#ifndef _CFGO_IMP_TRACK_HPP_
#define _CFGO_IMP_TRACK_HPP_

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include "cfgo/config/configuration.h"
#include "cfgo/track.hpp"
#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
#include "boost/circular_buffer.hpp"
#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

namespace rtc
{
    class Track;
} // namespace rtc

namespace cfgo
{
    namespace impl
    {
        struct Track : public std::enable_shared_from_this<Track>
        {
            using Ptr = std::shared_ptr<Track>;
            using MsgBuffer = boost::circular_buffer<std::pair<std::uint32_t, cfgo::Track::MsgPtr>>;
            using OnDataCb = cfgo::Track::OnDataCb;
            using OnStatCb = cfgo::Track::OnStatCb;
            using OnCloseCb = cfgo::Track::OnCloseCb;
            using Statistics = cfgo::Track::Statistics;
            
            std::string type;
            std::string pubId;
            std::string globalId;
            std::string bindId;
            std::string rid;
            std::string streamId;
            std::unordered_map<std::string, std::string> labels;
            std::shared_ptr<rtc::Track> track;

            bool m_inited {false};
            Logger m_logger;
            mutex m_lock;
            MsgBuffer m_rtp_cache;
            MsgBuffer m_rtcp_cache;
            uint32_t m_seq {0};
            OnDataCb m_on_data = nullptr;
            Statistics m_statistics;
            OnStatCb m_on_stat = nullptr;
            OnCloseCb m_on_close = nullptr;
            mutex m_close_cb_lock;
            unique_void_chan m_msg_notify;
            unique_void_chan m_open_notify;
            unique_void_chan m_closed_notify;
            #ifdef CFGO_SUPPORT_GSTREAMER
            GstSDPMessage *m_sdp = nullptr;
            const GstSDPMedia * gst_media() const;
            #endif

            Track(const msg::Track & msg, int rtp_cache_capicity, int rtcp_cache_capicity);
            ~Track();

            uint32_t makesure_min_seq();
            void prepare_track(
                #ifdef CFGO_SUPPORT_GSTREAMER
                GstSDPMessage *sdp
                #endif
            );
            void on_track_msg(rtc::binary data);
            void on_track_open();
            void on_track_closed();
            void on_track_error(std::string error);
            auto await_open_or_closed(close_chan close_ch) -> asio::awaitable<bool>;
            cfgo::Track::MsgPtr receive_msg(cfgo::Track::MsgType msg_type);
            auto await_msg(cfgo::Track::MsgType msg_type, close_chan close_ch) -> asio::awaitable<cfgo::Track::MsgPtr>;
            void * get_gst_caps(int pt) const;
            void set_on_data(const OnDataCb & cb);
            void set_on_data(OnDataCb && cb);
            void unset_on_data() noexcept;
            void set_on_stat(const OnStatCb & cb);
            void set_on_stat(OnStatCb && cb);
            void unset_on_stat() noexcept;
            void set_on_close(const OnCloseCb & cb);
            void set_on_close(OnCloseCb && cb);
            void unset_on_close() noexcept;
            std::uint64_t get_rtp_drops_bytes() noexcept;
            std::uint32_t get_rtp_drops_packets() noexcept;
            std::uint64_t get_rtp_receives_bytes() noexcept;
            std::uint32_t get_rtp_receives_packets() noexcept;
            float get_rtp_drop_bytes_rate() noexcept;
            float get_rtp_drop_packets_rate() noexcept;
            std::uint32_t get_rtp_packet_mean_size() noexcept;
            void reset_rtp_data() noexcept;
            std::uint64_t get_rtcp_drops_bytes() noexcept;
            std::uint32_t get_rtcp_drops_packets() noexcept;
            std::uint64_t get_rtcp_receives_bytes() noexcept;
            std::uint32_t get_rtcp_receives_packets() noexcept;
            float get_rtcp_drop_bytes_rate() noexcept;
            float get_rtcp_drop_packets_rate() noexcept;
            std::uint32_t get_rtcp_packet_mean_size() noexcept;
            void reset_rtcp_data() noexcept;
            float get_drop_bytes_rate() noexcept;
            float get_drop_packets_rate() noexcept;
            std::uint16_t get_rtp_cache_size() noexcept;
            std::uint16_t get_rtcp_cache_size() noexcept;
            std::uint16_t get_rtp_cache_capicity() const noexcept;
            std::uint16_t get_rtcp_cache_capicity() const noexcept;
        };
    } // namespace impl
    
} // namespace name

#endif