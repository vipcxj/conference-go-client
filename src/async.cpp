#include "cfgo/async.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/log.hpp"
#include "spdlog/spdlog.h"
#include <list>
#include <chrono>

namespace cfgo
{
    close_chan INVALID_CLOSE_CHAN {nullptr};

    CancelError::CancelError(std::string&& message, Reason reason, bool trace) noexcept:
        cpptrace::exception_with_message(std::move(message), trace ? cpptrace::detail::get_raw_trace_and_absorb() : cpptrace::raw_trace{}),
        m_reason(reason),
        m_trace(trace)
    {}

    CancelError::CancelError(Reason reason, bool trace) noexcept: CancelError("", reason, trace)
    {}

    CancelError::CancelError(bool is_timeout, bool trace) noexcept: CancelError(is_timeout ? TIMEOUT : CANCEL, trace)
    {}

    CancelError::CancelError(const close_chan & close_ch, bool trace) noexcept:
        CancelError(
            close_ch.is_timeout() ? std::move(close_ch.get_timeout_reason()) : std::move(close_ch.get_close_reason()),
            close_ch.is_timeout() ? TIMEOUT : CANCEL,
            trace
        )
    {}

    const char* CancelError::what() const noexcept
    {
        return m_trace ? cpptrace::exception_with_message::what() : cpptrace::exception_with_message::message();
    }

    cancelable<void> make_resolved() {
        return cancelable<void>(false);
    }

    cancelable<void> make_canceled() {
        return cancelable<void>(true);
    }

    close_chan make_timeout(const duration_t& dur) {
        auto timeout = close_chan{};
        timeout.set_timeout(dur);
        return timeout;
    }

    auto wait_timeout(const duration_t& dur, close_chan closer, std::string && reasion) -> asio::awaitable<void> {
        if (closer)
        {
            auto timeout_closer = closer.create_child();
            timeout_closer.set_timeout(dur, std::move(reasion));
            if (co_await timeout_closer.await())
            {
                throw CancelError(closer);
            }
        }
        else
        {
            auto executor = co_await asio::this_coro::executor;
            auto timer = asio::steady_timer{executor};
            timer.expires_after(dur);
            co_await timer.async_wait(asio::use_awaitable);
            co_return;
        }
    }

    auto AsyncMutex::accquire(close_chan close_chan) -> asio::awaitable<bool>
    {
        bool success = false;
        busy_chan ch{};
        bool ch_added = false;
        while (true)
        {
            {
                std::lock_guard g(m_mutex);
                if (!m_busy)
                {
                    m_busy = true;
                    success = true;
                    if (ch_added)
                    {
                        m_busy_chans.erase(std::remove(m_busy_chans.begin(), m_busy_chans.end(), ch), m_busy_chans.end());
                    }
                }
                else if (!ch_added)
                {
                    ch_added = true;
                    m_busy_chans.push_back(ch);
                }
            }
            if (success)
            {
                co_return true;
            }
            auto &&result = co_await chan_read<void>(ch, close_chan);
            if (result.is_canceled())
            {
                co_return false;
            }
        }
    }

    auto AsyncMutex::release() -> asio::awaitable<void>
    {
        auto executor = co_await asio::this_coro::executor;
        release(executor);
    }

    namespace detail
    {
        struct CloseSignalState : public std::enable_shared_from_this<CloseSignalState>
        {
            using Ptr = std::shared_ptr<CloseSignalState>;
            using Waiter = CloseSignal::Waiter;
            bool m_closed = false;
            std::string m_close_reason;
            bool m_is_timeout = false;
            bool m_stop = false;
            mutex m_mutex;
            duration_t m_timeout = duration_t {0};
            duration_t m_stop_timeout = duration_t {0};
            std::string m_timeout_reason = "timeout";
            std::shared_ptr<asio::steady_timer> m_timer = nullptr;
            std::list<Waiter> m_waiters;
            std::list<Waiter> m_stop_waiters;
            std::weak_ptr<CloseSignalState> m_parent;
            std::list<Ptr> m_children;

            CloseSignalState();
            CloseSignalState(const std::weak_ptr<CloseSignalState> & parent);
            CloseSignalState(std::weak_ptr<CloseSignalState> && parent);

            ~CloseSignalState() noexcept;

            auto timer_task() -> asio::awaitable<void>;

