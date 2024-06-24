#include "cfgo/coevent.hpp"
#include <atomic>

namespace cfgo {

    bool CoEvent::trigger() {
        bool handler = false;
        {
            const std::lock_guard lock(m_mutex);
            if (!m_done && --m_times == 0) {
                m_done = true;
                m_exception = std::current_exception();
                if (m_timer)
                {
                    m_timer.reset();
                }
                if (m_handler)
                {
                    handler = true;
                } 
            }
        }
        if (handler)
        {
            m_handler(m_exception);
        }
        return handler;
    }

    inline void invoke_handler(CoEvent::handler_type&& handler, std::exception_ptr&& e)
    {
        auto executor = asio::get_associated_executor(handler);
        auto alloc = asio::get_associated_allocator(
            handler, asio::recycling_allocator<void>()
        );
        asio::dispatch(
            executor,
            asio::bind_allocator(alloc, [handler = std::move(handler), e = std::move(e)]() mutable {
                std::move(handler)(e);
            })
        );
    }

    asio::awaitable<void> CoEvent::await(std::chrono::nanoseconds timeout) {
        return asio::async_initiate<decltype(asio::use_awaitable), void(std::exception_ptr)>([weak_self = weak_from_this(), timeout](auto&& handler) -> void {
            if (auto self = weak_self.lock())
            {   
                if (timeout.count() > 0)
                {
                    self->m_timer = std::make_unique<asio::steady_timer>(asio::get_associated_executor(handler), timeout);
                    self->m_timer->async_wait([self](auto && err) {
                        if (err == asio::error::operation_aborted)
                        {
                            return;
                        }
                        bool handler = false;
                        {
                            std::lock_guard lock(self->m_mutex);
                            if (!self->m_done)
                            {
                                self->m_done = true;
                                self->m_timer.reset();
                                self->m_exception = std::make_exception_ptr(std::make_error_code(std::errc::timed_out));
                                if (self->m_handler)
                                {
                                    handler = true;
                                }
                            }
                        }
                        if (handler)
                        {
                            self->m_handler(self->m_exception);
                        }
                    });
                }
                bool invoke = false;
                {
                    std::lock_guard lock(self->m_mutex);
                    if (self->m_done)
                    {
                        invoke = true;
                    } else {
                        self->m_handler = [handler = std::move(handler)](std::exception_ptr e) mutable {
                            invoke_handler(std::move(handler), std::move(e));
                        };
                    }
                }
                if (invoke)
                {
                    invoke_handler(std::move(handler), std::move(self->m_exception));
                }
            }
        }, asio::use_awaitable);
    }
}