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

    struct Configuration
    {
        SignalConfigure m_signal_config;
        ::rtc::Configuration m_rtc_config;
        const bool m_thread_safe {false};

        Configuration(
            const SignalConfigure & signal_config,
            const ::rtc::Configuration & rtc_config,
            const bool thread_safe = false
        ): m_signal_config(signal_config), m_rtc_config(rtc_config) {}
    };
    
}

#endif