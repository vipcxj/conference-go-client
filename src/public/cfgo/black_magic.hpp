#ifndef _CFGO_BLACK_MAGIC_HPP_
#define _CFGO_BLACK_MAGIC_HPP_

#include <tuple>
#include <utility>

namespace cfgo
{
    namespace magic
    {
        template <bool B, typename... Ts>
        constexpr auto pick_if(Ts &&...xs)
        {
            if constexpr (B)
                return std::forward_as_tuple(std::forward<Ts>(xs)...);
            else
                return std::tuple{};
        }

        template <std::size_t Offset, typename... T, std::size_t... I>
        auto subtuple_(const std::tuple<T...> &t, std::index_sequence<I...>)
            -> decltype(std::make_tuple(std::get<I + Offset>(t)...))
        {
            return std::make_tuple(std::get<I>(t)...);
        }

        template <std::size_t Offset, typename... T>
        auto tuple_offset(const std::tuple<T...> &t)
            -> decltype(subtuple_<Offset>(t, std::make_index_sequence<sizeof...(T) - Offset>()))
        {
            return subtuple_<Offset>(t, std::make_index_sequence<sizeof...(T) - Offset>());
        }

        template<class... Ts> 
        struct overloaded : Ts... { using Ts::operator()...; };

        template <typename First, typename... Others>
        auto shift_variant(const std::variant<First, Others...> &t)
            -> std::variant<Others...>
        {
            return std::visit(overloaded{
                []<typename T>(T && v) -> std::variant<Others...> {
                    if constexpr (std::disjunction_v<std::is_same<std::decay_t<T>, Others>...>)
                    {
                        return v;
                    }
                    else
                    {
                        throw std::bad_variant_access();
                    }
                },
            }, t);
        }

        template <typename First, typename... Others>
        auto shift_variant(std::variant<First, Others...> &&t)
            -> std::variant<Others...>
        {
            return std::visit(overloaded{
                []<typename T>(T && v) -> std::variant<Others...> {
                    if constexpr (std::disjunction_v<std::is_same<std::decay_t<T>, Others>...>)
                    {
                        return v;
                    }
                    else
                    {
                        throw std::bad_variant_access();
                    }
                },
            }, std::move(t));
        }

    } // namespace magic

} // namespace cfgo

#endif