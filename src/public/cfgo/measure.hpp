#ifndef _CFGO_MEASURE_H_
#define _CFGO_MEASURE_H_

#include <chrono>
#include <vector>
#include <list>
#include <cassert>
#include <concepts>
#include <algorithm>
#include <unordered_map>
#include <ranges>

#include "cfgo/str_helper.hpp"

namespace cfgo
{

    template<typename T>
    concept Measureable = requires() {
        { T{0} };
    } && requires(T a, T b) {
        { a < b } -> std::same_as<bool>;
    };
    template<typename T>
    concept Meanable = requires(T a, std::size_t n) {
        { a / n } -> std::convertible_to<T>;
    };

    template<typename T, typename Derve>
    class BaseMeasure
    {
    public:
        using Vec = std::vector<T>;
        using List = std::list<T>;
        using Size = Vec::size_type;
    private:
        Vec m_maxes;
        Vec m_mines;
        List m_latests;
        T m_sum {0};
        std::size_t m_num {0};
        Size m_capacity;
    public:
        BaseMeasure(Size capacity): m_capacity(capacity)
        {
            assert(m_capacity > 0);
        };
        virtual ~BaseMeasure() = default;
        void update(const T & value)
        {
            m_sum = m_sum + value;
            ++ m_num;
            m_maxes.push_back(value);
            std::sort(m_maxes.rbegin(), m_maxes.rend());
            if (m_maxes.size() == m_capacity + 1)
            {
                m_maxes.pop_back();
            }
            m_mines.push_back(value);
            std::sort(m_mines.begin(), m_mines.end());
            if (m_mines.size() == m_capacity + 1)
            {
                m_mines.pop_back();
            }
            m_latests.push_front(value);
            if (m_latests.size() == m_capacity + 1)
            {
                m_latests.pop_back();
            }
        }

        const Vec & max_vec() const noexcept
        {
            return m_maxes;
        }
        std::optional<T> max_n(int i) const
        {
            return i < m_maxes.size() ? std::make_optional(m_maxes[i]) : std::nullopt;
        }
        const Vec & min_vec() const noexcept
        {
            return m_mines;
        }
        std::optional<T> min_n(int i) const
        {
            return i < m_mines.size() ? std::make_optional(m_mines[i]) : std::nullopt;
        }
        const List & latest_list() const noexcept
        {
            return m_latests;
        }
        std::optional<T> latest_n(int i) const
        {
            auto j = 0;
            for (auto & latest : m_latests)
            {
                if (j++ == i)
                {
                    return latest;
                }
            }
            return std::nullopt;
        }
        T sum() const noexcept
        {
            return m_sum;
        }
        std::size_t num() const noexcept
        {
            return m_num;
        }
        std::optional<T> max() const
        {
            return max_n(0);
        }
        std::optional<T> min() const
        {
            return min_n(0);
        }
        std::optional<T> mean() const noexcept requires Meanable<T>
        {
            return m_maxes.empty() ? std::nullopt : (m_sum / m_num);
        }
        std::optional<T> latest() const {
            return latest_n(0);
        }
        template<typename F>
        requires requires(F f, const Derve & me) {
            f(me);
        }
        void run_per_n(int n, F fun) const noexcept(noexcept(fun(dynamic_cast<const Derve &>(*this))))
        {
            if (m_num % n == 0)
            {
                fun(dynamic_cast<const Derve &>(*this));
            }
        }
        template<typename F>
        requires requires(F f, const Derve & me) {
            f(me);
        }
        void run_greater_than(T v, F fun) const noexcept(noexcept(fun(dynamic_cast<const Derve &>(*this))))
        {
            if (!m_latests.empty() && m_latests.back() > v)
            {
                fun(dynamic_cast<const Derve &>(*this));
            }
        }
    };

    using HighDuration = std::chrono::high_resolution_clock::duration;
    using HighTimePoint = std::chrono::high_resolution_clock::time_point;

    inline int cast_ms(HighDuration dur) {
        using namespace std::chrono;
        return duration_cast<milliseconds>(dur).count();
    }

    inline int cast_ms(std::optional<HighDuration> dur) {
        using namespace std::chrono;
        return dur ? duration_cast<milliseconds>(*dur).count() : 0;
    }
    
