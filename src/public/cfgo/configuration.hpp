#ifndef _CFGO_CONFIGURATION_HPP_
#define _CFGO_CONFIGURATION_HPP_

#include "rtc/rtc.hpp"
#include "asio/io_context.hpp"

namespace cfgo {
    struct Configuration
    {
        const std::string m_signal_url;
        const std::string m_token;
        const ::rtc::Configuration m_rtc_config;
        const bool m_thread_safe;

        Configuration(
            const std::string& signal_url,
            const std::string& token,
            const bool thread_safe = false
        );

        Configuration(
            const std::string& signal_url,
            const std::string& token,
            const ::rtc::Configuration& rtc_config,
            const bool thread_safe = false
        );
    };
    
}

#endif