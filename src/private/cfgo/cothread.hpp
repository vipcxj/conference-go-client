#ifndef _CFGO_COTHREAD_HPP_
#define _CFGO_COTHREAD_HPP_

#include "asio.hpp"

namespace cfgo {
    
    template<typename R, asio::completion_token_for<void(std::exception_ptr, R)> CompletionToken>
    auto make_awaitable(std::function<R()> fn, R invalid, CompletionToken&& token)
    {
        auto init = [fn = std::move(fn), invalid = std::move(invalid)](
            asio::completion_handler_for<void(std::exception_ptr, R)> auto handler
        )
        {
            std::thread(
                [fn = std::move(fn), invalid = std::move(invalid), handler = std::move(handler)]() mutable
                {
                    auto work = asio::make_work_guard(handler);
                    // Get the handler's associated allocator. If the handler does not
                    // specify an allocator, use the recycling allocator as the default.
                    auto alloc = asio::get_associated_allocator(
                        handler, asio::recycling_allocator<void>()
                    );

                    try {
                        auto r = std::move(fn)();
                        asio::dispatch(
                            work.get_executor(),
                            asio::bind_allocator(alloc, [handler = std::move(handler), r = std::move(r)]() mutable {
                                std::move(handler)(std::current_exception(), std::move(r));
                            })
                        );
                    } catch (...) {
                        auto e = std::current_exception();
                        asio::dispatch(
                            work.get_executor(),
                            asio::bind_allocator(alloc, [handler = std::move(handler), invalid = std::move(invalid), e = std::move(e)]() mutable {
                                std::move(handler)(e, std::move(invalid));
                            })
                        );
                    }
                }
            ).detach();
        };
        return asio::async_initiate<CompletionToken, void(std::exception_ptr, R)>(
            init,
            token
        );
    }

    template<asio::completion_token_for<void(std::exception_ptr)> CompletionToken>
    auto make_void_awaitable(std::function<void()> fn, CompletionToken&& token)
    {
        auto init = [fn = std::move(fn)](
            asio::completion_handler_for<void(std::exception_ptr)> auto handler
        )
        {
            std::thread(
                [fn = std::move(fn), handler = std::move(handler)]() mutable
                {
                    auto work = asio::make_work_guard(handler);
                    // Get the handler's associated allocator. If the handler does not
                    // specify an allocator, use the recycling allocator as the default.
                    auto alloc = asio::get_associated_allocator(
                        handler, asio::recycling_allocator<void>()
                    );

                    try {
                        std::move(fn)();
                        asio::dispatch(
                            work.get_executor(),
                            asio::bind_allocator(alloc, [handler = std::move(handler)]() mutable {
                                std::move(handler)(std::current_exception());
                            })
                        );
                    } catch (...) {
                        auto e = std::current_exception();
                        asio::dispatch(
                            work.get_executor(),
                            asio::bind_allocator(alloc, [handler = std::move(handler), e = std::move(e)]() mutable {
                                std::move(handler)(e);
                            })
                        );
                    }
                }
            ).detach();
        };
        return asio::async_initiate<CompletionToken, void(std::exception_ptr)>(
            init,
            token
        );
    }
}

#endif