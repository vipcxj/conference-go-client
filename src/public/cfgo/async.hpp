#ifndef _CFGO_ASYNC_HPP_
#define _CFGO_ASYNC_HPP_

#include <chrono>
#include <mutex>
#include <vector>
#include <set>
#include "cfgo/alias.hpp"
#include "cfgo/common.hpp"
#include "cfgo/black_magic.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/log.hpp"
#include "asio/awaitable.hpp"
#include "asio/steady_timer.hpp"
#include "asiochan/asiochan.hpp"
#include "cpptrace/cpptrace.hpp"

namespace cfgo
{
    class CloseSignal;
    using close_chan = CloseSignal;
    using close_chan_ptr = std::shared_ptr<close_chan>;
    extern close_chan INVALID_CLOSE_CHAN;
    using duration_t = std::chrono::steady_clock::duration;
    template<typename T>
    using unique_chan = asiochan::channel<T, 1>;
    using unique_void_chan = unique_chan<void>;

    template <typename F, typename ...Args>
    auto invoke_async_lambda(F f, Args ...args)
        -> decltype(f(args...))
    { co_return co_await f(args...); }

    template <typename F>
    auto fix_async_lambda(F f) {
        return [f](auto &&...args) {
            return invoke_async_lambda(f, std::forward<decltype(args)>(args)...);
        };
    }

    namespace detail
    {
        class CloseSignalState;
    } // namespace detail

    class CloseSignal
    {
    private:
        std::shared_ptr<detail::CloseSignalState> m_state;
        CloseSignal(const std::shared_ptr<detail::CloseSignalState> & state);
        CloseSignal(std::shared_ptr<detail::CloseSignalState> && state);
    public:
        using Waiter = asiochan::channel<void, 1>;
        CloseSignal(std::nullptr_t);
         CloseSignal();
        [[nodiscard]] bool is_closed() const noexcept;
        [[nodiscard]] bool is_timeout() const noexcept;
        [[nodiscard]] inline operator bool() const noexcept
        {
            return (bool) m_state;
        }
        void close(std::string && reason = "") const;
        bool close_no_except(std::string && reason = "") const noexcept;
        /**
         * Async wait until closed or timeout. Return false if timeout.
        */
        [[nodiscard]] auto await() const -> asio::awaitable<bool>;
        void set_timeout(const duration_t& dur, std::string && reason = "") const;
        duration_t get_timeout() const noexcept;
        [[nodiscard]] CloseSignal create_child() const;
        void stop(bool stop_timer = true) const;
        void resume() const;
        [[nodiscard]] friend inline auto operator==(
            CloseSignal const& lhs,
            CloseSignal const& rhs) noexcept -> bool
        {
            return lhs.m_state == rhs.m_state;
        }
        [[nodiscard]] friend auto operator!=(
            CloseSignal const& lhs,
            CloseSignal const& rhs) noexcept -> bool = default;

        auto init_timer() const -> asio::awaitable<void>;
        [[nodiscard]] auto get_waiter() const -> std::optional<Waiter>;
        [[nodiscard]] auto get_stop_waiter() const -> std::optional<Waiter>;
        [[nodiscard]] const char * get_close_reason() const noexcept;
        [[nodiscard]] const char * get_timeout_reason() const noexcept;

        friend class detail::CloseSignalState;
    };

    inline bool is_valid_close_chan(const close_chan & ch) noexcept {
        return ch;
    }

    class AsyncMutex {
    private:
        bool m_busy = false;
        using busy_chan = asiochan::channel<void>;
        std::vector<busy_chan> m_busy_chans;
        mutex m_mutex;
    public:
        [[nodiscard]] asio::awaitable<bool> accquire(close_chan close_ch = INVALID_CLOSE_CHAN);
        auto release() -> asio::awaitable<void>;
        void release(asio::execution::executor auto executor)
        {
            std::lock_guard g(m_mutex);
            m_busy = false;
            for (auto &ch : m_busy_chans)
            {
                asio::co_spawn(executor, ch.write(), asio::detached);
            }
        }
    };

    class CancelError : public cpptrace::exception_with_message
    {
    public:
        enum Reason
        {
            CANCEL,
            TIMEOUT
        };
    protected:
        Reason m_reason;
        bool m_trace;
    public:
        explicit CancelError(std::string&& message, Reason reason = CANCEL, bool trace = false) noexcept;
        explicit CancelError(Reason reason = CANCEL, bool trace = false) noexcept;
        explicit CancelError(bool is_timeout, bool trace = false) noexcept;
        explicit CancelError(const close_chan & close_ch, bool trace = false) noexcept;
        const char* what() const noexcept override;
        inline bool is_timeout() const noexcept
        {
            return m_reason == TIMEOUT;
        }
    };

    template<typename T>
    class cancelable {
        std::variant<T, bool> m_value;

    public:
        using value_t = T;
        explicit cancelable(const T & value): m_value(value) {}
        explicit cancelable(T && value): m_value(std::move(value)) {}
        cancelable(): m_value(false) {}

        auto value() & -> T & {
            return std::get<T>(m_value);
        }

        auto value() const & -> T const & {
            return std::get<T>(m_value);
        }

        auto value() && -> T && {
            return std::get<T>(std::move(m_value));
        }

        auto value() const && -> T const && {
            return std::get<T>(std::move(m_value));
        }

        bool is_canceled() const noexcept {
            return m_value.index() == 1;
        }

        operator bool() const noexcept {
            return !is_canceled();
        }