            void init_timer(asio::execution::executor auto executor);

            auto get_waiter() -> std::optional<Waiter>;

            void _close_self(bool is_timeout, std::string && reason);

            void close(bool is_timeout, const std::string & reason);
            void close(bool is_timeout, std::string && reason);

            bool close_no_except(bool is_timeout, const std::string & reason) noexcept;
            bool close_no_except(bool is_timeout, std::string && reason) noexcept;

            void stop(bool stop_timer);

            void resume();

            auto get_stop_waiter() -> std::optional<Waiter>;

            void _set_timeout(const duration_t& dur, std::string && reason);

            void set_timeout(const duration_t& dur, const std::string & reason);
            void set_timeout(const duration_t& dur, std::string && reason);

            Ptr create_child();

            void remove_me(CloseSignalState * child);
        };

        CloseSignalState::CloseSignalState(): m_parent() {}
        CloseSignalState::CloseSignalState(const std::weak_ptr<CloseSignalState> & parent)
        : m_parent(parent) {}
        CloseSignalState::CloseSignalState(std::weak_ptr<CloseSignalState> && parent)
        : m_parent(std::move(parent)) {}

        CloseSignalState::~CloseSignalState() noexcept
        {
            close_no_except(false, "Destructor called.");
        }

        auto CloseSignalState::timer_task() -> asio::awaitable<void>
        {
            co_await m_timer->async_wait(asio::use_awaitable);
            bool cancelled = false;
            {
                std::lock_guard lock(m_mutex);
                if (m_timeout == duration_t {0})
                {
                    cancelled = true;
                }
            }
            if (cancelled)
            {
                co_return;
            }
            close(true, std::string(m_timeout_reason));
        }

        void CloseSignalState::init_timer(asio::execution::executor auto executor)
        {
            if (m_closed || m_timer)
            {
                return;
            }
            {
                std::lock_guard lock(m_mutex);
                if (m_closed || m_timer)
                {
                    return;
                }
                m_timer = std::make_shared<asio::steady_timer>(executor);
                if (m_timeout != duration_t {0})
                {
                    m_timer->expires_after(m_timeout);
                    asio::co_spawn(executor, fix_async_lambda([self = shared_from_this()]() -> asio::awaitable<void> {
                        co_await self->timer_task();
                    }), asio::detached);
                }
            }
            if (auto parent = m_parent.lock())
            {
                parent->init_timer(executor);
            }
        }

        auto CloseSignalState::get_waiter() -> std::optional<Waiter>
        {
            if (m_closed)
            {
                return std::nullopt;
            }
            std::lock_guard lock(m_mutex);
            if (m_closed)
            {
                return std::nullopt;
            }
            Waiter waiter {};
            m_waiters.push_back(waiter);
            return waiter;
        }

        void CloseSignalState::_close_self(bool is_timeout, std::string && reason)
        {
            if (m_closed)
            {
                return;
            }
            m_closed = true;
            m_stop = false;
            m_close_reason = std::move(reason);
            m_is_timeout = is_timeout;
            m_timeout = duration_t {0};
            if (m_timer)
            {
                m_timer->cancel();
            }
            while (!m_waiters.empty())
            {
                auto && waiter = m_waiters.front();
                chan_must_write(waiter);
                m_waiters.pop_front();
            }
            while (!m_stop_waiters.empty())
            {
                auto && waiter = m_stop_waiters.front();
                chan_must_write(waiter);
                m_stop_waiters.pop_front();
            }
        }

        void CloseSignalState::close(bool is_timeout, const std::string & reason)
        {
            auto reason_copy = reason;
            close(is_timeout, std::move(reason_copy));
        }

        void CloseSignalState::close(bool is_timeout, std::string && reason)
        {
            if (m_closed)
            {
                return;
            }
            std::string close_reason;
            std::list<Ptr> children;
            std::weak_ptr<CloseSignalState> weak_parent;
            {
                std::lock_guard lock(m_mutex);
                _close_self(is_timeout, std::move(reason));
                close_reason = m_close_reason;
                weak_parent = m_parent;
                m_parent.reset();
                children = m_children;
                m_children.clear();
            }
            if (auto parent = weak_parent.lock())
            {
                parent->remove_me(this);
            }
            for (auto & child : children)
            {
                child->close(is_timeout, std::move(close_reason));
            }
        }

