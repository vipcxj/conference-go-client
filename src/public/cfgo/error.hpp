#pragma once
#ifndef _CFGO_ERROR_HPP_
#define _CFGO_ERROR_HPP_

#include <system_error>
#include <type_traits>
#include "cpptrace/cpptrace.hpp"

namespace cfgo {

    struct CallFrame
    {
        std::string filename;
        int line;
        std::string funcname;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CallFrame, filename, line, funcname)
    };
    

    struct ServerErrorObject {
        int code = 0;
        std::string msg {};
        nlohmann::json data {nullptr};
        std::vector<CallFrame> callFrames {};
        std::string cause {};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ServerErrorObject, code, msg, data, callFrames, cause)
    };

    class ServerError : public cpptrace::exception_with_message {
    private:
        ServerErrorObject m_seo;
    public:
        ServerError(ServerErrorObject && seo, bool trace = true) noexcept:
            m_seo(std::move(seo)),
            cpptrace::exception_with_message(std::string(m_seo.msg), trace ? cpptrace::detail::get_raw_trace_and_absorb() : cpptrace::raw_trace{})
        {}

        const ServerErrorObject & data() const noexcept {
            return m_seo;
        }
    };

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