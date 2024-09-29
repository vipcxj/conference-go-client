#ifndef _CFGO_MEASURE_H_
#define _CFGO_MEASURE_H_

#include <chrono>
#include <vector>

namespace cfgo
{
    template<typename T>
    class BaseMeasure
    {
    private:
        std::vector<T> m_maxes;
        std::vector<T> m_mines;
        std::vector<T> m_latests;
        T m_sum;
        T m_num;
        T m_capacity;
    public:
        TimeMeasure();
        ~TimeMeasure();
        void update(const T & value);

        const std::vector<T> & max_vec() const noexcept;
        const std::vector<T> & min_vec() const noexcept;
        const std::vector<T> & latest_vec() const noexcept;
        T sum() const noexcept;
        std::uint64_t num() const noexcept;
        T max() const noexcept;
        T min() const noexcept;
        T mean() const noexcept; 
    };
    

} // namespace cfgo


#endif