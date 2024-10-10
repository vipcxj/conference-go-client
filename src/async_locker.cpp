#include "cfgo/async_locker.hpp"
#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
// #include "cfgo/measure.hpp"
#include <list>
#include <memory>
#include <algorithm>
#include <atomic>
#include <limits>

namespace cfgo
{
    namespace detail
    {
        class AsyncBlockerManager;
        class AsyncBlocker
        {
        public:
            AsyncBlocker(std::uint32_t id): m_id(id), m_user_data(nullptr) {}
            AsyncBlocker(const AsyncBlocker &) = delete;
            AsyncBlocker & operator = (const AsyncBlocker &) = delete;
            bool need_block() const noexcept;
            bool is_blocked() const noexcept;
            std::uint32_t id() const noexcept;
            void set_user_data(std::shared_ptr<void> user_data);
            void set_user_data(std::int64_t user_data);
            void set_user_data(double user_data);
            void set_user_data(const std::string & user_data);
            std::shared_ptr<void> get_pointer_user_data() const;
            std::int64_t get_integer_user_data() const;
            double get_float_user_data() const;
            const std::string & get_string_user_data() const;
            void remove_user_data();
            bool has_user_data() const noexcept;
            bool has_ptr_data() const noexcept;
            bool has_int_data() const noexcept;
            bool has_float_data() const noexcept;
            bool has_string_data() const noexcept;
        private:
            std::uint32_t m_id;
            std::atomic_bool m_block = false;
            std::atomic_bool m_blocked = false;
            mutex m_mutex;
            state_notifier m_state_notifier {};
            std::variant<std::nullptr_t, std::shared_ptr<void>, std::int64_t, double, std::string> m_user_data;

            bool request_block();
            bool request_unblock();
            auto sync(close_chan closer) -> asio::awaitable<bool>;
            auto await_unblock(close_chan closer) -> asio::awaitable<bool>;

            friend class AsyncBlockerManager;
        };

        // only called by manager
        bool AsyncBlocker::request_block()
        {
            std::lock_guard g(m_mutex);
            if (m_block)
            {
                return false;
            }
            else
            {
                m_block = true;
                m_state_notifier.notify();
                return true;
            }
        }
        
        // only called by manager
        bool AsyncBlocker::request_unblock()
        {
            std::lock_guard g(m_mutex);
            if (m_block)
            {
                m_block = false;
                m_state_notifier.notify();
                return true;
            }
            else
            {
                return false;
            }
        }

        // only called by manager
        auto AsyncBlocker::sync(close_chan closer) -> asio::awaitable<bool>
        {
            do
            {
                auto ch = m_state_notifier.make_notfiy_receiver();
                {
                    std::lock_guard g(m_mutex);
                    if (m_block)
                    {
                        if (m_blocked)
                        {
                            break;
                        }
                    }
                    else
                    {
                        if (!m_blocked)
                        {
                            break;
                        }
                    }
                }
                if (!co_await chan_read<void>(ch, closer))
                {
                    co_return false;
                }
            } while (true);
            co_return true;
        }

        bool AsyncBlocker::need_block() const noexcept
        {
            return m_block;
        }

        bool AsyncBlocker::is_blocked() const noexcept
        {
            return m_blocked;
        }

