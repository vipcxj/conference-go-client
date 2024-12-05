#ifndef _CFGO_JSON_FMT_HPP_
#define _CFGO_JSON_FMT_HPP_

#include "cfgo/fmt.hpp"
#include "nlohmann/json.hpp"

template<>
struct fmt::formatter<nlohmann::json> : fmt::formatter<std::string>
{
    auto format(const nlohmann::json & me, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", nlohmann::to_string(me));
    }
};

#endif