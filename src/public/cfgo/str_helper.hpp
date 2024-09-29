#ifndef _CFGO_STR_HELPER_HPP_
#define _CFGO_STR_HELPER_HPP_

#include <string>
#include <sstream>

namespace cfgo
{
    template <typename Container, typename Delimiter, typename Mapper = std::nullptr_t>
    std::string str_join(const Container &c, const Delimiter &delimiter, const Mapper &mapper = nullptr)
    {
        std::stringstream ss;
        auto s = std::size(c);
        int i = 0;
        for (auto &&e : c)
        {
            if constexpr (std::is_null_pointer_v<Mapper>)
            {
                ss << e;
            }
            else
            {
                ss << mapper(e, i);
            }
            if (i < s - 1)
            {
                ss << delimiter;
            }
            ++i;
        }
        return ss.str();
    }

} // namespace cfgo

#endif