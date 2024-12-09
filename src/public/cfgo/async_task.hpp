#ifndef _CFGO_ASYNC_TASK_HPP_
#define _CFGO_ASYNC_TASK_HPP_

#include "cfgo/task_pool.hpp"
#include "cfgo/async.hpp"

namespace cfgo
{
    template <asio_execution_context ExecutionContext, typename F>
    requires requires (ExecutionContext& ctx, F&& f) {
        asio::co_spawn(ctx, std::forward<F>(f), asio::detached);
    }
    void submit_async_task(ExecutionContext& ctx, F&& f)
    {
        asio::co_spawn(ctx, std::forward<F>(f), asio::detached);
    }

    template <asio_executor Executor, typename F>
    requires requires (const Executor& executor, F&& f) {
        asio::co_spawn(executor, std::forward<F>(f), asio::detached);
    }
    void submit_async_task(const Executor& executor, F&& f)
    {
        asio::co_spawn(executor, std::forward<F>(f), asio::detached);
    }

    template <typename F>
    requires requires (F && f) {
        asio::co_spawn(std::declval<asio::any_io_executor>(), std::forward<F>(f), asio::detached);
    }
    auto async_submit_async_task(F&& f) -> asio::awaitable<void>
    {
        auto executor = co_await asio::this_coro::executor;
        co_await ThreadPool::global_instance()->enqueue_async([executor = std::move(executor), f = std::move(f)]() {
            asio::co_spawn(executor, std::move(f), asio::detached);
        });
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
        asiochan::unbounded_channel<DataType> m_data_ch;
        std::vector<TaskType> m_tasks;
        mutex m_mutex;
        bool m_start;

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
        AsyncTasksBase(const close_chan & close_ch): m_close_ch(close_ch), m_start(false)
        {}

        virtual ~AsyncTasksBase() = default;

        void add_task(TaskType task)
        {
            std::lock_guard lock(m_mutex);
            _should_not_started();
            m_tasks.push_back(std::move(task));
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
                int i = 0;
                for (auto && task : m_tasks)
                {
                    co_await async_submit_async_task(
                        fix_async_lambda([i, close_ch = m_close_ch, data_ch = m_data_ch, task]() mutable -> asio::awaitable<void>
                        {
                            std::exception_ptr except = nullptr;
                            try
                            {
                                if constexpr (std::is_void_v<T>)
                                {
                                    co_await task(close_ch);
                                    CFGO_TRACE("task {} done.", i);
                                    data_ch.write(std::make_tuple(i, nullptr));
                                    co_return;
                                }
                                else
                                {
                                    auto res = co_await task(close_ch);
                                    CFGO_TRACE("task {} done.", i);
                                    data_ch.write(std::make_tuple(i, std::move(res), nullptr));
                                    co_return;
                                }
                            }
                            catch(...)
                            {
                                except = std::current_exception();
                                if constexpr (std::is_void_v<T>)
                                {
                                    data_ch.write(std::make_tuple(i, except));
                                }
                                else
                                {
                                    data_ch.write(std::make_tuple(i, std::nullopt, except));
                                }                         
                            }
                            CFGO_TRACE("task {} exit.", i);
                        })
                    );
                    ++i;
                }
            }
            // auto start = std::chrono::high_resolution_clock::now();
            co_await _sync();
            // CFGO_TRACE("sync use time {} ms.", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count());
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
    class AsyncTasksSome : public AsyncTasksBase<T, std::unordered_map<int, T>>
    {
    public:
        using PT = AsyncTasksBase<T, std::unordered_map<int, T>>;
        AsyncTasksSome(std::uint32_t n, const close_chan & close_ch = INVALID_CLOSE_CHAN): m_target(n), PT(close_ch) {}
        virtual ~AsyncTasksSome() = default;
    private:
        std::uint32_t m_target;
        std::unordered_map<int, T> m_result;
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

        auto _collect_result() -> std::unordered_map<int, T>
        {
            return m_result;
        }
    };

    template<>
    class AsyncTasksSome<void> : public AsyncTasksBase<void, std::unordered_set<int>>
    {
    public:
        using PT = AsyncTasksBase<void, std::unordered_set<int>>;
        AsyncTasksSome(std::uint32_t n, const close_chan & close_ch = INVALID_CLOSE_CHAN): PT(close_ch), m_target(n) {}
        virtual ~AsyncTasksSome() = default;
    private:
        std::uint32_t m_target;
        std::unordered_set<int> m_result;
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

        auto _collect_result() -> std::unordered_set<int>
        {
            return m_result;
        }
    };
} // namespace cfgo

#endif