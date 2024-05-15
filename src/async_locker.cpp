#include "cfgo/async_locker.hpp"
#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
#include <list>
#include <memory>
#include <algorithm>
#include <limits>

namespace cfgo
{
    namespace detail
    {
        class AsyncBlocker
        {
        public:
            AsyncBlocker(std::uint32_t id): m_id(id), m_user_data(nullptr) {}
            AsyncBlocker(const AsyncBlocker &) = delete;
            AsyncBlocker & operator = (const AsyncBlocker &) = delete;
            auto request_block(close_chan closer) -> asio::awaitable<bool>;
            bool need_block() const noexcept;
            bool is_blocked() const noexcept;
            auto await_unblock() -> asio::awaitable<void>;
            bool unblock();
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
            bool m_block = false;
            bool m_blocked = false;
            unique_void_chan m_request_chan;
            unique_void_chan m_response_chan;
            std::variant<std::nullptr_t, std::shared_ptr<void>, std::int64_t, double, std::string> m_user_data;
        };

        // only called by manager
        auto AsyncBlocker::request_block(close_chan closer) -> asio::awaitable<bool>
        {
            if (m_block)
            {
                throw cpptrace::logic_error("request_block could not be called when already requested block.");
            }
            
            m_block = true;
            do
            {
                if (!co_await chan_read<void>(m_response_chan, closer))
                {
                    unblock();
                    co_return false;
                }
                if (m_blocked)
                {
                    co_return true;
                }
            } while (true);
        }

        bool AsyncBlocker::need_block() const noexcept
        {
            return m_block;
        }

        bool AsyncBlocker::is_blocked() const noexcept
        {
            return m_blocked;
        }
        
        // only called by manager
        bool AsyncBlocker::unblock()
        {
            if (m_block)
            {
                m_block = false;
                std::ignore = m_request_chan.try_write();
                return true;
            }
            else
            {
                return false;
            }
        }

        auto AsyncBlocker::await_unblock() -> asio::awaitable<void>
        {
            if (m_blocked)
            {
                throw cpptrace::logic_error("await_unblock should not be called when already blocked.");
            }
            
            if (!m_block)
            {
                co_return;
            }
            m_blocked = true;
            do
            {
                std::ignore = m_response_chan.try_write();
                co_await m_request_chan.read();
                if (!m_block)
                {
                    m_blocked = false;
                    co_return;
                }
                
            } while (true);
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
            bool m_locked = false;
            unique_void_chan m_ready_ch;
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
                bool not_enouth = _calc_batch() > m_blockers.size();
                if (!m_locked)
                {
                    block_ptr = std::make_shared<AsyncBlocker>(m_next_id++);
                    m_blockers.push_back({block_ptr, m_next_epoch++, priority, true});
                    if (not_enouth && _calc_batch() <= m_blockers.size())
                    {
                        chan_must_write(m_ready_ch);
                    }
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
            else
            {
                std::lock_guard lk(m_mutex);
                auto req_iter = std::find_if(m_blocker_requests.begin(), m_blocker_requests.end(), [id](const BlockerRequest & request) -> bool {
                    return request.m_id == id;
                });
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
            std::uint32_t batch;
            do
            {
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
                co_await chan_read_or_throw<void>(m_ready_ch, closer);
            } while (true);
            try
            {
                AsyncTasksSome<void> tasks(batch, closer);
                {
                    std::lock_guard lk(m_mutex);
                    std::vector<std::reference_wrapper<BlockerInfo>> selects(m_blockers.begin(), m_blockers.end());
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
                            child_closer.set_timeout(timeout);
                            if (blocker->has_int_data())
                            {
                                cfgo::Log::instance().default_logger()->info("[blocker {}] requesting block.", blocker->get_integer_user_data());
                            }
                            if (!co_await blocker->request_block(child_closer) && !child_closer.is_timeout())
                            {
                                if (blocker->has_int_data())
                                {
                                    cfgo::Log::instance().default_logger()->info("[blocker {}] canceled.", blocker->get_integer_user_data());
                                }
                                throw CancelError(child_closer);
                            }
                            if (blocker->has_int_data())
                            {
                                cfgo::Log::instance().default_logger()->info("[blocker {}] block requested.", blocker->get_integer_user_data());
                            }
                        }));
                    }
                }
                co_await tasks.await();
            }
            catch(...)
            {
                unlock();
                std::rethrow_exception(std::current_exception());
            }
        }
        void AsyncBlockerManager::unlock()
        {
            {
                std::lock_guard lk(m_mutex);
                if (!m_locked)
                {
                    throw cpptrace::runtime_error("Not locked yet.");
                }
                m_locked = false;
                for (auto && blocker : m_blockers)
                {
                    if (blocker.m_blocker->unblock())
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

    } // namespace detail

    AsyncBlocker::AsyncBlocker(detail::AsyncBlockerPtr impl): ImplBy(impl) {}

    auto AsyncBlocker::request_block(close_chan closer) -> asio::awaitable<bool>
    {
        return impl()->request_block(std::move(closer));
    }

    bool AsyncBlocker::need_block() const noexcept
    {
        return impl()->need_block();
    }

    bool AsyncBlocker::is_blocked() const noexcept
    {
        return impl()->is_blocked();
    }

    auto AsyncBlocker::await_unblock() -> asio::awaitable<void>
    {
        return impl()->await_unblock();
    }

    void AsyncBlocker::unblock()
    {
        impl()->unblock();
    }

    std::uint32_t AsyncBlocker::id() const noexcept
    {
        return impl()->id();
    }

    void AsyncBlocker::set_user_data(std::shared_ptr<void> user_data)
    {
        impl()->set_user_data(user_data);
    }

    void AsyncBlocker::set_user_data(std::int64_t user_data)
    {
        impl()->set_user_data(user_data);
    }
    
    void AsyncBlocker::set_user_data(double user_data)
    {
        impl()->set_user_data(user_data);
    }
    
    void AsyncBlocker::set_user_data(const std::string & user_data)
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

    void AsyncBlocker::remove_user_data()
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
        auto block_ptr = co_await impl()->add_blocker(priority, closer);
        co_return AsyncBlocker{block_ptr};
    }

    void AsyncBlockerManager::remove_blocker(std::uint32_t id) const
    {
        impl()->remove_blocker(id);
    }
} // namespace cfgo

