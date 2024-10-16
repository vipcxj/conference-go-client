#include "cfgo/error.hpp"
#include "cfgo/json.hpp"

namespace cfgo {

    const char* signal_category_impl::name() const noexcept
    {
        return "client";
    }

    std::string signal_category_impl::message(int ev) const
    {
        switch (ev)
        {
        case signal_error::connect_failed:
            return "Unable to connect to the signal server";
        default:
            return "Unknown signal error";
        }
    }

    bool signal_category_impl::equivalent(
        const std::error_code& code,
        int condition) const noexcept
    {
        return false;
    }

    const std::error_category& signal_category()
    {
        static signal_category_impl api_category_instance;
        return api_category_instance;
    }

    std::error_condition make_error_condition(signal_error e)
    {
        return std::error_condition(
            static_cast<int>(e),
            signal_category());
    }

    allocate_tracers::unique_ptr<ServerErrorObject> ServerErrorObject::create(std::string msg, int code, bool call_frames)
    {
        auto ptr = allocate_tracers::make_unique<ServerErrorObject>();
        ptr->msg = std::move(msg);
        ptr->code = code;
        if (call_frames)
        {
            auto trace = cpptrace::generate_trace(1);
            for(auto iter = trace.begin(); iter != trace.end(); ++iter)
            {
                ptr->callFrames.push_back(CallFrame {
                    .filename = iter->filename,
                    .line = (int) iter->line.value_or(0),
                    .funcname = iter->symbol
                });
            }
        }
        return ptr;
    }
}