#ifndef _CFGO_FMT_HELPER_HPP_
#define _CFGO_FMT_HELPER_HPP_

#include "cfgo/fmt.hpp"

#include <concepts>
#include <thread>
#include <sstream>
#include "cfgo/track.hpp"
#include "cfgo/pattern.hpp"

namespace cfgo
{
    template<typename T>
    concept nthable = std::integral<T> && requires(T t) {
        { t % 10 } -> std::integral;
    };

    template<nthable T>
    struct Nth {
        T n;

        const char * postfix() const noexcept
        {
            auto t0 = n % 10;
            switch (t0)
            {
            case 1:
                return "st";
            case 2:
                return "nd";
            case 3:
                return "rd";
            default:
                return "th";
            }
        }

        T & operator*() noexcept
        {
            return n;
        }

        const T & operator*() const noexcept
        {
            return n;
        }
    };
} // namespace cfgo

template<cfgo::nthable T>
struct fmt::formatter<cfgo::Nth<T>> : fmt::formatter<std::string>
{
    auto format(const cfgo::Nth<T>& me, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}{}", me.n, me.postfix());
    }
};

template<>
struct fmt::formatter<cfgo::Track::MsgType> : fmt::formatter<std::string>
{
    auto format(const cfgo::Track::MsgType& me, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        switch (me)
        {
        case cfgo::Track::MsgType::RTP:
            return fmt::format_to(ctx.out(), "rtp");
        case cfgo::Track::MsgType::RTCP:
            return fmt::format_to(ctx.out(), "rtcp");
        case cfgo::Track::MsgType::ALL:
            return fmt::format_to(ctx.out(), "rtp or rtcp");
        default:
            return fmt::format_to(ctx.out(), "unknown");
        }
    }
};

template<>
struct fmt::formatter<cfgo::Pattern> : fmt::formatter<std::string>
{
    auto format(const cfgo::Pattern& me, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        nlohmann::json j;
        cfgo::to_json(j, me);
        return fmt::format_to(ctx.out(), "\n{}\n", j.dump(2));
    }
};

template<>
struct fmt::formatter<std::thread::id> : fmt::formatter<std::string>
{
    auto format(const std::thread::id& me, fmt::format_context &ctx) const -> decltype(ctx.out()) {
        std::stringstream ss;
        ss << me;
        return fmt::format_to(ctx.out(), "{}", ss.str());
    }
};

#endif