#include "cfgo/log.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "cpptrace/cpptrace.hpp"

#include <regex>
#include <unordered_map>

namespace cfgo
{
    namespace detail
    {
        constexpr LogLevel DEFAULT_LOG_LEVEL = LogLevel::info;
        constexpr LogTimeType DEFAULT_LOG_TIME_TYPE = LogTimeType::local;
        constexpr bool DEFAULT_LOG_BACKTRACE = false;
        constexpr std::size_t DEFAULT_LOG_BACKTRACE_SIZE = 4096;

        Logger create_default_logger(std::string name)
        {
            auto logger_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            return std::make_shared<spdlog::logger>(std::move(name), logger_sink);
        }

        class Log
        {
        public:
            using Category = cfgo::Log::Category;
            struct CategoryInfo
            {
                LogLevel level = DEFAULT_LOG_LEVEL;
                bool backtrace = DEFAULT_LOG_BACKTRACE;
                std::size_t backtrace_size = DEFAULT_LOG_BACKTRACE_SIZE;
                std::string pattern;
                LogTimeType time_type = DEFAULT_LOG_TIME_TYPE;
                LoggerFactory factory = create_default_logger;
            };
            using CategoryMap = std::unordered_map<Category, CategoryInfo>;

            Log();

            void set_logger_factory(Category category, LoggerFactory factory);
            void set_level(Category category, LogLevel level);
            LogLevel get_level(Category category) const;
            void set_pattern(Category category, const std::string & pattern, LogTimeType time_type);
            std::string get_pattern(Category category) const;
            LogTimeType get_time_type(Category category) const;
            void enable_backtrace(Category category, std::size_t size);
            void disable_backtrace(Category category);
            bool is_backtrace(Category category) const;
            Logger create_logger(Category category, const std::string & name) const;
            Logger default_logger() const;
        private:
            CategoryMap m_categories;
            Logger m_default_logger;
        };

        Log::Log(): m_default_logger(create_logger(Category::DEFAULT, "")) {}

        void Log::set_logger_factory(Category category, LoggerFactory factory)
        {
            if (category == Category::DEFAULT)
            {
                m_default_logger = create_logger(Category::DEFAULT, "");
            }
            else
            {
                auto [iter, _] = m_categories.try_emplace(category);
                iter->second.factory = std::move(factory);
            }
        }

        void Log::set_level(Category category, LogLevel level)
        {            
            auto [iter, _] = m_categories.try_emplace(category);
            iter->second.level = level;
            if (category == Category::DEFAULT)
            {
                m_default_logger->set_level(level);
            }
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

        void Log::set_pattern(Category category, const std::string & pattern, LogTimeType time_type)
        {
            auto [iter, _] = m_categories.try_emplace(category);
            iter->second.pattern = pattern;
            iter->second.time_type = time_type;
            if (category == Category::DEFAULT)
            {
                m_default_logger->set_pattern(pattern, time_type);
            }
        }

        std::string Log::get_pattern(Category category) const
        {
            auto cat_iter = m_categories.find(category);
            if (cat_iter != m_categories.end())
            {
                return cat_iter->second.pattern;
            }
            else
            {
                return "";
            }
        }

        LogTimeType Log::get_time_type(Category category) const
        {
            auto cat_iter = m_categories.find(category);
            if (cat_iter != m_categories.end())
            {
                return cat_iter->second.time_type;
            }
            else
            {
                return DEFAULT_LOG_TIME_TYPE;
            }
        }

        void Log::enable_backtrace(Category category, std::size_t size)
        {
            auto [iter, _] = m_categories.try_emplace(category);
            iter->second.backtrace = true;
            iter->second.backtrace_size = size;
            if (category == Category::DEFAULT)
            {
                m_default_logger->enable_backtrace(size);
            }
        }

        void Log::disable_backtrace(Category category)
        {
            auto [iter, _] = m_categories.try_emplace(category);
            iter->second.backtrace = false;
            iter->second.backtrace_size = DEFAULT_LOG_BACKTRACE_SIZE;
            if (category == Category::DEFAULT)
            {
                m_default_logger->disable_backtrace();
            }
        }

        bool Log::is_backtrace(Category category) const
        {
            auto cat_iter = m_categories.find(category);
            if (cat_iter != m_categories.end())
            {
                return cat_iter->second.backtrace;
            }
            else
            {
                return DEFAULT_LOG_BACKTRACE;
            }
        }

        std::shared_ptr<spdlog::logger> Log::create_logger(Category category, const std::string & name) const
        {
            std::string logger_name;
            if (name.empty())
            {
                logger_name = fmt::format("cfgo::{}", cfgo::Log::get_category_name(category));
            }
            else if (category == Category::DEFAULT)
            {
                logger_name = "cfgo";
            }
            else
            {
                logger_name = name;
            }
            auto cat_iter = m_categories.find(category);
            if (cat_iter != m_categories.end())
            {
                auto & cat = cat_iter->second;
                auto logger = cat.factory(logger_name);
                logger->set_level(cat.level);
                if (!cat.pattern.empty())
                {
                    logger->set_pattern(cat.pattern, cat.time_type);
                }
                if (cat.backtrace)
                {
                    logger->enable_backtrace(cat.backtrace_size);
                }
                else
                {
                    logger->disable_backtrace();
                }
                return logger;
            }
            else
            {
                auto logger = create_default_logger(logger_name);
                logger->set_level(DEFAULT_LOG_LEVEL);
                if (DEFAULT_LOG_BACKTRACE)
                {
                    logger->enable_backtrace(DEFAULT_LOG_BACKTRACE_SIZE);
                }
                else
                {
                    logger->disable_backtrace();
                }
                return logger;
            }
        }

        Logger Log::default_logger() const
        {
            return m_default_logger;
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
        case DEFAULT:
            return "default";
        case CLIENT:
            return "client";
        case CFGOSRC:
            return "gst::src";
        case TRACK:
            return "track";
        default:
            return "unknown";
        }
    }

    void Log::set_logger_factory(Category category, LoggerFactory factory) const
    {
        impl()->set_logger_factory(category, std::move(factory));
    }

    void Log::set_level(Category category, LogLevel level) const
    {
        impl()->set_level(category, level);
    }

    auto Log::get_level(Category category) const -> LogLevel
    {
        return impl()->get_level(category);
    }

    void Log::set_pattern(Category category, const std::string & pattern, LogTimeType time_type) const
    {
        impl()->set_pattern(category, pattern, time_type);
    }

    std::string Log::get_pattern(Category category) const
    {
        return impl()->get_pattern(category);
    }

    LogTimeType Log::get_time_type(Category category) const
    {
        return impl()->get_time_type(category);
    }

    void Log::enable_backtrace(Category category, std::size_t size) const
    {
        impl()->enable_backtrace(category, size);
    }

    void Log::disable_backtrace(Category category) const
    {
        impl()->disable_backtrace(category);
    }

    bool Log::is_backtrace(Category category) const
    {
        return impl()->is_backtrace(category);
    }

    std::shared_ptr<spdlog::logger> Log::create_logger(Category category, const std::string & name) const
    {
        return impl()->create_logger(category, name);
    }

    Logger Log::default_logger() const
    {
        return impl()->default_logger();
    }

} // namespace cfgo
