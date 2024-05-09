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

    class Log : public cfgo::ImplBy<detail::Log>
    {
    public:
        enum Category {
            CLIENT,
            CFGOSRC
        };
        
        Log();

        static const Log & instance();
        static const char * get_category_name(Category category) noexcept;
        void set_level(Category category, LogLevel level) const;
        LogLevel get_level(Category category) const noexcept;
        Logger create_logger(Category category, const std::string & name = "") const;
    };

} // namespace cfgo


#endif