        inline auto operator->() & -> T &
        {
            return value();
        }

        inline auto operator->() const & -> T const &
        {
            return value();
        }

        inline auto operator->() && -> T &&
        {
            return value();
        }

        inline auto operator->() const && -> T const &&
        {
            return value();
        }
    };

    template<>
    class cancelable<bool> {
        bool m_value;
        bool m_canceled;

    public:
        explicit cancelable(bool value): m_value(value), m_canceled(false) {}
        cancelable(): m_value(false), m_canceled(true) {}

        bool value() const {
            return m_value;
        }

        bool is_canceled() const noexcept {
            return m_canceled;
        }

        operator bool() const noexcept {
            return !is_canceled();
        }

        inline const bool * operator->() const noexcept
        {
            return &m_value;
        }

        inline bool * operator->() noexcept
        {
            return &m_value;
        }
    };

    template<>
    class cancelable<void> {
        bool m_canceled;

    public:
        cancelable(bool canceled = true): m_canceled(canceled) {}

        bool is_canceled() const noexcept {
            return m_canceled;
        }

        operator bool() const noexcept {
            return !is_canceled();
        }
    };

    template<typename T>
    cancelable<T> make_resolved(T && value) {
        return cancelable<T>(std::forward<T>(value));
    }

    cancelable<void> make_resolved();

    template<typename T>
    cancelable<T> make_canceled() {
        return cancelable<T>();
    }

    cancelable<void> make_canceled();

    template<typename... TS>
    class select_result
    {
    private:
        std::variant<TS...> m_value;
    public:
        using PC = cancelable<std::variant<TS...>>;
        explicit select_result(const std::variant<TS...> & v): m_value(v) {}
        explicit select_result(std::variant<TS...> && v): m_value(std::move(v)) {}

