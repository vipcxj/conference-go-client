#include "cfgo/log.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "cpptrace/cpptrace.hpp"

#include <regex>
#include <unordered_map>

namespace cfgo
{
    namespace detail
    {
        constexpr LogLevel DEFAULT_LOG_LEVEL = spdlog::level::level_enum::info;

        class Log
        {
        public:
            using Category = cfgo::Log::Category;
            struct CategoryInfo
            {
                LogLevel level = DEFAULT_LOG_LEVEL;
            };
            using CategoryMap = std::unordered_map<Category, CategoryInfo>;

            void set_level(Category category, LogLevel level);
            LogLevel get_level(Category category) const;
            Logger create_logger(Category category, const std::string & name) const;
        private:
            CategoryMap m_categories;
        };

        void Log::set_level(Category category, LogLevel level)
        {
            auto [iter, _] = m_categories.try_emplace(category);
            iter->second.level = level;
        }

        LogLevel Log::get_level(Category category) const
        {
            auto cat_iter = m_categories.find(category);
            if (cat_iter != m_categories.end())
            {
                return cat_iter->second.level;
            }
            else
            {
                return DEFAULT_LOG_LEVEL;
            }
        }

        std::shared_ptr<spdlog::logger> Log::create_logger(Category category, const std::string & name) const
        {
            auto logger_name = name.empty() ? cfgo::Log::get_category_name(category) : name;
            auto logger = spdlog::stdout_color_mt(logger_name);
            logger->set_level(get_level(category));
            return logger;
        }
         
    } // namespace detail

    Log::Log(): ImplBy() {}

    const Log & Log::instance()
    {
        static Log log {};
        return log;
    }

    const char * Log::get_category_name(Category category) noexcept
    {
        switch (category)
        {
        case CLIENT:
            return "client";
        
        default:
            return "unknown";
        }
    }

    void Log::set_level(Category category, LogLevel level) const
    {
        impl()->set_level(category, level);
    }

    auto Log::get_level(Category category) const noexcept -> LogLevel
    {
        return impl()->get_level(category);
    }

    std::shared_ptr<spdlog::logger> Log::create_logger(Category category, const std::string & name) const
    {
        return impl()->create_logger(category, name);
    }

} // namespace cfgo
