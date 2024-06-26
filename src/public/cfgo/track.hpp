#ifndef _CFGO_TRACK_HPP_
#define _CFGO_TRACK_HPP_

#include <string>
#include <memory>
#include "cfgo/config/configuration.h"
#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "rtc/track.hpp"
#include "asio/awaitable.hpp"

namespace rtc
{
    struct Track;
} // namespace rtc

namespace cfgo
{
    namespace impl
    {
        struct Track;
        struct Client;
    } // namespace impl
    
    constexpr int DEFAULT_TRACK_CACHE_CAPICITY = 16;
    
    struct Track : ImplBy<impl::Track>
    {
        struct Statistics
        {
            std::uint64_t m_rtp_drops_bytes = 0;
            std::uint32_t m_rtp_drops_packets = 0;
            std::uint64_t m_rtp_receives_bytes = 0;
            std::uint32_t m_rtp_receives_packets = 0;
            std::uint64_t m_rtcp_drops_bytes = 0;
            std::uint32_t m_rtcp_drops_packets = 0;
            std::uint64_t m_rtcp_receives_bytes = 0;
            std::uint32_t m_rtcp_receives_packets = 0;

            inline float rtp_drop_bytes_rate() const noexcept
            {
                if (m_rtp_receives_bytes > 0)
                {
                    return 1.0f * m_rtp_drops_bytes / m_rtp_receives_bytes;
                }
                else
                {
                    return 0.0f;
                }
            }

            inline double rtp_drop_packets_rate() const noexcept
            {
                if (m_rtp_receives_packets > 0)
                {
                    return 1.0f * m_rtp_drops_packets / m_rtp_receives_packets;
                }
                else
                {
                    return 0.0f;
                }
            }

            inline std::uint32_t rtp_packet_mean_size() const noexcept
            {
                if (m_rtp_receives_packets > 0)
                {
                    return static_cast<std::uint32_t>(m_rtp_receives_bytes / m_rtp_receives_packets);
                }
                else
                {
                    return 0;
                }
            }

            inline float rtcp_drop_bytes_rate() const noexcept
            {
                if (m_rtcp_receives_bytes > 0)
                {
                    return 1.0f * m_rtcp_drops_bytes / m_rtcp_receives_bytes;
                }
                else
                {
                    return 0.0f;
                }
            }

            inline float rtcp_drop_packets_rate() const noexcept
            {
                if (m_rtcp_receives_packets > 0)
                {
                    return 1.0f * m_rtcp_drops_packets / m_rtcp_receives_packets;
                }
                else
                {
                    return 0.0f;
                }
            }

            inline std::uint32_t rtcp_packet_mean_size() const noexcept
            {
                if (m_rtcp_receives_packets > 0)
                {
                    return static_cast<std::uint32_t>(m_rtcp_receives_bytes / m_rtcp_receives_packets);
                }
                else
                {
                    return 0;
                }
            }

            inline float drop_bytes_rate() const noexcept
            {
                if (m_rtp_receives_bytes > 0 || m_rtcp_receives_bytes > 0)
                {
                    return 1.0f * (m_rtp_drops_bytes + m_rtcp_drops_bytes) 
                        / (m_rtp_receives_bytes + m_rtcp_receives_bytes);
                }
                else
                {
                    return 0.0f;
                }
            }

            float drop_packets_rate() const noexcept
            {
                if (m_rtp_receives_packets > 0 || m_rtcp_receives_packets > 0)
                {
                    return 1.0f * (m_rtp_drops_packets + m_rtcp_drops_packets) 
                        / (m_rtp_receives_packets + m_rtcp_receives_packets);
                }
                else
                {
                    return 0.0f;
                }
            }
        };

        using Ptr = std::shared_ptr<Track>;
        using MsgPtr = std::unique_ptr<rtc::binary>;
        using MsgSharedPtr = std::shared_ptr<rtc::binary>;
        using OnDataCb = std::function<void(const rtc::binary &, bool)>;
        using OnStatCb = std::function<void(const Statistics &)>;
        enum MsgType
        {
            RTP,
            RTCP,
            ALL
        };
        Track(std::nullptr_t);
        Track(const msg_ptr & msg, int cache_capicity = DEFAULT_TRACK_CACHE_CAPICITY);

        const std::string& type() const noexcept;
        const std::string& pub_id() const noexcept;
        const std::string& global_id() const noexcept;
        const std::string& bind_id() const noexcept;
        const std::string& rid() const noexcept;
        const std::string& stream_id() const noexcept;
        std::map<std::string, std::string> & labels() noexcept;
        const std::map<std::string, std::string> & labels() const noexcept;
        std::shared_ptr<rtc::Track> & track() noexcept;
        const std::shared_ptr<rtc::Track> & track() const noexcept;
        void * get_gst_caps(int pt) const;
        void set_on_data(const OnDataCb & cb) const;
        void set_on_data(OnDataCb && cb) const;
        void unset_on_data() const noexcept;
        void set_on_stat(const OnStatCb & cb) const;
        void set_on_stat(OnStatCb && cb) const;
        void unset_on_stat() const noexcept;
        /**
         * wait until track open or closed. return false if close_ch is closed.
        */
        auto await_open_or_closed(const close_chan &  close_ch = INVALID_CLOSE_CHAN) const -> asio::awaitable<bool>;
        /**
         * wait until a msg is available. return nullptr when close_ch is closed or track is closed.
        */
        auto await_msg(MsgType msg_type, const close_chan &  close_ch = INVALID_CLOSE_CHAN) const -> asio::awaitable<MsgPtr>;
        /**
         * immediately return a msg or nullptr if no msg available.
        */
        MsgPtr receive_msg(MsgType msg_type) const;

        std::uint64_t get_rtp_drops_bytes() const noexcept;
        std::uint32_t get_rtp_drops_packets() const noexcept;
        std::uint64_t get_rtp_receives_bytes() const noexcept;
        std::uint32_t get_rtp_receives_packets() const noexcept;
        float get_rtp_drop_bytes_rate() const noexcept;
        float get_rtp_drop_packets_rate() const noexcept;
        std::uint32_t get_rtp_packet_mean_size() const noexcept;
        void reset_rtp_data() const noexcept;
        std::uint64_t get_rtcp_drops_bytes() const noexcept;
        std::uint32_t get_rtcp_drops_packets() const noexcept;
        std::uint64_t get_rtcp_receives_bytes() const noexcept;
        std::uint32_t get_rtcp_receives_packets() const noexcept;
        float get_rtcp_drop_bytes_rate() const noexcept;
        float get_rtcp_drop_packets_rate() const noexcept;
        std::uint32_t get_rtcp_packet_mean_size() const noexcept;
        void reset_rtcp_data() const noexcept;
        float get_drop_bytes_rate() const noexcept;
        float get_drop_packets_rate() const noexcept;

        friend class impl::Client;
    };
    
} // namespace name

#endif