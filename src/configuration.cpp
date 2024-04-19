#include "cfgo/configuration.hpp"

namespace cfgo {

    Configuration::Configuration(
        const std::string& signal_url,
        const std::string& token,
        const bool thread_safe
    ):
    m_signal_url(signal_url),
    m_token(token),
    m_rtc_config(),
    m_thread_safe(thread_safe)
    {}

    Configuration::Configuration(
        const std::string& signal_url,
        const std::string& token,
        const rtc::Configuration& rtc_config,
        const bool thread_safe
    ):
    m_signal_url(signal_url),
    m_token(token),
    m_rtc_config(rtc_config),
    m_thread_safe(thread_safe)
    {}
}