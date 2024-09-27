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

    auto make_void_awaitable() -> asio::awaitable<void> {
        co_return;
    }

    bool is_valid_loc(const std::source_location & loc) {
        auto file_name = loc.file_name();
        auto fun_name = loc.function_name();
        return file_name && strlen(file_name) > 0 && fun_name && strlen(fun_name) > 0;
    }

    std::string create_cancel_error_msg(std::string&& message, CancelError::Reason reason, bool trace, const std::source_location & source_loc) {
        if (message.empty())
        {
            if (trace || !is_valid_loc(source_loc))
            {
                if (reason == CancelError::CANCEL)
                {
                    return "closer canceled";
                }
                else
                {
                    return "closer closed";
                }
            }
            else
            {
                if (reason == CancelError::CANCEL)
                {
                    return fmt::format("closer canceled [{} - {}:{}:{}]", source_loc.file_name(), source_loc.function_name(), source_loc.line(), source_loc.column());
                }
                else
                {
                    return fmt::format("closer closed [{} - {}:{}:{}]", source_loc.file_name(), source_loc.function_name(), source_loc.line(), source_loc.column());
                }
            }
        }
        else
        {
            if (trace || !is_valid_loc(source_loc))
            {
                if (reason == CancelError::CANCEL)
                {
                    return fmt::format("closer canceled, {}", message);
                }
                else
                {
                    return fmt::format("closer timeout, {}", message);
                }
            }
            else
            {
                if (reason == CancelError::CANCEL)
                {
                    return fmt::format("closer canceled, {} [{} - {}:{}:{}]", message, source_loc.file_name(), source_loc.function_name(), source_loc.line(), source_loc.column());
                }
                else
                {
                    return fmt::format("closer timeout, {} [{} - {}:{}:{}]", message, source_loc.file_name(), source_loc.function_name(), source_loc.line(), source_loc.column());
                }
            }
        }
    }

    CancelError::CancelError(std::string message, Reason reason, bool trace, std::source_location source_loc) noexcept:
        cpptrace::exception_with_message(create_cancel_error_msg(std::move(message), reason, trace, source_loc), trace ? cpptrace::detail::get_raw_trace_and_absorb() : cpptrace::raw_trace{}),
        m_reason(reason),
        m_trace(trace),
        m_loc(std::move(source_loc))
    {}

    CancelError::CancelError(Reason reason, bool trace, std::source_location source_loc) noexcept: CancelError("", reason, trace, std::move(source_loc))
    {}

    CancelError::CancelError(bool is_timeout, bool trace, std::source_location source_loc) noexcept: CancelError(is_timeout ? TIMEOUT : CANCEL, trace, std::move(source_loc))
    {}

    CancelError::CancelError(close_chan close_ch, bool trace) noexcept:
        CancelError(
            close_ch.get_close_reason(),
            close_ch.is_timeout() ? TIMEOUT : CANCEL,
            trace,
            close_ch.get_close_source_location()
        )
    {
        assert(close_ch.is_closed());
    }

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

    auto wait_timeout(duration_t dur, close_chan closer, std::string reasion) -> asio::awaitable<void> {
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
            std::source_location m_close_src_loc {};
            std::source_location m_timeout_src_loc {};
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

            void _close_self(bool is_timeout, std::string reason, std::source_location src_loc);

            void close(bool is_timeout, std::string reason, std::source_location src_loc);

            bool close_no_except(bool is_timeout, std::string reason, std::source_location src_loc) noexcept;

            void stop(bool stop_timer);

            void resume();

            auto get_stop_waiter() -> std::optional<Waiter>;

            void _set_timeout(duration_t dur, std::string reason, std::source_location src_loc);

            void set_timeout(duration_t dur, std::string reason, std::source_location src_loc);

            Ptr create_child();

            void remove_me(CloseSignalState * child);

            auto depend_on(close_chan closer, std::string reason, std::source_location src_loc) -> asio::awaitable<void>
            {
                auto self = shared_from_this();
                auto executor = co_await asio::this_coro::executor;
                asio::co_spawn(executor, fix_async_lambda([weak_self = self->weak_from_this(), weak_closer = closer.weak(), reason = std::move(reason), src_loc = std::move(src_loc)]() -> asio::awaitable<void> {
                    std::optional<Waiter> waiter = std::nullopt;
                    if (auto self = weak_self.lock())
                    {
                        waiter = self->get_waiter();
                    }
                    if (waiter)
                    {
                        if (auto closer = weak_closer.lock())
                        {
                            co_await chan_read<void>(*waiter, closer);
                            if (auto self = weak_self.lock())
                            {
                                if (closer.is_closed() && !self->m_closed)
                                {
                                    if (reason.empty())
                                    {
                                        self->close_no_except(false, closer.get_close_reason(), closer.get_close_source_location());
                                    }
                                    else
                                    {
                                        self->close_no_except(false, std::move(reason), closer.get_close_source_location());
                                    }
                                }
                            }
                        }
                        else
                        {
                            if (auto self = weak_self.lock())
                            {
                                if (reason.empty())
                                {
                                    self->close_no_except(false, "dependent closer released", std::move(src_loc));
                                }
                                else
                                {
                                    self->close_no_except(false, std::move(reason), std::move(src_loc));
                                }
                            }
                        }
                    }
                }), asio::detached);
            }

            auto after_close(std::function<asio::awaitable<void>()> cb) -> asio::awaitable<void>
            {
                auto self = shared_from_this();
                auto executor = co_await asio::this_coro::executor;
                asio::co_spawn(executor, fix_async_lambda([self = std::move(self), cb = std::move(cb)]() -> asio::awaitable<void> {
                    auto waiter = self->get_waiter();
                    if (waiter)
                    {
                        co_await waiter->read();
                        co_await cb();
                    }
                }), asio::detached);
            }
        };

        CloseSignalState::CloseSignalState(): m_parent() {}
        CloseSignalState::CloseSignalState(const std::weak_ptr<CloseSignalState> & parent)
        : m_parent(parent) {}
        CloseSignalState::CloseSignalState(std::weak_ptr<CloseSignalState> && parent)
        : m_parent(std::move(parent)) {}

        CloseSignalState::~CloseSignalState() noexcept
        {
            close_no_except(false, "Destructor called.", std::source_location {});
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
            close(true, m_timeout_reason, m_timeout_src_loc);
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

        void CloseSignalState::_close_self(bool is_timeout, std::string reason, std::source_location src_loc)
        {
            if (m_closed)
            {
                return;
            }
            m_closed = true;
            m_stop = false;
            m_close_reason = std::move(reason);
            m_close_src_loc = std::move(src_loc);
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

        void CloseSignalState::close(bool is_timeout, std::string reason, std::source_location src_loc)
        {
            if (m_closed)
            {
                return;
            }
            std::list<Ptr> children;
            std::weak_ptr<CloseSignalState> weak_parent;
            {
                std::lock_guard lock(m_mutex);
                _close_self(is_timeout, reason, src_loc);
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
                child->close(is_timeout, reason, src_loc);
            }
        }

        bool CloseSignalState::close_no_except(bool is_timeout, std::string reason, std::source_location src_loc) noexcept
        {
            try
            {
                close(is_timeout, std::move(reason), std::move(src_loc));
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
                                _set_timeout(duration_t {0}, std::move(m_timeout_reason), std::move(m_timeout_src_loc));
                            }
                        }
                        else
                        {
                            m_stop_timeout = m_timeout;
                            _set_timeout(duration_t {0}, std::move(m_timeout_reason), std::move(m_timeout_src_loc));
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
                        _set_timeout(m_stop_timeout, std::move(m_timeout_reason), std::move(m_timeout_src_loc));
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

        void CloseSignalState::_set_timeout(duration_t dur, std::string reason, std::source_location src_loc)
        {
            if (m_closed)
            {
                return;
            }
            m_timeout_reason = std::move(reason);
            m_timeout_src_loc = std::move(src_loc);
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

        void CloseSignalState::set_timeout(duration_t dur, std::string reason, std::source_location src_loc)
        {
            if (m_closed)
            {
                return;
            }
            std::lock_guard lock(m_mutex);
            _set_timeout(std::move(dur), std::move(reason), std::move(src_loc));
        }

        auto CloseSignalState::create_child() -> Ptr
        {
            std::lock_guard lock(m_mutex);
            if (m_closed)
            {
                auto child = std::make_shared<CloseSignalState>();
                child->_close_self(m_is_timeout, m_close_reason, m_close_src_loc);
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

    WeakCloseSignal CloseSignal::weak() const noexcept {
            return WeakCloseSignal{m_state};
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

    void CloseSignal::close(std::string reason, std::source_location src_loc) const
    {
        if (m_state)
        {
            m_state->close(false, std::move(reason), std::move(src_loc));
        }
        else
        {
            throw cpptrace::runtime_error("The null closer dose not support the close operation.");
        }
    }

    bool CloseSignal::close_no_except(std::string reason, std::source_location src_loc) const noexcept
    {
        return m_state ? m_state->close_no_except(false, std::move(reason), std::move(src_loc)) : false;
    }

    void CloseSignal::set_timeout(duration_t dur, std::string reason, std::source_location src_loc) const
    {
        if (m_state)
        {
            m_state->set_timeout(std::move(dur), std::move(reason), std::move(src_loc));
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

    std::source_location CloseSignal::get_close_source_location() const noexcept
    {
        return m_state ? m_state->m_close_src_loc : std::source_location {};
    }

    auto CloseSignal::depend_on(close_chan closer, std::string reason, std::source_location src_loc) const -> asio::awaitable<void>
    {
        if (m_state)
        {
            return m_state->depend_on(std::move(closer), std::move(reason), std::move(src_loc));
        }
        else
        {
            throw cpptrace::runtime_error("The null closer does not support the depend_on operation.");
        }
    }

    auto CloseSignal::after_close(std::function<asio::awaitable<void>()> cb) const -> asio::awaitable<void>
    {
        if (m_state)
        {
            co_await m_state->after_close(std::move(cb));
        }
        co_return;
    }

    auto wrap_cancel(std::function<asio::awaitable<void>()> func) -> asio::awaitable<void> {
        try
        {
            co_await func();
        }
        catch(const CancelError& e) {}
    }

    auto log_error(std::function<asio::awaitable<void>()> func, Logger logger, const std::source_location loc) -> std::function<asio::awaitable<void>()> {
        return [logger = std::move(logger), func = std::move(func), loc = std::move(loc)]() -> asio::awaitable<void> {
            try
            {
                co_await func();
            }
            catch(const CancelError& e)
            {
                logger->debug("[{}:{}:{}]<{}> canceled, {}", loc.file_name(), loc.line(), loc.column(), loc.function_name(), e.what());
            }
            catch(...)
            {
                logger->error("[{}:{}:{}]<{}> error found, {}", loc.file_name(), loc.line(), loc.column(), loc.function_name(), what());
            }
        };
    }

    namespace detail
    {
        struct StateMaybeChangedNotifierState {
            std::vector<unique_void_chan> m_chs {};
            mutex m_mux {};

            void notify() {
                std::lock_guard lg(m_mux);
                for (auto & ch : m_chs) {
                    chan_maybe_write(ch);
                }
                m_chs.clear();
            }

            auto make_notfiy_receiver() -> unique_void_chan {
                unique_void_chan ch {};
                {
                    std::lock_guard lg(m_mux);
                    m_chs.push_back(ch);
                }
                return ch;
            }
        };
    } // namespace detail

    StateMaybeChangedNotifier::StateMaybeChangedNotifier(): m_state(std::make_shared<detail::StateMaybeChangedNotifierState>()) {}
    

    void StateMaybeChangedNotifier::notify() const {
        m_state->notify();
    }

    auto StateMaybeChangedNotifier::make_notfiy_receiver() const -> unique_void_chan {
        return m_state->make_notfiy_receiver();
    }

    
} // namespace cfgo
