#ifndef _CFGO_CONFIGURATION_HPP_
#define _CFGO_CONFIGURATION_HPP_

#include "cfgo/alias.hpp"
#include "rtc/rtc.hpp"

namespace cfgo {

    struct SignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout = std::chrono::seconds {10};
        duration_t ack_timeout = std::chrono::seconds {10};
    };

    constexpr std::int32_t DEFAULT_RTP_CACHE_MIN_SEGMENTS = 2;
    constexpr std::int32_t DEFAULT_RTP_CACHE_MAX_SEGMENTS = 8;
    constexpr std::int32_t DEFAULT_RTP_CACHE_SEGMENT_CAPICITY = 16;
    constexpr std::int32_t DEFAULT_RTCP_CACHE_MIN_SEGMENTS = 1;
    constexpr std::int32_t DEFAULT_RTCP_CACHE_MAX_SEGMENTS = 4;
    constexpr std::int32_t DEFAULT_RTCP_CACHE_SEGMENT_CAPICITY = 16;
    struct TrackConfigure {
        std::int32_t rtp_cache_min_segments = DEFAULT_RTP_CACHE_MIN_SEGMENTS;
        std::int32_t rtp_cache_max_segments = DEFAULT_RTP_CACHE_MAX_SEGMENTS;
        std::int32_t rtp_cache_segment_capicity = DEFAULT_RTP_CACHE_SEGMENT_CAPICITY;
        std::int32_t rtcp_cache_min_segments = DEFAULT_RTCP_CACHE_MIN_SEGMENTS;
        std::int32_t rtcp_cache_max_segments = DEFAULT_RTCP_CACHE_MAX_SEGMENTS;
        std::int32_t rtcp_cache_segment_capicity = DEFAULT_RTCP_CACHE_SEGMENT_CAPICITY;
    };

    struct Configuration
    {
        SignalConfigure m_signal_config;
        ::rtc::Configuration m_rtc_config;
        TrackConfigure m_track_config;
        const bool m_thread_safe {false};

        Configuration(
            const SignalConfigure & signal_config,
            const ::rtc::Configuration & rtc_config,
            const TrackConfigure & track_config,
            const bool thread_safe = false
        ): m_signal_config(signal_config), m_rtc_config(rtc_config), m_track_config(track_config) {}
    };
    
}

#endif