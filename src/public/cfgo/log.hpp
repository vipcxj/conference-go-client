#ifndef _CFGO_LOG_HPP_
#define _CFGO_LOG_HPP_

#include "cfgo/macros.h"
#include "cfgo/utils.hpp"

#include "spdlog/spdlog.h"

namespace cfgo
{
    namespace detail
    {
        class Log;
    } // namespace detail
    
    using Logger = std::shared_ptr<spdlog::logger>;
    using LogLevel = spdlog::level::level_enum;
    using LogTimeType = spdlog::pattern_time_type;
    using LoggerFactory = std::function<Logger(std::string)>;

    class Log : public cfgo::ImplBy<detail::Log>
    {
    public:
        enum Category {
            DEFAULT,
            CLIENT,
            SIGNAL,
            WEBRTC,
            WEBSOCKET,
            CFGOSRC,
            TRACK
        };
        
        Log();

        static const Log & instance();
        static const char * get_category_name(Category category) noexcept;
        /**
         * create a logger name prefixed with category name -> cfgo::${category}:${name}
         */
        static auto make_logger_name(Category category, const std::string & name) -> std::string {
            if (name.empty())
            {
                return fmt::format("cfgo::{}", get_category_name(category));
            }
            else
            {
                return fmt::format("cfgo::{}:{}", get_category_name(category), name);
            }
        }
        void set_logger_factory(Category category, LoggerFactory factory) const;
        void set_level(Category category, LogLevel level) const;
        LogLevel get_level(Category category) const;
        void set_pattern(Category category, const std::string & pattern, LogTimeType time_type = LogTimeType::local) const;
        std::string get_pattern(Category category) const;
        LogTimeType get_time_type(Category category) const;
        void enable_backtrace(Category category, std::size_t size) const;
        void disable_backtrace(Category category) const;
        bool is_backtrace(Category category) const;
        Logger create_logger(Category category, const std::string & name = "") const;
        Logger default_logger() const;
    };

} // namespace cfgo

#define CFGO_DEFAULT_LOGGER cfgo::Log::instance().default_logger()

#define _CFGO_VARARGS_NUM(...) std::tuple_size<decltype(std::make_tuple(__VA_ARGS__))>::value

#define _CFGO_LOG_LOGGER_LOG(LOGGER, LVL, MTH, FMT, ...) \
do { \
    if (LOGGER->should_log(cfgo::LogLevel::LVL)) \
    { \
        LOGGER->MTH(FMT CFGO_VA_ARGS(__VA_ARGS__)); \
    } \
} while(false)

#define _CFGO_LOG_DEFAULT_LOG(LVL, MTH, FMT, ...) \
do { \
    const auto & logger = CFGO_DEFAULT_LOGGER; \
    if (logger->should_log(cfgo::LogLevel::LVL)) \
    { \
        logger->MTH(FMT CFGO_VA_ARGS(__VA_ARGS__)); \
    } \
} while(false)

#define CFGO_TRACE(FMT, ...) _CFGO_LOG_DEFAULT_LOG(trace, trace, FMT, __VA_ARGS__)
#define CFGO_DEBUG(FMT, ...) _CFGO_LOG_DEFAULT_LOG(debug, debug, FMT, __VA_ARGS__)
#define CFGO_INFO(FMT, ...) _CFGO_LOG_DEFAULT_LOG(info, info, FMT, __VA_ARGS__)
#define CFGO_WARN(FMT, ...) _CFGO_LOG_DEFAULT_LOG(warn, warn, FMT, __VA_ARGS__)
#define CFGO_ERROR(FMT, ...) _CFGO_LOG_DEFAULT_LOG(err, error, FMT, __VA_ARGS__)
#define CFGO_CRITICAL(FMT, ...) _CFGO_LOG_DEFAULT_LOG(critical, critical, FMT, __VA_ARGS__)

#define CFGO_THIS_TRACE(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, trace, trace, FMT, __VA_ARGS__)
#define CFGO_THIS_DEBUG(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, debug, debug, FMT, __VA_ARGS__)
#define CFGO_THIS_INFO(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, info, info, FMT, __VA_ARGS__)
#define CFGO_THIS_WARN(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, warn, warn, FMT, __VA_ARGS__)
#define CFGO_THIS_ERROR(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, err, error, FMT, __VA_ARGS__)
#define CFGO_THIS_CRITICAL(FMT, ...) _CFGO_LOG_LOGGER_LOG(this->m_logger, critical, critical, FMT, __VA_ARGS__)

#define CFGO_SELF_TRACE(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, trace, trace, FMT, __VA_ARGS__)
#define CFGO_SELF_DEBUG(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, debug, debug, FMT, __VA_ARGS__)
#define CFGO_SELF_INFO(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, info, info, FMT, __VA_ARGS__)
#define CFGO_SELF_WARN(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, warn, warn, FMT, __VA_ARGS__)
#define CFGO_SELF_ERROR(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, err, error, FMT, __VA_ARGS__)
#define CFGO_SELF_CRITICAL(FMT, ...) _CFGO_LOG_LOGGER_LOG(self->m_logger, critical, critical, FMT, __VA_ARGS__)

#define CFGO_LOGGER_TRACE(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, trace, trace, FMT, __VA_ARGS__)
#define CFGO_LOGGER_DEBUG(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, debug, debug, FMT, __VA_ARGS__)
#define CFGO_LOGGER_INFO(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, info, info, FMT, __VA_ARGS__)
#define CFGO_LOGGER_WARN(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, warn, warn, FMT, __VA_ARGS__)
#define CFGO_LOGGER_ERROR(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, err, error, FMT, __VA_ARGS__)
#define CFGO_LOGGER_CRITICAL(LOGGER, FMT, ...) _CFGO_LOG_LOGGER_LOG(LOGGER, critical, critical, FMT, __VA_ARGS__)

#endif