        template <typename T>
        static constexpr bool is_alternative = (std::same_as<T, TS> or ...);

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() & -> T&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T& result) -> T& { return result; },
                    [](auto const&) -> T& { throw asiochan::bad_select_result_access{}; },
                },
                m_value);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const& -> T const&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T const& result) -> T const& { return result; },
                    [](auto const&) -> T const& { throw asiochan::bad_select_result_access{}; },
                },
                m_value);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() && -> T&&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T& result) -> T&& { return std::move(result); },
                    [](auto const&) -> T&& { throw asiochan::bad_select_result_access{}; },
                },
                m_value);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const&& -> T const&&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T const& result) -> T const&& { return std::move(result); },
                    [](auto const&) -> T const&& { throw asiochan::bad_select_result_access{}; },
                },
                m_value);
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() & -> T&
        // clang-format on
        {
            return get<asiochan::read_result<T>>().get();
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() const& -> T const&
        // clang-format on
        {
            return get<asiochan::read_result<T>>().get();
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received()&& -> T&&
        // clang-format on
        {
            return std::move(get<asiochan::read_result<T>>().get());
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() const&& -> T const&&
        // clang-format on
        {
            return std::move(get<asiochan::read_result<T>>().get());
        }

        // clang-format off
        template <asiochan::any_readable_channel_type T>
        requires is_alternative<asiochan::read_result<typename T::send_type>>
        [[nodiscard]] auto received_from(T const& channel) const noexcept -> bool
        // clang-format on
        {
            using SendType = typename T::send_type;

            return std::visit(
                asiochan::detail::overloaded{
                    [&](asiochan::read_result<SendType> const& result)
                    { return result.matches(channel); },
                    [](auto const&)
                    { return false; },
                },
                m_value);
        }
    };

    template<typename... TS>
    class cancelable_select_result : public cancelable<std::variant<TS...>>
    {
    public:
        using PC = cancelable<std::variant<TS...>>;
        cancelable_select_result(std::variant<TS...> v): PC(v) {}
        cancelable_select_result(): PC() {}

        template <typename T>
        static constexpr bool is_alternative = (std::same_as<T, TS> or ...);

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() & -> T&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T& result) -> T& { return result; },
                    [](auto const&) -> T& { throw asiochan::bad_select_result_access{}; },
                },
                PC::value());
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const& -> T const&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T const& result) -> T const& { return result; },
                    [](auto const&) -> T const& { throw asiochan::bad_select_result_access{}; },
                },
                PC::value());
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() && -> T&&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T& result) -> T&& { return std::move(result); },
                    [](auto const&) -> T&& { throw asiochan::bad_select_result_access{}; },
                },
                PC::value());
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const&& -> T const&&
        // clang-format on
        {
            return std::visit(
                asiochan::detail::overloaded{
                    [](T const& result) -> T const&& { return std::move(result); },
                    [](auto const&) -> T const&& { throw asiochan::bad_select_result_access{}; },
                },
                PC::value());
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() & -> T&
        // clang-format on
        {
            return get<asiochan::read_result<T>>().get();
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() const& -> T const&
        // clang-format on
        {
            return get<asiochan::read_result<T>>().get();
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received()&& -> T&&
        // clang-format on
        {
            return std::move(get<asiochan::read_result<T>>().get());
        }

        // clang-format off
        template <asiochan::sendable_value T>
        requires is_alternative<asiochan::read_result<T>>
        [[nodiscard]] auto get_received() const&& -> T const&&
        // clang-format on
        {
            return std::move(get<asiochan::read_result<T>>().get());
        }

        // clang-format off
        template <asiochan::any_readable_channel_type T>
        requires is_alternative<asiochan::read_result<typename T::send_type>>
        [[nodiscard]] auto received_from(T const& channel) const noexcept -> bool
        // clang-format on
        {
            using SendType = typename T::send_type;

            return std::visit(
                asiochan::detail::overloaded{
                    [&](asiochan::read_result<SendType> const& result)
                    { return result.matches(channel); },
                    [](auto const&)
                    { return false; },
                },
                PC::value());
        }
    };

    template<asiochan::select_op... Ops>
    auto make_canceled_select_result() -> cancelable_select_result<typename Ops::result_type...>
    {
        return cancelable_select_result<typename Ops::result_type...>();
    }

    close_chan make_timeout(const duration_t& dur);

    auto wait_timeout(const duration_t& dur) -> asio::awaitable<void>;

    template <asiochan::sendable T, asiochan::select_op Op1, asiochan::select_op Op2>
    class combine_read_op
    {
    public:
        using executor_type = typename Op1::executor_type;
        using result_type = asiochan::read_result<T>;
        static constexpr auto num_alternatives_1 = Op1::num_alternatives;
        static constexpr auto num_alternatives_2 = Op2::num_alternatives;
        static constexpr auto num_alternatives = num_alternatives_1 + num_alternatives_2;
        static constexpr auto always_waitfree_1 = Op1::always_waitfree;
        static constexpr auto always_waitfree_2 = Op2::always_waitfree;
        static constexpr auto always_waitfree = always_waitfree_1 && always_waitfree_2;
        using wait_state_type_1 = typename Op1::wait_state_type;
        using wait_state_type_2 = typename Op2::wait_state_type;
        struct wait_state_type
        {
            wait_state_type_1 state_1;
            wait_state_type_2 state_2;
        };

        explicit combine_read_op(const Op1 & op1, const Op2 & op2): m_op1(op1), m_op2(op2)
        {}

        [[nodiscard]] auto submit_if_ready() -> std::optional<std::size_t>
        {
            if (auto res = m_op1.submit_if_ready())
            {
                return res;
            }
            if (auto res = m_op2.submit_if_ready())
            {
                return Op1::num_alternatives + *res;
            }
            return std::nullopt;
        }

        [[nodiscard]] auto submit_with_wait(
            asiochan::detail::select_wait_context<executor_type>& select_ctx,
            asiochan::detail::select_waiter_token const base_token,
            wait_state_type& wait_state)
            -> std::optional<std::size_t>
        {
            if (auto res = m_op1.submit_with_wait(select_ctx, base_token, wait_state.state_1))
            {
                return res;
            }
            if (auto res = m_op2.submit_with_wait(select_ctx, base_token + Op1::num_alternatives, wait_state.state_2))
            {
                return *res + Op1::num_alternatives;
            }
            return std::nullopt;
        }

        void clear_wait(
            std::optional<std::size_t> const successful_alternative,
            wait_state_type& wait_state)
        {
            if (successful_alternative)
            {
                if (*successful_alternative >= Op1::num_alternatives)
                {
                    m_op1.clear_wait(std::nullopt, wait_state.state_1);
                    m_op2.clear_wait(*successful_alternative - Op1::num_alternatives, wait_state.state_2);
                }
                else
                {
                    m_op1.clear_wait(successful_alternative, wait_state.state_1);
                    m_op2.clear_wait(std::nullopt, wait_state.state_2);
                }
            }
            else
            {
                m_op1.clear_wait(std::nullopt, wait_state.state_1);
                m_op2.clear_wait(std::nullopt, wait_state.state_2);
            }
        }

        [[nodiscard]] auto get_result(std::size_t const successful_alternative) noexcept -> result_type
        {
            if (successful_alternative >= Op1::num_alternatives)
            {
                return m_op2.get_result(successful_alternative - Op1::num_alternatives);
            }
            else
            {
                return m_op1.get_result(successful_alternative);
            }
        }

    private:
        Op1 m_op1;
        Op2 m_op2;
    };

    template<asiochan::select_op Op>
    constexpr bool is_void_read_op = std::is_same_v<asiochan::read_result<void>, std::decay_t<typename Op::result_type>>;

    template<asiochan::select_op Op, asiochan::select_op... Ops>
    constexpr bool none_is_void_read_op()
    {
        if constexpr (sizeof...(Ops) == 0)
            return !is_void_read_op<Op>;
        else
            return !is_void_read_op<Op> && none_is_void_read_op<Ops...>();
    }

    template <asiochan::select_op First_Op, asiochan::select_op... Ops,
              asio::execution::executor Executor = typename First_Op::executor_type>
    requires asiochan::waitable_selection<First_Op, Ops...>
    constexpr auto select_(First_Op && first_op, Ops && ... other_ops)
    {
        if constexpr (sizeof...(Ops) == 0)
        {
            return asiochan::select(std::forward<First_Op>(first_op));
        }
        else
        {
            return asiochan::select(std::forward<First_Op>(first_op), std::forward<Ops>(other_ops)...);
        }
    }

    template <asiochan::select_op First_Op, asiochan::select_op... Ops,
              asio::execution::executor Executor = typename First_Op::executor_type>
    requires asiochan::waitable_selection<First_Op, Ops...>
    [[nodiscard]] auto select(close_chan close_ch, First_Op && first_op, Ops && ... other_ops) -> asio::awaitable<cancelable_select_result<typename First_Op::result_type, typename Ops::result_type...>>
    {
        if constexpr (sizeof...(Ops) > 0)
        {
            static_assert(none_is_void_read_op<Ops...>(), "None of other_ops could be asiochan::ops::read<void>, use first_op instead.");
        }
        if (is_valid_close_chan(close_ch))
        {
            co_await close_ch.init_timer();
            if (auto waiter_opt = close_ch.get_waiter())
            {
                if constexpr (is_void_read_op<First_Op>)
                {   
                    auto && res = co_await select_(
                        combine_read_op<void, asiochan::ops::read<void, close_chan::Waiter>, std::decay_t<First_Op>>(
                            asiochan::ops::read(*waiter_opt),
                            std::forward<First_Op>(first_op)
                        ),
                        std::forward<Ops>(other_ops)...
                    );
                    if (res.received_from(*waiter_opt))
                    {
                        if (!close_ch.is_closed())
                        {
                            cfgo::Log::instance().default_logger()->error("this should not happened. num of other ops: {}", sizeof...(Ops));
                        }
                        co_return make_canceled_select_result<First_Op, Ops...>();
                    }
                    else
                    {
                        if (auto stop_waiter = close_ch.get_stop_waiter())
                        {
                            co_await stop_waiter->read();
                        }
                        if (close_ch.is_closed() && !close_ch.is_timeout())
                        {
                            co_return make_canceled_select_result<First_Op, Ops...>();
                        }
                        co_return cancelable_select_result<typename First_Op::result_type, typename Ops::result_type...>(std::move(res).to_variant());
                    }
                }
                else
                {
                    auto res = co_await select_(
                        asiochan::ops::read(*waiter_opt),
                        std::forward<First_Op>(first_op),
                        std::forward<Ops>(other_ops)...
                    );
                    if (res.received_from(*waiter_opt))
                    {
                        co_return make_canceled_select_result<First_Op, Ops...>();
                    }
                    else
                    {
                        if (auto stop_waiter = close_ch.get_stop_waiter())
                        {
                            co_await stop_waiter->read();
                        }
                        if (close_ch.is_closed() && !close_ch.is_timeout())
                        {
                            co_return make_canceled_select_result<First_Op, Ops...>();
                        }
                        co_return cancelable_select_result<typename First_Op::result_type, typename Ops::result_type...>(
                            magic::shift_variant(std::move(res).to_variant())
                        );
                    }
                }
            }
            else
            {
                co_return make_canceled_select_result<First_Op, Ops...>();
            }
        }
        else
        {
            auto && res = co_await select_(std::forward<First_Op>(first_op), std::forward<Ops>(other_ops)...);
            co_return cancelable_select_result<typename First_Op::result_type, typename Ops::result_type...>(std::move(res).to_variant());
        }
    }

    template <asiochan::select_op First_Op, asiochan::select_op... Ops,
              asio::execution::executor Executor = typename First_Op::executor_type>
    requires asiochan::waitable_selection<First_Op, Ops...>
    [[nodiscard]] auto select_or_throw(close_chan close_ch, First_Op && first_op, Ops && ... other_ops) -> asio::awaitable<select_result<typename First_Op::result_type, typename Ops::result_type...>>
    {
        if constexpr (sizeof...(Ops) > 0)
        {
            static_assert(none_is_void_read_op<Ops...>(), "None of other_ops could be asiochan::ops::read<void>, use first_op instead.");
        }
        if (is_valid_close_chan(close_ch))
        {
            co_await close_ch.init_timer();
            if (auto waiter_opt = close_ch.get_waiter())
            {
                if constexpr (is_void_read_op<First_Op>)
                {   
                    auto && res = co_await select_(
                        combine_read_op<void, asiochan::ops::read<void, close_chan::Waiter>, std::decay_t<First_Op>>(
                            asiochan::ops::read(*waiter_opt),
                            std::forward<First_Op>(first_op)
                        ),
                        std::forward<Ops>(other_ops)...
                    );
                    if (res.received_from(*waiter_opt))
                    {
                        throw CancelError(close_ch.is_timeout());
                    }
                    else
                    {
                        if (auto stop_waiter = close_ch.get_stop_waiter())
                        {
                            co_await stop_waiter->read();
                        }
                        if (close_ch.is_closed() && !close_ch.is_timeout())
                        {
                            throw CancelError(true);
                        }
                        co_return select_result<typename First_Op::result_type, typename Ops::result_type...>(std::move(res).to_variant());
                    }
                }
                else
                {
                    auto res = co_await select_(
                        asiochan::ops::read(*waiter_opt),
                        std::forward<First_Op>(first_op),
                        std::forward<Ops>(other_ops)...
                    );
                    if (res.received_from(*waiter_opt))
                    {
                        throw CancelError(close_ch.is_timeout());
                    }
                    else
                    {
                        if (auto stop_waiter = close_ch.get_stop_waiter())
                        {
                            co_await stop_waiter->read();
                        }
                        if (close_ch.is_closed() && !close_ch.is_timeout())
                        {
                            throw CancelError(true);
                        }
                        co_return select_result<typename First_Op::result_type, typename Ops::result_type...>(magic::shift_variant(std::move(res).to_variant()));
                    }
                }
            }
            else
            {
                throw CancelError(close_ch.is_timeout());
            }
        }
        else
        {
            auto && res = co_await select_(std::forward<First_Op>(first_op), std::forward<Ops>(other_ops)...);
            co_return select_result<typename First_Op::result_type, typename Ops::result_type...>(std::move(res).to_variant());
        }
    }

    template<typename T>
    auto chan_read(asiochan::readable_channel_type<T> auto ch, close_chan close_ch = INVALID_CLOSE_CHAN) -> asio::awaitable<cancelable<T>> {
        if (is_valid_close_chan(close_ch))
        {
            auto && res = co_await select(
                close_ch,
                asiochan::ops::read(ch)
            );
            if (!res)
            {
                co_return make_canceled<T>();
            }
            else
            {
                if constexpr(std::is_void_v<T>)
                {
                    co_return make_resolved();
                }
                else
                {
                    co_return std::move(res).template get_received<T>();
                }
            }
        }
        else
        {
            if constexpr(std::is_void_v<T>)
            {
                co_await ch.read();
                co_return make_resolved();
            }
            else
            {
                co_return make_resolved(std::forward<T>(co_await ch.read()));
            }
        }
    }

    template<typename T>
    auto chan_read_or_throw(asiochan::readable_channel_type<T> auto ch, close_chan close_ch = INVALID_CLOSE_CHAN) -> asio::awaitable<T> {
        if (is_valid_close_chan(close_ch))
        {
            auto && res = co_await select_or_throw(
                close_ch,
                asiochan::ops::read(ch)
            );
            if constexpr(std::is_void_v<T>)
            {
                co_return;
            }
            else
            {
                co_return std::move(res).template get_received<T>();
            }
        }
        else
        {
            if constexpr(std::is_void_v<T>)
            {
                co_await ch.read();
                co_return;
            }
            else
            {
                co_return co_await ch.read();
            }
        }
    }

    template<typename T>
    auto chan_write(asiochan::writable_channel_type<T> auto ch, T && data, close_chan close_ch = INVALID_CLOSE_CHAN) -> asio::awaitable<cancelable<void>> {
        if (is_valid_close_chan(close_ch))
        {
            auto && res = co_await select(
                close_ch,
                asiochan::ops::write(std::forward<T>(data), ch)
            );
            if (!res)
            {
                co_return make_canceled<T>();
            }
            else
            {
                co_return make_resolved();
            }
        }
        else
        {
            co_await ch.write(std::forward<T>(data));
            co_return make_resolved();
        }
    }

    template<typename T>
    auto chan_write_or_throw(asiochan::writable_channel_type<T> auto ch, T && data, close_chan close_ch = INVALID_CLOSE_CHAN) -> asio::awaitable<void> {
        if (is_valid_close_chan(close_ch))
        {
            co_await select_or_throw(
                close_ch,
                asiochan::ops::write(std::forward<T>(data), ch)
            );
        }
        else
        {
            co_await ch.write(std::forward<T>(data));
        }
    }

    template<asiochan::writable_channel_type<void> CH>
    void chan_must_write(CH & ch)
    {
        if (!ch.try_write())
        {
            throw cpptrace::runtime_error("Write chan failed. This should not happened.");
        }
    }

    template<typename T, asiochan::writable_channel_type<std::decay_t<T>> CH>
    void chan_must_write(CH & ch, T && value)
    {
        if (!ch.try_write(std::forward<T>(value)))
        {
            throw cpptrace::runtime_error("Write chan failed. This should not happened.");
        }
    }

    template<asiochan::writable_channel_type<void> CH>
    void chan_maybe_write(CH & ch)
    {
        std::ignore = ch.try_write();
    }

    template<typename T, asiochan::writable_channel_type<std::decay_t<T>> CH>
    void chan_maybe_write(CH & ch, T && value)
    {
        std::ignore = ch.try_write(std::forward<T>(value));
    }

    template<typename T>
    auto async_retry(
        std::chrono::nanoseconds timeout,
        const TryOption & option, 
        std::function<asio::awaitable<T>(int, close_chan)> && func, 
        std::function<bool(const T &)> retry_checker, 
        close_chan close_ch,
        std::string timeout_reason = ""
    ) -> asio::awaitable<cancelable<T>>
    {
        if (!is_valid_close_chan(close_ch))
        {
            throw cpptrace::runtime_error("The input close_ch arg must be a valid closer.");
        }
        auto tried = option.m_tries, tries = option.m_tries;
        auto delay_init = option.m_delay_init;
        auto delay_step = option.m_delay_step;
        auto delay_level = option.m_delay_level > 16 ? 16 : option.m_delay_level;
        std::uint32_t delay_current_level = 0;
        do
        {
            close_chan timeout_closer = close_ch.create_child();
            timeout_closer.set_timeout(timeout, std::string(timeout_reason));
            auto res = co_await func(tries - tried + 1, timeout_closer);
            if (retry_checker(res) && timeout_closer.is_timeout())
            {
                if (tried == 0)
                {
                    co_return make_canceled<T>();
                }
                else if (tried > 0)
                {
                    --tried;
                }
                auto delay = delay_init;
                if (delay_current_level > 0)
                {
                    delay += delay_step * (1 << (delay_current_level - 1));
                }
                if (delay_current_level < delay_level)
                {
                    ++delay_current_level;
                }
                if (delay > 0)
                {
                    auto timer = close_ch.create_child();
                    timer.set_timeout(std::chrono::milliseconds {delay});
                    bool closed = co_await timer.await();
                    if (closed)
                    {
                        co_return make_canceled<T>();
                    }
                }
            }
            co_return make_resolved<T>(std::forward<T>(res));
        } while (true);
    }

    template<typename T, typename AT>
    class AsyncTasksBase
    {
    protected:
        using TaskType = std::function<asio::awaitable<T>(close_chan closer)>;
        using DataTypeT = std::tuple<int, std::optional<T>, std::exception_ptr>;
        using DataTypeVoid = std::tuple<int, std::exception_ptr>;
        using DataType = std::conditional_t<std::is_void_v<T>, DataTypeVoid, DataTypeT>;
        close_chan m_close_ch;
        asiochan::channel<DataType> m_data_ch;
        close_chan m_done_signal;
        std::vector<TaskType> m_tasks;
        mutex m_mutex;
        bool m_start;
        std::exception_ptr m_internal_err;

        void _should_not_started()
        {
            if (m_start)
            {
                throw cpptrace::logic_error("Forbidden operation. The async parallel tasks have started.");
            }
        }

        virtual auto _sync() -> asio::awaitable<void> = 0;
        virtual AT _collect_result() = 0;
    public:
        AsyncTasksBase(const close_chan & close_ch): m_start(false)
        {
            if (is_valid_close_chan(close_ch))
            {
                m_close_ch = close_ch.create_child();
            }
            else
            {
                m_close_ch = close_chan {};
            }
        }

        virtual ~AsyncTasksBase() = default;

        void add_task(TaskType task)
        {
            std::lock_guard lock(m_mutex);
            _should_not_started();
            m_tasks.push_back(task);
        }

        auto await() -> asio::awaitable<AT>
        {
            bool first_start = false;
            {
                std::lock_guard lock(m_mutex);
                if (!m_start)
                {
                    m_start = true;
                    first_start = true;
                }
            }
            if (first_start)
            {
                auto executor = co_await asio::this_coro::executor;
                int i = 0;
                for (auto && task : m_tasks)
                {
                    asio::co_spawn(
                        executor,
                        fix_async_lambda([i, close_ch = m_close_ch, data_ch = m_data_ch, task]() mutable -> asio::awaitable<T>
                        {
                            std::exception_ptr except = nullptr;
                            try
                            {
                                if constexpr (std::is_void_v<T>)
                                {
                                    co_await task(close_ch);
                                    cfgo::Log::instance().default_logger()->trace("task done.");
                                    co_await chan_write_or_throw<DataType>(data_ch, DataType(i, nullptr), close_ch);
                                    co_return;
                                }
                                else
                                {
                                    auto res = co_await task(close_ch);
                                    cfgo::Log::instance().default_logger()->trace("task done.");
                                    co_await chan_write_or_throw<DataType>(data_ch, DataType(i, res, nullptr), close_ch);
                                    co_return res;
                                }
                            }
                            catch(...)
                            {
                                except = std::current_exception();                                 
                            }
                            bool closed = false;
                            if (!close_ch.is_closed())
                            {
                                if constexpr (std::is_void_v<T>)
                                {
                                    closed = !co_await chan_write<DataType>(data_ch, std::make_tuple(i, except), close_ch);
                                    cfgo::Log::instance().default_logger()->trace("except writed with closer {}", closed ? "closed" : "not closed");
                                }
                                else
                                {
                                    closed = !co_await chan_write<DataType>(data_ch, std::make_tuple(i, std::nullopt, except), close_ch);
                                    cfgo::Log::instance().default_logger()->trace("except writed with closer {}", closed ? "closed" : "not closed");
                                }
                            }
                            cfgo::Log::instance().default_logger()->trace("task exit.");
                        }),
                        asio::detached
                    );
                    ++i;
                }
                try
                {
                    co_await _sync();
                    m_done_signal.close_no_except();
                }
                catch(...)
                {
                    m_internal_err = std::current_exception();
                    m_done_signal.close_no_except();
                }
                m_close_ch.close_no_except();
            }
            co_await m_done_signal.await();
            if (m_internal_err)
            {
                std::rethrow_exception(m_internal_err);
            }
            co_return _collect_result();
        }
    };

    template<typename T>
    class AsyncTasksAll : public AsyncTasksBase<T, std::vector<T>>
    {
        using PT = AsyncTasksBase<T, std::vector<T>>;
    private:
        std::vector<std::optional<T>> m_result;
    protected:
        auto _sync() -> asio::awaitable<void>
        {
            auto n = PT::m_tasks.size();
            m_result = std::vector<std::optional<T>>(n, std::nullopt);
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, opt_value, except] = res.value();
                    if (except)
                    {
                        // PT::m_close_ch.close("Some task of \"All Group\" tasks failed.");
                        std::rethrow_exception(except);
                    }
                    else
                    {
                        m_result[index] = std::move(opt_value);
                    }
                }
                else
                {
                    throw CancelError(PT::m_close_ch);
                }
            }
            co_return;
        }

        auto _collect_result() -> std::vector<T>
        {
            auto n = PT::m_tasks.size();
            std::vector<T> result;
            for (size_t i = 0; i < n; i++)
            {
                result.push_back(*m_result[i]);
            }
            return result;
        }

    public:
        AsyncTasksAll(const close_chan & close_ch = INVALID_CLOSE_CHAN): PT(close_ch) {}
        virtual ~AsyncTasksAll() = default;
    };

    template<>
    class AsyncTasksAll<void> : public AsyncTasksBase<void, void>
    {
        using PT = AsyncTasksBase<void, void>;
    protected:
        auto _sync() -> asio::awaitable<void> override
        {
            auto n = PT::m_tasks.size();
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, except] = res.value();
                    if (except)
                    {
                        // PT::m_close_ch.close("Some task of \"All Group\" tasks failed.");
                        std::rethrow_exception(except);
                    }
                }
                else
                {
                    throw CancelError(PT::m_close_ch);
                }
            }
            co_return;
        }

        void _collect_result() override
        {}

    public:
        AsyncTasksAll(const close_chan & close_ch = INVALID_CLOSE_CHAN): PT(close_ch) {}
        virtual ~AsyncTasksAll() = default;
    };

    template<typename T>
    class AsyncTasksAny : public AsyncTasksBase<T, T>
    {
        using PT = AsyncTasksBase<T, T>;
    private:
        std::optional<T> m_result;
        std::vector<std::exception_ptr> m_excepts;
    protected:
        auto _sync() -> asio::awaitable<void> override
        {
            auto n = PT::m_tasks.size();
            m_excepts = std::vector<std::exception_ptr>(n, nullptr);
            bool accepted = false;
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, opt_value, except] = res.value();
                    if (except)
                    {
                        m_excepts[index] = except;
                    }
                    else
                    {
                        accepted = true;
                        // PT::m_close_ch.close("Some task of \"Any Group\" tasks succeed.");
                        m_result = std::move(opt_value);
                        break;
                    }
                }
                else
                {
                    try
                    {
                        throw CancelError(PT::m_close_ch);
                    }
                    catch(const std::exception& e)
                    {
                        for (size_t j = 0; j < n; j++)
                        {
                            if (!m_excepts[j])
                            {
                                m_excepts[j] = std::current_exception();
                            }
                        }
                        break;
                    }
                }
            }
            if (!accepted)
            {
                if (!m_excepts.empty())
                {
                    std::rethrow_exception(m_excepts[0]);
                }
                else
                {
                    throw std::logic_error("The empty \"Any group\" tasks always throw.");
                }
            }
            co_return;
        }

        auto _collect_result() -> T override
        {
            return *m_result;
        }

    public:
        AsyncTasksAny(const close_chan & close_ch = INVALID_CLOSE_CHAN): PT(close_ch) {}
        virtual ~AsyncTasksAny() = default;
    };

    template<>
    class AsyncTasksAny<void> : public AsyncTasksBase<void, void>
    {
        using PT = AsyncTasksBase<void, void>;
    private:
        std::vector<std::exception_ptr> m_excepts;
    protected:
        auto _sync() -> asio::awaitable<void> override
        {
            auto n = PT::m_tasks.size();
            m_excepts = std::vector<std::exception_ptr>(n, nullptr);
            bool accepted = false;
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, except] = res.value();
                    if (except)
                    {
                        m_excepts[index] = except;
                    }
                    else
                    {
                        accepted = true;
                        // PT::m_close_ch.close("Some task of \"Any Group\" tasks succeed.");
                        break;
                    }
                }
                else
                {
                    try
                    {
                        throw CancelError(PT::m_close_ch);
                    }
                    catch(const std::exception& e)
                    {
                        for (size_t j = 0; j < n; j++)
                        {
                            if (!m_excepts[j])
                            {
                                m_excepts[j] = std::current_exception();
                            }
                        }
                        break;
                    }
                }
            }
            if (!accepted)
            {
                if (!m_excepts.empty())
                {
                    std::rethrow_exception(m_excepts[0]);
                }
                else
                {
                    throw std::logic_error("The empty \"Any group\" tasks always throw.");
                }
            }
            co_return;
        }

        void _collect_result() override
        {}

    public:
        AsyncTasksAny(const close_chan & close_ch = INVALID_CLOSE_CHAN): PT(close_ch) {}
        virtual ~AsyncTasksAny() = default;
    };

    template<typename T>
    class AsyncTasksSome : public AsyncTasksBase<T, std::map<int, T>>
    {
    public:
        using PT = AsyncTasksBase<T, std::map<int, T>>;
        AsyncTasksSome(std::uint32_t n, const close_chan & close_ch = INVALID_CLOSE_CHAN): m_target(n), PT(close_ch) {}
        virtual ~AsyncTasksSome() = default;
    private:
        std::uint32_t m_target;
        std::map<int, T> m_result;
    protected:
        auto _sync() -> asio::awaitable<void>
        {
            auto n = PT::m_tasks.size();
            if (m_target > n)
            {
                throw cpptrace::runtime_error("The target is greater than the number of tasks.");
            }
            
            std::uint32_t succeed = 0;
            std::uint32_t failed = 0;
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, opt_value, except] = res.value();
                    if (except)
                    {
                        ++failed;
                    }
                    else
                    {
                        m_result.insert(std::make_pair(index, *opt_value));
                        ++succeed;
                    }
                    if (failed > n - m_target)
                    {
                        std::rethrow_exception(except);
                    }
                    if (succeed == m_target)
                    {
                        break;
                    }
                }
                else
                {
                    throw CancelError(PT::m_close_ch);
                }
            }
            co_return;
        }

        auto _collect_result() -> std::vector<std::optional<T>>
        {
            return m_result;
        }
    };

    template<>
    class AsyncTasksSome<void> : public AsyncTasksBase<void, std::set<int>>
    {
    public:
        using PT = AsyncTasksBase<void, std::set<int>>;
        AsyncTasksSome(std::uint32_t n, const close_chan & close_ch = INVALID_CLOSE_CHAN): m_target(n), PT(close_ch) {}
        virtual ~AsyncTasksSome() = default;
    private:
        std::uint32_t m_target;
        std::set<int> m_result;
    protected:
        auto _sync() -> asio::awaitable<void>
        {
            auto n = PT::m_tasks.size();
            if (m_target > n)
            {
                throw cpptrace::runtime_error("The target is greater than the number of tasks.");
            }
            
            std::uint32_t succeed = 0;
            std::uint32_t failed = 0;
            for (size_t i = 0; i < n; i++)
            {
                auto res = co_await chan_read<typename PT::DataType>(PT::m_data_ch, PT::m_close_ch);
                if (res)
                {
                    const auto & [index, except] = res.value();
                    if (except)
                    {
                        ++failed;
                    }
                    else
                    {
                        m_result.insert(index);
                        ++succeed;
                    }
                    if (failed > n - m_target)
                    {
                        std::rethrow_exception(except);
                    }
                    if (succeed == m_target)
                    {
                        break;
                    }
                }
                else
                {
                    throw CancelError(PT::m_close_ch);
                }
            }
            co_return;
        }

        auto _collect_result() -> std::set<int>
        {
            return m_result;
        }
    };

    template<typename T>
    class manually_ptr
    {
    public:
        template<typename... Args>
        manually_ptr(Args &&... args): m_data(std::forward<Args>(args)...), m_ref_count(1) {}
        manually_ptr(const manually_ptr &) = delete;
        manually_ptr & operator = (const manually_ptr &) = delete;
        void ref()
        {
            std::lock_guard lk(m_mutex);
            ++m_ref_count;
        }
        T & data()
        {
            return m_data;
        }
        const T & data() const
        {
            return m_data;
        }
        T & operator -> ()
        {
            return m_data;
        }
        const T & operator -> () const
        {
            return m_data;
        }
        friend void manually_ptr_unref<T>(manually_ptr<T> ** ptr);
    private:
        std::uint32_t m_ref_count;
        mutex m_mutex;
        T m_data;
    };

    template<typename T, typename... Args>
    manually_ptr<T> * make_manually_ptr(Args && ...args)
    {
        return new manually_ptr<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    void manually_ptr_unref(manually_ptr<T> ** ptr)
    {
        if (ptr == nullptr || *ptr == nullptr)
        {
            return;
        }
        {
            std::lock_guard lk((*ptr)->m_mutex);
            --(*ptr)->m_ref_count;
        }
        if ((*ptr)->m_ref_count == 0)
        {
            delete *ptr;
            *ptr = nullptr;
        }
    }

    template<typename T>
    struct shared_ptr_holder
    {
        std::shared_ptr<T> m_ptr;

        shared_ptr_holder(const std::shared_ptr<T> & ptr): m_ptr(ptr) {}
        shared_ptr_holder(std::shared_ptr<T> && ptr): m_ptr(std::move(ptr)) {}
        shared_ptr_holder(const shared_ptr_holder &) = delete;
        shared_ptr_holder & operator = (const shared_ptr_holder &) = delete;

        std::shared_ptr<T> & operator -> ()
        {
            return m_ptr;
        }

        const std::shared_ptr<T> & operator -> () const
        {
            return m_ptr;
        }
    };

    template<typename T>
    shared_ptr_holder<T> * make_shared_holder(const std::shared_ptr<T> & ptr)
    {
        return new shared_ptr_holder<T>(ptr);
    }

    template<typename T>
    shared_ptr_holder<T> * make_shared_holder(std::shared_ptr<T> && ptr)
    {
        return new shared_ptr_holder<T>(std::move(ptr));
    }

    template<typename T>
    inline shared_ptr_holder<T> & cast_shared_holder_ref(void * ptr)
    {
        auto holder_ptr = static_cast<shared_ptr_holder<T> *>(ptr);
        return *holder_ptr;
    } 

    template<typename T>
    void destroy_shared_holder(shared_ptr_holder<T> * holder_ptr)
    {
        if (holder_ptr)
            delete holder_ptr;
    }

    template<typename T>
    inline void destroy_shared_holder(void * holder_ptr)
    {
        destroy_shared_holder<T>(static_cast<shared_ptr_holder<T> *>(holder_ptr));
    }

    template<typename T>
    struct weak_ptr_holder
    {
        std::weak_ptr<T> m_ptr;

        weak_ptr_holder(std::weak_ptr<T> && ptr): m_ptr(std::move(ptr)) {}
        weak_ptr_holder(const weak_ptr_holder &) = delete;
        weak_ptr_holder & operator = (const weak_ptr_holder &) = delete;

        std::shared_ptr<T> lock() const noexcept
        {
            return m_ptr.lock();
        }
    };

    template<typename T>
    weak_ptr_holder<T> * make_weak_holder(std::weak_ptr<T> && ptr)
    {
        return new weak_ptr_holder<T>(std::move(ptr));
    }

    template<typename T>
    inline weak_ptr_holder<T> * cast_weak_holder(void * ptr)
    {
        return static_cast<weak_ptr_holder<T> *>(ptr);
    } 

    template<typename T>
    inline weak_ptr_holder<T> & cast_weak_holder_ref(void * ptr)
    {
        auto holder_ptr = cast_weak_holder<T>(ptr);
        return *holder_ptr;
    } 

    template<typename T>
    void destroy_weak_holder(weak_ptr_holder<T> * holder_ptr)
    {
        if (holder_ptr)
            delete holder_ptr;
    }

    template<typename T>
    inline void destroy_weak_holder(void * holder_ptr)
    {
        destroy_weak_holder<T>(static_cast<weak_ptr_holder<T> *>(holder_ptr));
    }

} // namespace cfgo


#endif