        bool CloseSignalState::close_no_except(bool is_timeout, const std::string & reason) noexcept
        {
            auto reason_copy = reason;
            return close_no_except(is_timeout, std::move(reason_copy));
        }

        bool CloseSignalState::close_no_except(bool is_timeout, std::string && reason) noexcept
        {
            try
            {
                close(is_timeout, std::move(reason));
                return true;
            }
            catch(...) {}
            return false;
        }

        void CloseSignalState::stop(bool stop_timer)
        {
            if (m_closed || m_stop)
            {
                return;
            }
            std::list<Ptr> children {};
            {
                std::lock_guard lock(m_mutex);
                if (!m_closed && !m_stop)
                {
                    m_stop = true;
                    if (stop_timer)
                    {
                        if (m_timer && m_timeout > duration_t {0})
                        {
                            auto now = std::chrono::steady_clock::now();
                            if (m_timer->expiry() > now)
                            {
                                m_stop_timeout = m_timer->expiry() - now;
                                _set_timeout(duration_t {0}, std::move(m_timeout_reason));
                            }
                        }
                        else
                        {
                            m_stop_timeout = m_timeout;
                            _set_timeout(duration_t {0}, std::move(m_timeout_reason));
                        }
                    }
                    children = m_children;
                }
            }
            for (auto && child : children)
            {
                child->stop(stop_timer);
            }
        }

        void CloseSignalState::resume()
        {
            if (!m_stop)
            {
                return;
            }
            std::list<Ptr> children {};
            {
                std::lock_guard lock(m_mutex);
                if (m_stop)
                {
                    m_stop = false;
                    while (!m_stop_waiters.empty())
                    {
                        auto waiter = m_stop_waiters.front();
                        if (!waiter.try_write())
                        {
                            throw cpptrace::logic_error(cfgo::THIS_IS_IMPOSSIBLE);
                        }
                        m_stop_waiters.pop_front();
                    }
                    if (m_stop_timeout > duration_t {0})
                    {
                        _set_timeout(m_stop_timeout, std::move(m_timeout_reason));
                        m_stop_timeout = duration_t {0};
                    }
                    children = m_children;
                }
            }
            for (auto && child : children)
            {
                child->resume();
            }
        }

        auto CloseSignalState::get_stop_waiter() -> std::optional<Waiter>
        {
            if (!m_stop)
            {
                return std::nullopt;
            }
            std::lock_guard lock(m_mutex);
            if (!m_stop)
            {
                return std::nullopt;
            }
            Waiter waiter {};
            m_stop_waiters.push_back(waiter);
            return waiter;
        }

        void CloseSignalState::_set_timeout(const duration_t& dur, std::string&& reason)
        {
            if (m_closed)
            {
                return;
            }
            m_timeout_reason = std::move(reason);
            if (m_timeout == dur)
            {
                return;
            }
            duration_t old_timeout = m_timeout;
            m_timeout = dur;
            if (m_timer)
            {
                if (dur == duration_t {0})
                {
                    m_timer->cancel();
                }
                else if (old_timeout == duration_t {0})
                {
                    m_timer->expires_after(dur);
                    asio::co_spawn(m_timer->get_executor(), fix_async_lambda([self = shared_from_this()]() -> asio::awaitable<void> {
                        co_await self->timer_task();
                    }), asio::detached);
                }
                else
                {
                    m_timer->expires_at(m_timer->expiry() - old_timeout + dur);
                }
            }
        }

        void CloseSignalState::set_timeout(const duration_t& dur, const std::string& reason)
        {
            auto reason_copy = reason;
            set_timeout(dur, std::move(reason_copy));
        }

        void CloseSignalState::set_timeout(const duration_t& dur, std::string&& reason)
        {
            if (m_closed)
            {
                return;
            }
            std::lock_guard lock(m_mutex);
            _set_timeout(dur, std::move(reason));
        }

        auto CloseSignalState::create_child() -> Ptr
        {
            std::lock_guard lock(m_mutex);
            if (m_closed)
            {
                auto child = std::make_shared<CloseSignalState>();
                auto close_reason = m_close_reason;
                child->_close_self(m_is_timeout, std::move(close_reason));
                return child;
            }
            else
            {
                auto child = std::make_shared<CloseSignalState>(weak_from_this());
                m_children.push_back(child);
                return child;
            }
        }

