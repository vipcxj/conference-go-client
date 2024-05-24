#ifndef _CFGO_ASYNC_LOCKER_HPP_
#define _CFGO_ASYNC_LOCKER_HPP_

#include "asio/awaitable.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/async.hpp"
#include "cfgo/coroutine_concepts.hpp"
#include <mutex>
#include <memory>
#include <cstdint>

namespace cfgo
{
    template<typename T>
    concept async_mutex = 
        !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T> &&
        !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T> &&
        requires(T & mutex, close_chan & closer) {
            { mutex.lock(closer) } -> Awaitable;
            mutex.try_lock() ? void() : void();
            mutex.unlock();
        };

    template<async_mutex M>
    class AsyncLockGuard
    {
    public:
        using mutex_type = M;
        AsyncLockGuard(mutex_type & mutex) noexcept: m_mutex(mutex), m_locked(false) {}
        AsyncLockGuard(mutex_type & mutex, std::adopt_lock_t) noexcept: m_mutex(mutex), m_locked(true) {}
        AsyncLockGuard(const AsyncLockGuard &) = delete;
        ~AsyncLockGuard()
        {
            if (m_locked)
            {
                m_mutex.unlock();
            }
        }

        AsyncLockGuard & operator = (const AsyncLockGuard &) = delete;

        template<Awaitable AW = asio::awaitable<void>>
        auto lock(close_chan closer = INVALID_CLOSE_CHAN) -> AW
        {
            if (!m_locked)
            {
                co_await m_mutex.lock(closer);
                m_locked = true;
            }
            co_return;
        }
    private:
        mutex_type & m_mutex;
        bool m_locked;
    };

    template<async_mutex M>
    class AsyncUniqueLock
    {
    public:
        using mutex_type = M;
        
        AsyncUniqueLock() noexcept: m_mutex(nullptr), m_locked(false) {}
        
        explicit AsyncUniqueLock(mutex_type& m): m_mutex(std::addressof(m)), m_locked(false) {}
        
        AsyncUniqueLock(mutex_type& m, std::try_to_lock_t): m_mutex(std::addressof(m)), m_locked(m_mutex->try_lock()) {}
        
        AsyncUniqueLock(mutex_type& m, std::adopt_lock_t) noexcept: m_mutex(std::addressof(m)), m_locked(true) {}

        ~AsyncUniqueLock()
        {
            unlock();
        }

        AsyncUniqueLock(const AsyncUniqueLock &) = delete;
        AsyncUniqueLock & operator = (const AsyncUniqueLock &) = delete;
        
        AsyncUniqueLock(AsyncUniqueLock && u) noexcept: m_mutex(u.m_mutex), m_locked(u.m_locked)
        {
            u.m_mutex = nullptr;
            u.m_locked = false;
        }
        AsyncUniqueLock & operator = (AsyncUniqueLock && u) noexcept
        {
            unlock();
            m_mutex = u.m_mutex;
            m_locked = u.m_locked;
            u.m_mutex = nullptr;
            u.m_locked = false;
            return *this;
        }

        template<Awaitable AW = asio::awaitable<void>>
        auto lock(close_chan closer = INVALID_CLOSE_CHAN) -> AW
        {
            if (m_mutex && !m_locked)
            {
                co_await m_mutex->lock(closer);
                m_locked = true;
            }
            co_return;
        }

        bool try_lock()
        {
            if (m_mutex && !m_locked)
            {
                m_locked = m_mutex->try_lock();
            }
            return m_locked;
        }

        void unlock() noexcept
        {
            if (m_mutex && m_locked)
            {
                m_mutex->unlock();
            }
        }
        
        void swap(AsyncUniqueLock & u) noexcept
        {
            std::swap(m_mutex, u.m_mutex);
            std::swap(m_locked, u.m_locked);
        }
        
        mutex_type* release() noexcept
        {
            mutex_type* ret = m_mutex;
            m_mutex = nullptr;
            m_locked = false;
            return ret;
        }
        
        bool owns_lock() const noexcept
        {
            return m_locked;
        }
        
        explicit operator bool() const noexcept
        {
            return owns_lock();
        }
        
        mutex_type* mutex() const noexcept
        {
            return m_mutex;
        }

    private:
        mutex_type * m_mutex;
        bool m_locked;
    };

    namespace detail
    {
        template<int Idx, bool Continue = true>
        struct try_async_lock_impl
        {
            template<async_mutex... Locks>
            static void do_try_lock(tuple<Locks & ...> & locks, int& idx)
            {
                idx = Idx;
                using Lock = decltype(std::get<Idx>(locks));
                AsyncUniqueLock<Lock> lock(std::get<Idx>(locks), std::try_to_lock);
                if (lock.owns_lock())
                {
                    constexpr bool cont = Idx + 2 < sizeof...(Locks);
                    using try_locker = try_async_lock_impl<Idx + 1, cont>;
                    try_locker::do_try_lock(locks, idx);
                    if (idx == -1)
                        lock.release();
                }
            }
        };

        template<int Idx>
        struct try_async_lock_impl<Idx, false>
        {
            template<async_mutex... Locks>
            static void do_try_lock(tuple<Locks & ...> & locks, int& idx)
            {
                idx = Idx;
                using Lock = decltype(std::get<Idx>(locks));
                AsyncUniqueLock<Lock> lock(std::get<Idx>(locks), std::try_to_lock);
                if (lock.owns_lock())
                {
                    idx = -1;
                    lock.release();
                }
            }
        };