    class DurationMeasure : public BaseMeasure<HighDuration, DurationMeasure>
    {
    private:
        HighTimePoint m_start;
    public:
        DurationMeasure(DurationMeasure::Size capacity): BaseMeasure<HighDuration, DurationMeasure>(capacity) {}
        ~DurationMeasure() = default;
        void mark_start() noexcept
        {
            m_start = std::chrono::high_resolution_clock::now();
        }
        void mark_end()
        {
            update(std::chrono::high_resolution_clock::now() - m_start);
        }
        std::string max_vec_string() const
        {
            return str_join(max_vec(), ", ", [](const HighDuration & dur, int i) -> std::string {
                return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()) + " ms";
            });
        }
        std::string min_vec_string() const
        {
            return str_join(min_vec(), ", ", [](const HighDuration & dur, int i) -> std::string {
                return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()) + " ms";
            });
        }
        std::string latest_list_string() const
        {
            return str_join(latest_list(), ", ", [](const HighDuration & dur, int i) -> std::string {
                return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()) + " ms";
            });
        }
    };

    class DurationMeasures
    {
    private:
        std::unordered_map<std::string, DurationMeasure> m_measures;
        DurationMeasure::Size m_capacity;
    public:
        DurationMeasures(DurationMeasure::Size capacity): m_capacity(capacity) {}
        DurationMeasure & measure(std::string name, DurationMeasure::Size capacity = 0) {
            auto [iter, _] = m_measures.try_emplace(std::move(name), capacity > 0 ? capacity : m_capacity);
            return iter->second;
        }

        std::optional<HighDuration> max_n(const std::string & name, int i) const
        {
            auto iter = m_measures.find(name);
            return iter != m_measures.end() ? iter->second.max_n(i) : std::nullopt;
        }

        HighDuration max_all_n(int i) const
        {
            HighDuration sum {0};
            for (auto [_, m] : m_measures)
            {
                auto v = m.max_n(i);
                if (v)
                {
                    sum += *v;
                }
            }
            return sum;
        }

        float max_percent_n(const std::string & name, int i) const
        {
            auto a = max_all_n(i);
            if (a.count() == 0)
            {
                return 0.0f;
            }
            auto v = max_n(name, i);
            return v ? v->count() * 1.0f / a.count() : 0.0f;
        }

        std::optional<HighDuration> min_n(const std::string & name, int i) const
        {
            auto iter = m_measures.find(name);
            return iter != m_measures.end() ? iter->second.min_n(i) : std::nullopt;
        }

        HighDuration min_all_n(int i) const
        {
            HighDuration sum {0};
            for (auto [_, m] : m_measures)
            {
                auto v = m.min_n(i);
                if (v)
                {
                    sum += *v;
                }
            }
            return sum;
        }

        float min_percent_n(const std::string & name, int i) const
        {
            auto a = min_all_n(i);
            if (a.count() == 0)
            {
                return 0.0f;
            }
            auto v = min_n(name, i);
            return v ? v->count() * 1.0f / a.count() : 0.0f;
        }

        std::optional<HighDuration> latest_n(const std::string & name, int i) const
        {
            auto iter = m_measures.find(name);
            return iter != m_measures.end() ? iter->second.latest_n(i) : std::nullopt;
        }

        HighDuration latest_all_n(int i) const
        {
            HighDuration sum {0};
            for (auto [_, m] : m_measures)
            {
                auto v = m.latest_n(i);
                if (v)
                {
                    sum += *v;
                }
            }
            return sum;
        }

        float latest_percent_n(const std::string & name, int i) const
        {
            auto a = latest_all_n(i);
            if (a.count() == 0)
            {
                return 0.0f;
            }
            auto v = latest_n(name, i);
            return v ? v->count() * 1.0f / a.count() : 0.0f;
        }

        std::optional<HighDuration> max(const std::string & name) const
        {
            return max_n(name, 0);
        }

        std::optional<HighDuration> min(const std::string & name) const
        {
            return min_n(name, 0);
        }

        std::optional<HighDuration> latest(const std::string & name) const
        {
            return latest_n(name, 0);
        }

        std::string max_ns_string(const std::string & name) const
        {
            auto iter = m_measures.find(name);
            if (iter == m_measures.end())
            {
                return "";
            }
            using namespace std::chrono;
            return str_join(std::views::iota(0, (int) iter->second.max_vec().size()), ", ", [&](int n, int i) -> std::string {
                return fmt::format("{} ms ({:.0f}%)", duration_cast<milliseconds>(*max_n(name, n)).count(), max_percent_n(name, n));;
            });
        }

        std::string min_ns_string(const std::string & name) const
        {
            auto iter = m_measures.find(name);
            if (iter == m_measures.end())
            {
                return "";
            }
            using namespace std::chrono;
            return str_join(std::views::iota(0, (int) iter->second.min_vec().size()), ", ", [&](int n, int i) -> std::string {
                return fmt::format("{} ms ({:.0f}%)", duration_cast<milliseconds>(*min_n(name, n)).count(), min_percent_n(name, n));;
            });
        }

        std::string latest_ns_string(const std::string & name) const
        {
            auto iter = m_measures.find(name);
            if (iter == m_measures.end())
            {
                return "";
            }
            using namespace std::chrono;
            return str_join(std::views::iota(0, (int) iter->second.latest_list().size()), ", ", [&](int n, int i) -> std::string {
                return fmt::format("{} ms ({:.0f}%)", duration_cast<milliseconds>(*latest_n(name, n)).count(), latest_percent_n(name, n));;
            });
        }

        // just for argument of noexcept operator, not really use this function.
        const DurationMeasure & __return_const_measure_ref() const noexcept {
            assert(false);
            return m_measures.find("")->second;
        }

        template<typename F>
        requires requires(F f, const DurationMeasures & me, const DurationMeasure & m) {
            f(me, m);
        }
        void run_per_n(const std::string & name, int n, F fun) const noexcept(noexcept(fun(*this, __return_const_measure_ref())))
        {
            auto iter = m_measures.find(name);
            if (iter == m_measures.end())
            {
                return;
            }
            if (iter->second.num() % n == 0)
            {
                fun(*this, iter->second);
            }
        }

        template<typename F>
        requires requires(F f, const DurationMeasures & me, const DurationMeasure & m) {
            f(me, m);
        }
        void run_greater_than(const std::string & name, HighDuration v, F fun) const noexcept(noexcept(fun(*this, __return_const_measure_ref())))
        {
            auto iter = m_measures.find(name);
            if (iter == m_measures.end())
            {
                return;
            }
            auto latest = iter->second.latest();
            if (latest && *latest > v)
            {
                fun(*this, iter->second);
            }
        }
    };

    class ScopeDurationMeasurer
    {
    private:
        DurationMeasure & m_measure;
    public:
        ScopeDurationMeasurer(DurationMeasure & measure): m_measure(measure) {
            m_measure.mark_start();
        }
        ~ScopeDurationMeasurer()
        {
            m_measure.mark_end();
        }
    };
    
} // namespace cfgo


#endif