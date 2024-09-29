#ifndef _CFGO_MEASURE_H_
#define _CFGO_MEASURE_H_

#include <chrono>
#include <vector>
#include <list>
#include <cassert>
#include <concepts>
#include <algorithm>

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

    template<typename T>
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
        ~BaseMeasure() = default;
        void update(const T & value)
        {
            m_sum = m_sum + value;
            ++ m_num;
            m_maxes.push_back(value);
            std::sort(m_maxes.rbegin(), m_maxes.rend());
            if (m_maxes.size() == m_capacity)
            {
                m_maxes.pop_back();
            }
            m_mines.push_back(value);
            std::sort(m_mines.begin(), m_mines.end());
            if (m_mines.size() == m_capacity)
            {
                m_mines.pop_back();
            }
            m_latests.push_back(value);
            if (m_latests.size() == m_capacity)
            {
                m_latests.push_front();
            }
        }

        const Vec & max_vec() const noexcept
        {
            return m_maxes;
        }
        const Vec & min_vec() const noexcept
        {
            return m_mines;
        }
        const List & latest_list() const noexcept
        {
            return m_latests;
        }
        T sum() const noexcept
        {
            return m_sum;
        }
        std::size_t num() const noexcept
        {
            return m_num;
        }
        std::optional<T> max() const noexcept
        {
            if (m_maxes.empty())
            {
                return std::nullopt;
            }
            else
            {
                return m_maxes[0];
            }
        }
        std::optional<T> min() const noexcept
        {
            if (m_mines.empty())
            {
                return std::nullopt;
            }
            else
            {
                return m_mines[0];
            }
        }
        std::optional<T> mean() const noexcept requires Meanable<T>
        {
            if (m_maxes.empty())
            {
                return std::nullopt;
            }
            else
            {
                return m_sum / m_num;
            }
        }
    };

    using HighDuration = std::chrono::high_resolution_clock::duration;
    using HighTimePoint = std::chrono::high_resolution_clock::time_point;
    
    class DurationMeasure : public BaseMeasure<HighDuration>
    {
    private:
        /* data */
    public:
        DurationMeasure(DurationMeasure::Size capacity): BaseMeasure<HighDuration>(capacity) {}
        ~DurationMeasure() = default;
    };
    
} // namespace cfgo


#endif