        template<Awaitable AW, async_mutex M1, async_mutex M2, async_mutex... MS>
        auto async_lock(close_chan closer, M1 & m1, M2 & m2, MS & ... m3) -> AW
        {
            while (true)
            {
                using try_locker = try_async_lock_impl<0, sizeof...(MS) != 0>;
                AsyncUniqueLock<M1> first(m1);
                co_await first.lock(closer);
                int idx;
                auto locks = std::tie(m2, m3...);
                try_locker::do_try_lock(locks, idx);
                if (idx == -1)
                {
                    first.release();
                    co_return;
                }
            }
        }
    } // namespace detail
    

    template<async_mutex... MS>
    class AsyncScopedLock
    {
    public:
        explicit AsyncScopedLock(MS &... ms) noexcept: m_mutexs(std::tie(ms...)), m_locked(false) {}
        explicit AsyncScopedLock(std::adopt_lock_t, MS &... ms) noexcept: m_mutexs(std::tie(ms...)), m_locked(true) {}
        AsyncScopedLock(const AsyncScopedLock &) = delete;
        ~AsyncScopedLock()
        {
            if (m_locked)
            {
                std::apply([](auto &... m) { (m.unlock(), ...); }, m_mutexs);
            }
        }
        AsyncScopedLock & operator = (const AsyncScopedLock &) = delete;

        template<Awaitable AW = asio::awaitable<void>>
        auto lock(close_chan closer = INVALID_CLOSE_CHAN) -> AW
        {
            if (!m_locked)
            {
                std::scoped_lock l;
                co_await std::apply([closer](auto &...m) -> AW {
                    return detail::async_lock(closer, m...);
                }, m_mutexs);
                m_locked = true;
            }
            co_return;
        }
    private:
        tuple<MS&...> m_mutexs;
        bool m_locked;
    };

    template<>
    class AsyncScopedLock<>
    {
        explicit AsyncScopedLock() noexcept = default;
        explicit AsyncScopedLock(std::adopt_lock_t) noexcept {}
        AsyncScopedLock(const AsyncScopedLock &) = delete;
        ~AsyncScopedLock() = default;
        AsyncScopedLock & operator = (const AsyncScopedLock &) = delete;

        template<Awaitable AW = asio::awaitable<void>>
        auto lock(close_chan closer = INVALID_CLOSE_CHAN) -> AW
        {
            co_return;
        }
    };

    template<async_mutex M>
    class AsyncScopedLock<M>
    {
    public:
        explicit AsyncScopedLock(M & mutex) noexcept: m_mutex(mutex), m_locked(false) {}
        explicit AsyncScopedLock(std::adopt_lock_t, M & mutex) noexcept: m_mutex(mutex), m_locked(true) {}
        AsyncScopedLock(const AsyncScopedLock &) = delete;
        ~AsyncScopedLock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
            }
        }
        AsyncScopedLock & operator = (const AsyncScopedLock &) = delete;

        template<Awaitable AW = asio::awaitable<void>>
        auto lock(close_chan closer = INVALID_CLOSE_CHAN) -> AW
        {
            if (!m_locked)
            {
                co_await m_mutex.lock(closer);
                m_locked = true;
            }
            co_return;
        }
    private:
        M & m_mutex;
        bool m_locked;
    };

    namespace detail
    {
        class AsyncBlocker;
        using AsyncBlockerPtr = std::shared_ptr<detail::AsyncBlocker>;
        class AsyncBlockerManager;
    } // namespace detail

    class AsyncBlocker : public ImplBy<detail::AsyncBlocker>
    {
    public:
        using Ptr = std::shared_ptr<AsyncBlocker>;
        AsyncBlocker(detail::AsyncBlockerPtr impl);
        bool need_block() const noexcept;
        bool is_blocked() const noexcept;
        std::uint32_t id() const noexcept;
        void set_user_data(std::shared_ptr<void> user_data) const;
        void set_user_data(std::int64_t user_data) const;
        void set_user_data(double user_data) const;
        void set_user_data(const std::string & user_data) const;
        std::shared_ptr<void> get_pointer_user_data() const;
        std::int64_t get_integer_user_data() const;
        double get_float_user_data() const;
        const std::string & get_string_user_data() const;
        void remove_user_data() const;
        bool has_user_data() const noexcept;
        bool has_ptr_data() const noexcept;
        bool has_int_data() const noexcept;
        bool has_float_data() const noexcept;
        bool has_string_data() const noexcept;
    };

    class AsyncBlockerManager : public ImplBy<detail::AsyncBlockerManager>
    {
    public:
        struct Configure
        {
            duration_t block_timeout;
            int target_batch;
            std::uint32_t min_batch = 1;

            void validate() const;
        };

        AsyncBlockerManager(const Configure & configure);
        auto lock(const close_chan & closer = nullptr) const -> asio::awaitable<void>;
        void unlock() const;
        void collect_locked_blocker(std::vector<AsyncBlocker> & blockers) const;
        auto add_blocker(int priority, const close_chan & closer = nullptr) const -> asio::awaitable<AsyncBlocker>;
        void remove_blocker(std::uint32_t id) const;
        auto wait_blocker(std::uint32_t id, const close_chan & closer = nullptr) const -> asio::awaitable<void>;
    };
    
} // namespace cfgo


#endif