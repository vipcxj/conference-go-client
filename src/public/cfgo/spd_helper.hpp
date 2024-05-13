#ifndef _CFGO_SPD_HELPER_HPP_
#define _CFGO_SPD_HELPER_HPP_

#include <optional>
#include "spdlog/spdlog.h"
#include "sio_message.h"
#include "cfgo/sio_helper.hpp"

template<typename T>
struct fmt::formatter<std::optional<T>> : fmt::formatter<std::string>
{
    auto format(const std::optional<T>& my, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        if (my)
        {
            return fmt::format_to(ctx.out(), "{}", my.value());
        }
        else
        {
            return fmt::format_to(ctx.out(), "nullopt");
        }
    }
};

template<>
struct fmt::formatter<sio::message::ptr> : fmt::formatter<std::string>
{
    auto format(const sio::message::ptr& my, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        if (my)
        {
            return fmt::format_to(ctx.out(), "{}", cfgo::sio_msg_to_str(my));
        }
        else
        {
            return fmt::format_to(ctx.out(), "nullopt");
        }
    }
};

#endif