        auto AsyncBlocker::await_unblock(close_chan closer) -> asio::awaitable<bool>
        {
            do
            {
                auto ch = m_state_notifier.make_notfiy_receiver();
                {
                    std::lock_guard g(m_mutex);
                    if (m_blocked)
                    {
                        if (!m_block)
                        {
                            m_blocked = false;
                            m_state_notifier.notify();
                            break;
                        }
                    }
                    else
                    {
                        if (m_block)
                        {
                            m_blocked = true;
                            m_state_notifier.notify();
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                if (!co_await chan_read<void>(ch, closer))
                {
                    co_return false;
                }
            } while (true);
            co_return true;
        }

        std::uint32_t AsyncBlocker::id() const noexcept
        {
            return m_id;
        }

        void AsyncBlocker::set_user_data(std::shared_ptr<void> user_data)
        {
            m_user_data = user_data;
        }

        void AsyncBlocker::set_user_data(std::int64_t user_data)
        {
            m_user_data = user_data;
        }

        void AsyncBlocker::set_user_data(double user_data)
        {
            m_user_data = user_data;
        }

        void AsyncBlocker::set_user_data(const std::string & user_data)
        {
            m_user_data = user_data;
        }

        std::shared_ptr<void> AsyncBlocker::get_pointer_user_data() const
        {
            return std::get<std::shared_ptr<void>>(m_user_data);
        }

        std::int64_t AsyncBlocker::get_integer_user_data() const
        {
            return std::get<std::int64_t>(m_user_data);
        }

        double AsyncBlocker::get_float_user_data() const
        {
            return std::get<double>(m_user_data);
        }

        const std::string & AsyncBlocker::get_string_user_data() const
        {
            return std::get<std::string>(m_user_data);
        }

        void AsyncBlocker::remove_user_data()
        {
            m_user_data = nullptr;
        }

        bool AsyncBlocker::has_user_data() const noexcept
        {
            return !std::holds_alternative<std::nullptr_t>(m_user_data);
        }

        bool AsyncBlocker::has_ptr_data() const noexcept
        {
            return std::holds_alternative<std::shared_ptr<void>>(m_user_data);
        }

        bool AsyncBlocker::has_int_data() const noexcept
        {
            return std::holds_alternative<std::int64_t>(m_user_data);
        }

        bool AsyncBlocker::has_float_data() const noexcept
        {
            return std::holds_alternative<double>(m_user_data);
        }

        bool AsyncBlocker::has_string_data() const noexcept
        {
            return std::holds_alternative<std::string>(m_user_data);
        }

        class AsyncBlockerManager : public std::enable_shared_from_this<AsyncBlockerManager>
        {
        public:
            using ScheduleConfigure = cfgo::AsyncBlockerManager::Configure;
            struct BlockerRequest
            {
                std::uint32_t m_id;
                int m_priority;
                unique_chan<AsyncBlockerPtr> m_chan;
            };
            
            struct BlockerInfo
            {
                AsyncBlockerPtr m_blocker;
                std::uint32_t m_epoch;
                int m_priority;
                bool m_valid;
            };

            AsyncBlockerManager(const ScheduleConfigure & configure);
            AsyncBlockerManager(const AsyncBlockerManager &) = delete;
            AsyncBlockerManager & operator = (const AsyncBlockerManager &) = delete;

            auto lock(close_chan closer) -> asio::awaitable<void>;
            void unlock();
            void collect_locked_blocker(std::vector<cfgo::AsyncBlocker> & blockers);
            auto add_blocker(int priority, close_chan closer) -> asio::awaitable<AsyncBlockerPtr>;
            auto wait_blocker(std::uint32_t id, close_chan closer) -> asio::awaitable<void>;
            void remove_blocker(std::uint32_t id);
        protected:
            auto _select_and_request_block(int n, close_chan closer) -> asio::awaitable<int>;
            std::uint32_t _calc_batch() const noexcept
            {
                std::uint32_t n;
                if (m_conf.target_batch > 0)
                {
                    if (m_conf.target_batch > m_blockers.size())
                    {
                        n = std::max(m_conf.min_batch, (std::uint32_t) m_blockers.size());
                    }
                    else
                    {
                        n = m_conf.target_batch;
                    }
                }
                else if (m_conf.target_batch < 0)
                {
                    n = (std::uint32_t) std::max(m_conf.target_batch + (std::int64_t) m_blockers.size(), (std::int64_t) m_conf.min_batch);
                }
                else
                {
                    n = m_conf.min_batch;
                }
                return n;
            }
        private:
            ScheduleConfigure m_conf;
            std::list<BlockerInfo> m_blockers;
            std::list<BlockerRequest> m_blocker_requests;
            std::uint32_t m_next_id = 0;
            std::uint32_t m_next_epoch = 0;
            std::atomic_bool m_locked = false;
            state_notifier m_ready_notifier;
            mutex m_mutex;
        };
        
        AsyncBlockerManager::AsyncBlockerManager(const ScheduleConfigure & configure): m_conf(configure) {}
        auto AsyncBlockerManager::add_blocker(int priority, close_chan closer) -> asio::awaitable<AsyncBlockerPtr>
        {
            auto self = shared_from_this();
            AsyncBlockerPtr block_ptr = nullptr;
            std::uint32_t id;
            unique_chan<AsyncBlockerPtr> chan{};
            {
                std::lock_guard lk(m_mutex);
                if (!m_locked)
                {
                    block_ptr = std::make_shared<AsyncBlocker>(m_next_id++);
                    m_blockers.push_back({block_ptr, m_next_epoch++, priority, true});
                    m_ready_notifier.notify();
                }
                else
                {
                    id = m_next_id++;
                    m_blocker_requests.push_back({id, priority, chan});
                }
            }
            if (block_ptr)
            {
                co_return block_ptr;
            }
            auto c_block_ptr = co_await chan_read<AsyncBlockerPtr>(chan, closer);
            if (c_block_ptr)
            {
                co_return c_block_ptr.value();
            }
            else // timeout
            {
                std::lock_guard lk(m_mutex);
                auto req_iter = std::find_if(m_blocker_requests.begin(), m_blocker_requests.end(), [id](const BlockerRequest & request) -> bool {
                    return request.m_id == id;
                });
                // not really add blocker yet, throw cancel error
                if (req_iter != m_blocker_requests.end())
                {
                    m_blocker_requests.erase(req_iter);
                    throw CancelError(closer);
                }
                auto iter = std::find_if(m_blockers.begin(), m_blockers.end(), [id](const BlockerInfo & info) -> bool {
                    return info.m_blocker->id() == id;
                });
                if (iter == m_blockers.end())
                {
                    throw cpptrace::logic_error(THIS_IS_IMPOSSIBLE);
                }
                co_return iter->m_blocker;
            }
        }

        void AsyncBlockerManager::remove_blocker(std::uint32_t id)
        {
            std::lock_guard lk(m_mutex);
            m_blocker_requests.erase(
                std::remove_if(m_blocker_requests.begin(), m_blocker_requests.end(), [id](const BlockerRequest & request) {
                    return request.m_id == id;
                }),
                m_blocker_requests.end()
            );
            if (!m_locked)
            {
                m_blockers.erase(
                    std::remove_if(m_blockers.begin(), m_blockers.end(), [id](const BlockerInfo & info) {
                        return info.m_blocker->id() == id;
                    }),
                    m_blockers.end()
                );
            }
            else
            {
                // when locking, the size of m_blockers should not changed.
                auto iter = std::find_if(m_blockers.begin(), m_blockers.end(), [id](const BlockerInfo & info) {
                    return info.m_blocker->id() == id;
                });
                if (iter != m_blockers.end())
                {
                    iter->m_valid = false;
                }
            }
        }



        auto AsyncBlockerManager::lock(close_chan closer) -> asio::awaitable<void>
        {
            if (m_locked)
            {
                throw cpptrace::runtime_error("Already locked.");
            }
            auto self = shared_from_this();
            // DurationMeasures measures(3);
            std::uint32_t batch;
            
            do
            {
                auto ch = m_ready_notifier.make_notfiy_receiver();
                {
                    std::lock_guard lk(m_mutex);
                    batch = _calc_batch();
                    if (batch <= m_blockers.size())
                    {
                        assert(batch > 0);
                        if (m_locked)
                        {
                            throw cpptrace::runtime_error("Already locked.");
                        }
                        m_locked = true;
                        break;
                    }
                }
                co_await chan_read_or_throw<void>(ch, closer);
            } while (true);
            
            // After here, m_locked == true
            try
            {
                AsyncTasksAll<void> tasks(closer);
                // After locked (m_locked == true), no blocker should be added to or removed from m_blockers, so we can copy refs of m_blockers outside of lock.
                std::vector<std::reference_wrapper<BlockerInfo>> selects(m_blockers.begin(), m_blockers.end());
                {
                    std::lock_guard lk(m_mutex);
                    std::sort(selects.begin(), selects.end(), [](const BlockerInfo & b1, const BlockerInfo & b2) -> bool {
                        if (b1.m_epoch == b2.m_epoch)
                        {
                            if (b1.m_priority == b2.m_priority)
                            {
                                return b1.m_blocker->id() < b2.m_blocker->id();
                            }
                            else
                            {
                                return b1.m_priority > b2.m_priority;
                            }
                        }
                        else
                        {
                            return b1.m_epoch < b2.m_epoch;
                        }
                    });
                    BlockerInfo & blocker_with_max_epoch = *std::max_element(selects.begin(), selects.end(), [](const BlockerInfo & b1, const BlockerInfo & b2) -> bool {
                        return b1.m_epoch < b2.m_epoch;
                    });
                    std::uint32_t max_epoch = blocker_with_max_epoch.m_epoch;

                    std::uint32_t n = 0;
                    for (std::uint32_t i = 0; i < selects.size(); ++i)
                    {
                        BlockerInfo & blocker = selects[i];
                        if (blocker.m_epoch == max_epoch)
                        {
                            ++n;
                        }
                        if (n > batch)
                        {
                            break;
                        }
                        tasks.add_task(fix_async_lambda([blocker = blocker.m_blocker, timeout = m_conf.block_timeout](close_chan closer) -> asio::awaitable<void> {
                            auto child_closer = closer.create_child();
                            close_guard cg(child_closer);
                            child_closer.set_timeout(timeout);
                            blocker->request_block();
                            if (!co_await blocker->sync(child_closer))
                            {
                                blocker->request_unblock();
                            }
                        }));
                    }
                }
                {
                    // ScopeDurationMeasurer sdm(measures.measure("block-selects"));
                    co_await tasks.await();
                }
                {
                    // ScopeDurationMeasurer sdm(measures.measure("sync-selects-1"));
                    for (BlockerInfo & select : selects)
                    {
                        if (!co_await select.m_blocker->sync(closer))
                        {
                            throw CancelError(closer);
                        }
                    }
                }
                
                // unblock blockers exceed the plan.
                int n_blocked = 0;
                for (BlockerInfo & select : selects)
                {
                    std::lock_guard lk(m_mutex);
                    if (select.m_blocker->is_blocked())
                    {
                        if (++n_blocked > batch)
                        {
                            select.m_blocker->request_unblock();
                        }
                    }
                }
                
                {
                    // ScopeDurationMeasurer sdm(measures.measure("sync-selects-2"));
                    for (BlockerInfo & select : selects)
                    {
                        if (!co_await select.m_blocker->sync(closer))
                        {
                            throw CancelError(closer);
                        }
                    }
                }
                // CFGO_INFO(
                //     "block-selects: {} ms ({:.02f}%), sync-selects-1: {} ms ({:.02f}%), sync-selects-2: {} ms ({:.02f}%)",
                //     cast_ms(*measures.latest("block-selects")), measures.latest_percent_n("block-selects", 0) * 100,
                //     cast_ms(*measures.latest("sync-selects-1")), measures.latest_percent_n("sync-selects-1", 0) * 100,
                //     cast_ms(*measures.latest("sync-selects-2")), measures.latest_percent_n("sync-selects-2", 0) * 100
                // );
            }
            catch(...)
            {
                unlock();
                throw;
            }
        }
        void AsyncBlockerManager::unlock()
        {
            {
                std::lock_guard lk(m_mutex);
                if (!m_locked)
                {
                    return;
                }
                m_locked = false;
                for (auto && blocker : m_blockers)
                {
                    if (blocker.m_blocker->request_unblock())
                    {
                        ++blocker.m_epoch;
                    }
                }
                for (auto it = m_blockers.begin(); it != m_blockers.end();)
                {
                    auto & blocker = *it;
                    if (!blocker.m_valid)
                    {
                        it = m_blockers.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                for (auto && request : m_blocker_requests)
                {
                    auto block_ptr = std::make_shared<AsyncBlocker>(request.m_id);
                    m_blockers.push_back({block_ptr, m_next_epoch++, request.m_priority, true});
                    chan_must_write(request.m_chan, block_ptr);
                    m_ready_notifier.notify();
                }
                m_blocker_requests.clear();
            }
        }

        void AsyncBlockerManager::collect_locked_blocker(std::vector<cfgo::AsyncBlocker> & blockers)
        {
            blockers.clear();
            for (auto && blocker : m_blockers)
            {
                if (blocker.m_blocker->is_blocked())
                {
                    blockers.push_back(cfgo::AsyncBlocker{blocker.m_blocker});
                }
            }
        }

        auto AsyncBlockerManager::wait_blocker(std::uint32_t id, close_chan closer) -> asio::awaitable<void>
        {
            AsyncBlockerPtr blocker = nullptr;
            {
                std::lock_guard lk(m_mutex);
                for (auto && blocker_info : m_blockers)
                {
                    if (blocker_info.m_blocker->id() == id)
                    {
                        blocker = blocker_info.m_blocker;
                        break;
                    }
                }
            }
            if (blocker)
            {
                if (!co_await blocker->await_unblock(closer))
                {
                    throw CancelError(closer);
                }
            }
            co_return;
        }

    } // namespace detail

    AsyncBlocker::AsyncBlocker(detail::AsyncBlockerPtr impl): ImplBy(impl) {}

    bool AsyncBlocker::need_block() const noexcept
    {
        return impl()->need_block();
    }

    bool AsyncBlocker::is_blocked() const noexcept
    {
        return impl()->is_blocked();
    }

    std::uint32_t AsyncBlocker::id() const noexcept
    {
        return impl()->id();
    }

    void AsyncBlocker::set_user_data(std::shared_ptr<void> user_data) const
    {
        impl()->set_user_data(user_data);
    }

    void AsyncBlocker::set_user_data(std::int64_t user_data) const
    {
        impl()->set_user_data(user_data);
    }
    
    void AsyncBlocker::set_user_data(double user_data) const
    {
        impl()->set_user_data(user_data);
    }
    
    void AsyncBlocker::set_user_data(const std::string & user_data) const
    {
        impl()->set_user_data(user_data);
    }
    
    std::shared_ptr<void> AsyncBlocker::get_pointer_user_data() const
    {
        return impl()->get_pointer_user_data();
    }
    
    std::int64_t AsyncBlocker::get_integer_user_data() const
    {
        return impl()->get_integer_user_data();
    }

    double AsyncBlocker::get_float_user_data() const
    {
        return impl()->get_float_user_data();
    }

    const std::string & AsyncBlocker::get_string_user_data() const
    {
        return impl()->get_string_user_data();
    }

    void AsyncBlocker::remove_user_data() const
    {
        impl()->remove_user_data();
    }

    bool AsyncBlocker::has_user_data() const noexcept
    {
        return impl()->has_user_data();
    }

    bool AsyncBlocker::has_ptr_data() const noexcept
    {
        return impl()->has_ptr_data();
    }

    bool AsyncBlocker::has_int_data() const noexcept
    {
        return impl()->has_int_data();
    }

    bool AsyncBlocker::has_float_data() const noexcept
    {
        return impl()->has_float_data();
    }

    bool AsyncBlocker::has_string_data() const noexcept
    {
        return impl()->has_string_data();
    }

    void AsyncBlockerManager::Configure::validate() const
    {
        if (min_batch < 1 || (std::uint64_t) min_batch > (std::uint64_t) std::numeric_limits<std::int32_t>::max())
        {
            throw cpptrace::runtime_error(fmt::format("Invalid min_batch. The min_batch must be greater or equal than 1 and less or equal than {}.", std::numeric_limits<std::int32_t>::max()));
        }
        if (target_batch > 0 && target_batch < min_batch)
        {
            throw cpptrace::runtime_error("Invalid target_batch. The target_batch must greater than min_batch when it is positive.");
        }
    }

    AsyncBlockerManager::AsyncBlockerManager(const Configure & configure): ImplBy(configure) {}

    auto AsyncBlockerManager::lock(close_chan closer) const -> asio::awaitable<void>
    {
        return impl()->lock(std::move(closer));
    }

    void AsyncBlockerManager::unlock() const
    {
        impl()->unlock();
    }

    void AsyncBlockerManager::collect_locked_blocker(std::vector<cfgo::AsyncBlocker> & blockers) const
    {
        impl()->collect_locked_blocker(blockers);
    }
    
    auto AsyncBlockerManager::add_blocker(int priority, close_chan closer) const -> asio::awaitable<AsyncBlocker>
    {
        auto block_ptr = co_await impl()->add_blocker(priority, std::move(closer));
        co_return AsyncBlocker{block_ptr};
    }

    void AsyncBlockerManager::remove_blocker(std::uint32_t id) const
    {
        impl()->remove_blocker(id);
    }

    auto AsyncBlockerManager::wait_blocker(std::uint32_t id, close_chan closer) const -> asio::awaitable<void>
    {
        return impl()->wait_blocker(id, std::move(closer));
    }
} // namespace cfgo

