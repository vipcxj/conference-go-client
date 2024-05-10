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
            CFGOSRC,
            TRACK
        };
        
        Log();

        static const Log & instance();
        static const char * get_category_name(Category category) noexcept;
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


#endif