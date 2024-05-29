#ifndef _CFGO_IMP_TRACK_HPP_
#define _CFGO_IMP_TRACK_HPP_

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include "cfgo/config/configuration.h"
#include "cfgo/track.hpp"
#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
#include "impl/client.hpp"
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
            using OnDataCbMoveOnly = cfgo::Track::OnDataCbMoveOnly;
            using OnStatCb = cfgo::Track::OnStatCb;
            using OnStatCbMoveOnly = cfgo::Track::OnStatCbMoveOnly;
            using Statistics = cfgo::Track::Statistics;
            
            std::string type;
            std::string pubId;
            std::string globalId;
            std::string bindId;
            std::string rid;
            std::string streamId;
            std::map<std::string, std::string> labels;
            std::shared_ptr<rtc::Track> track;

            bool m_inited;
            Logger m_logger;
            mutex m_lock;
            MsgBuffer m_rtp_cache;
            MsgBuffer m_rtcp_cache;
            uint32_t m_seq;
            OnDataCbMoveOnly m_on_data = nullptr;
            Statistics m_statistics;
            OnStatCbMoveOnly m_on_stat = nullptr;
            std::shared_ptr<Client> m_client;
            asiochan::channel<void, 1> m_msg_notify;
            asiochan::channel<void, 1> m_open_notify;
            asiochan::channel<void, 1> m_closed_notify;
            #ifdef CFGO_SUPPORT_GSTREAMER
            GstSDPMedia *m_gst_media;
            #endif

            Track(const msg_ptr& msg, int cache_capicity);
            ~Track();

            uint32_t makesure_min_seq();
            void prepare_track();
            void on_track_msg(rtc::binary data);
            void on_track_open();
            void on_track_closed();
            void on_track_error(std::string error);
            auto await_open_or_closed(close_chan close_ch) -> asio::awaitable<bool>;
            cfgo::Track::MsgPtr receive_msg(cfgo::Track::MsgType msg_type);
            auto await_msg(cfgo::Track::MsgType msg_type, close_chan close_ch) -> asio::awaitable<cfgo::Track::MsgPtr>;
            void bind_client(std::shared_ptr<Client> client);
            void * get_gst_caps(int pt) const;
            void set_on_data(const OnDataCb & cb);
            void set_on_data(OnDataCbMoveOnly && cb);
            void unset_on_data() noexcept;
            void set_on_stat(const OnStatCb & cb);
            void set_on_stat(OnStatCbMoveOnly && cb);
            void unset_on_stat() noexcept;
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
        };
    } // namespace impl
    
} // namespace name

#endif