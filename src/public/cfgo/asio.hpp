#ifndef _CFGO_ASIO_HPP_
#define _CFGO_ASIO_HPP_

#ifdef STANDALONE_ASIO
#include "asio.hpp"
#define ASIOCHAN_USE_STANDALONE_ASIO
#else
#include "boost/asio.hpp"
namespace asio = boost::asio;
#endif

#include "asiochan/asiochan.hpp"
#include <memory>
#include <functional>
#include <concepts>

namespace cfgo
{
    using executor_t = asio::any_io_executor;
    using executor_factory_t = std::function<executor_t()>;
    using strand_t = asio::strand<executor_t>;

    template<typename ExecCtx>
    requires requires (ExecCtx & exec_ctx) {
        { exec_ctx.get_executor() } -> std::convertible_to<executor_t>;
    }
    executor_factory_t make_executor_factory(std::shared_ptr<ExecCtx> exec_ctx_ptr)
    {
        return [exec_ctx_ptr = std::move(exec_ctx_ptr)]() -> executor_t {
            return exec_ctx_ptr->get_executor();
        };
    }
} // namespace cfgo


#endif