        void CloseSignalState::remove_me(CloseSignalState * child)
        {
            if (m_closed)
            {
                return;
            }
            std::lock_guard g(m_mutex);
            if (m_closed)
            {
                return;
            }
            m_children.erase(
                std::remove_if(m_children.begin(), m_children.end(), [child](auto && v) { return v.get() == child; }),
                m_children.end()
            );
        }
    } // namespace detail

    CloseSignal::CloseSignal(std::nullptr_t): m_state(nullptr) {}

    CloseSignal::CloseSignal(const std::shared_ptr<detail::CloseSignalState> & state): m_state(state)
    {}

    CloseSignal::CloseSignal(std::shared_ptr<detail::CloseSignalState> && state): m_state(std::move(state))
    {}

    CloseSignal::CloseSignal(): m_state(std::make_shared<detail::CloseSignalState>())
    {}

    auto CloseSignal::init_timer() const -> asio::awaitable<void>
    {
        if (m_state)
        {
            auto executor = co_await asio::this_coro::executor;
            m_state->init_timer(executor);
        }
    }

    auto CloseSignal::get_waiter() const -> std::optional<Waiter>
    {
        if (m_state)
        {
            return m_state->get_waiter();
        }
        else
        {
            // forever, never close.
            return unique_void_chan {};
        }
    }

    auto CloseSignal::get_stop_waiter() const -> std::optional<Waiter>
    {
        if (m_state)
        {
            return m_state->get_stop_waiter();
        }
        else
        {
            // never stop.
            return std::nullopt;
        }
    }

    bool CloseSignal::is_closed() const noexcept
    {
        // null closer never closed.
        return m_state ? m_state->m_closed : false;
    }

    bool CloseSignal::is_timeout() const noexcept
    {
        // null closer never timeout.
        return m_state ? m_state->m_is_timeout : false;
    }

    void CloseSignal::close(const std::string & reason) const
    {
        auto reason_copy = reason;
        close(std::move(reason_copy));
    }

    void CloseSignal::close(std::string && reason) const
    {
        if (m_state)
        {
            m_state->close(false, std::move(reason));
        }
        else
        {
            throw cpptrace::runtime_error("The null closer dose not support the close operation.");
        }
    }

    bool CloseSignal::close_no_except(const std::string & reason) const noexcept
    {
        auto reason_copy = reason;
        return close_no_except(std::move(reason_copy));
    }

    bool CloseSignal::close_no_except(std::string && reason) const noexcept
    {
        return m_state ? m_state->close_no_except(false, std::move(reason)) : false;
    }

    void CloseSignal::set_timeout(const duration_t& dur, const std::string & reason) const
    {
        auto reason_copy = reason;
        set_timeout(dur, std::move(reason_copy));
    }

    void CloseSignal::set_timeout(const duration_t& dur, std::string && reason) const
    {
        if (m_state)
        {
            m_state->set_timeout(dur, std::move(reason));
        }
        else
        {
            throw cpptrace::runtime_error("The null closer does not support the timeout operation.");
        }
    }

    duration_t CloseSignal::get_timeout() const noexcept
    {
        return m_state ? m_state->m_timeout : duration_t {0};
    }

    void CloseSignal::stop(bool stop_timer) const
    {
        if (m_state)
        {
            m_state->stop(stop_timer);
        }
        else
        {
            throw cpptrace::runtime_error("The null closer does not support the stop operation.");
        }
    }

    void CloseSignal::resume() const
    {
        if (m_state)
        {
            m_state->resume();
        }
        else
        {
            throw cpptrace::runtime_error("The null closer does not support the stop operation.");
        }
    }

    CloseSignal CloseSignal::create_child() const
    {
        if (m_state)
        {
            return m_state->create_child();
        }
        else
        {
            // just a new closer.
            return CloseSignal {};
        }
    }

    auto CloseSignal::await() const -> asio::awaitable<bool>
    {
        co_await init_timer();
        if (auto waiter = get_waiter())
        {
            co_await waiter->read();
        }
        co_return !is_timeout();
    }

    const char * CloseSignal::get_close_reason() const noexcept
    {
        return m_state ? m_state->m_close_reason.c_str() : "";
    }

    const char * CloseSignal::get_timeout_reason() const noexcept
    {
        return m_state ? m_state->m_timeout_reason.c_str() : "";
    }
    
} // namespace cfgo
