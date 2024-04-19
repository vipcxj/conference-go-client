#pragma once
#ifndef _CFGO_ERROR_HPP_
#define _CFGO_ERROR_HPP_

#include <system_error>
#include <type_traits>

namespace cfgo {
    enum signal_error
    {
        connect_failed = 1
    };

    class signal_category_impl : public std::error_category {
    public:
        virtual const char* name() const noexcept;
        virtual std::string message(int ev) const;
        virtual bool equivalent(
            const std::error_code& code,
            int condition) const noexcept;
    };

    const std::error_category& signal_category();

    std::error_condition make_error_condition(signal_error e);
}

namespace std
{
    template <>
    struct is_error_condition_enum<cfgo::signal_error> : public true_type {};
}

#endif