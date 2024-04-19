#ifndef _CFGO_COMMON_HPP_
#define _CFGO_COMMON_HPP_

#include <cstdint>
#include <string>

namespace cfgo
{
    struct TryOption
    {
        std::int32_t m_tries;
        std::uint64_t m_delay_init;
        std::uint32_t m_delay_step;
        std::uint32_t m_delay_level;
    };
} // namespace cfgo


#endif