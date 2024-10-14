#ifndef _CFGO_ALGORITHM_HPP_
#define _CFGO_ALGORITHM_HPP_

#include <functional>
#include <algorithm>
#include <ranges>

namespace cfgo
{
    /**
     * @brief   Return a vector of iterators to the n largest elements of the given
     *          range.
     * 
     * @tparam  R 
     *          The type of range.
     * @param   range 
     *          The input range to find the largest elements in.
     * @param   n 
     *          The number of largest elements to return.
     * @param   compare
     *          function to compare two iterators,
     *          bool(std::ranges::borrowed_iterator_t<R> iter1, std::ranges::borrowed_iterator_t<R> iter2)
     * 
     * @return  Vector containing iterators to the n largest elements of the range.
     */
    template <std::ranges::forward_range R, typename Cmp = std::ranges::greater>
    constexpr std::vector<std::ranges::borrowed_iterator_t<R>>
    max_n_elements(R &&range, size_t n, Cmp compare) {
        // The iterator type of the input range
        using iter_t = std::ranges::borrowed_iterator_t<R>;
        // The range of iterators over the input range
        auto iterators = std::views::iota(std::ranges::begin(range), 
                                        std::ranges::end(range));
        // Vector of iterators to the largest n elements
        std::vector<iter_t> result(n);
        // Sort the largest n elements of the input range, and store iterators to 
        // these elements to the result vector
        std::ranges::partial_sort_copy(iterators, result, compare);
        return result;
    }
} // namespace cfgo


#endif