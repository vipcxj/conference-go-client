#ifndef _CFGO_FURCATE_STREAM_HPP_
#define _CFGO_FURCATE_STREAM_HPP_

#include "cfgo/async.hpp"
#include "cfgo/smart_list.hpp"
#include "cfgo/defer.hpp"

namespace cfgo
{
    template<typename T, asiochan::channel_buff_size BuffSize>
    class FurcateStream;

    template<typename T, asiochan::channel_buff_size BuffSize>
    class FurcateBranch
    {
    public:
        using stream_t = FurcateStream<T, BuffSize>;
        using stream_ptr_t = std::shared_ptr<stream_t>;
        using stream_wptr_t = std::weak_ptr<stream_t>;
    private:
        stream_wptr_t m_stream;
        uint64_t m_index = 0;
        bool m_closed = false;
    public:
        FurcateBranch(stream_wptr_t stream): m_stream(std::move(stream)) {}
        FurcateBranch(const FurcateBranch &) = delete;
        FurcateBranch & operator= (const FurcateBranch &) = delete;
        
        std::optional<T> receive_sync(close_chan closer = nullptr)
        {
            bool cancel = false;
            close_chan cleaner {};
            close_guard cg {cleaner};
            if (auto stream = m_stream.lock())
            {
                closer.after_close(stream->m_executor, [weak_stream = m_stream, &cancel]() -> void {
                    if (auto stream = weak_stream.lock())
                    {
                        cancel = true;
                        stream->m_cv.notify_all();
                    }
                }, cleaner);
            }
            do
            {
                std::optional<T> current;
                uint64_t index;
                do
                {
                    if (auto stream = m_stream.lock())
                    {
                        auto lock = std::unique_lock {stream->m_mutex};
                        if (m_closed)
                        {
                            return std::nullopt;
                        }
                        if (!stream->m_current)
                        {
                            current = stream->m_ch.try_read();
                            if (current)
                            {
                                CFGO_INFO("read succeed");
                                stream->m_current = current;
                                index = stream->m_index;
                                assert(index > m_index);
                                stream->reset_timer();
                                break;
                            }
                            else
                            {
                                CFGO_INFO("waiting cv...");
                                stream->m_cv.wait(lock);
                                CFGO_INFO("cv wake up");
                            }
                        }
                        else if (stream->m_index > m_index)
                        {
                            current = stream->m_current;
                            index = stream->m_index;
                            break;
                        }
                        else
                        {
                            CFGO_INFO("waiting cv...");
                            stream->m_cv.wait(lock);
                            CFGO_INFO("cv wake up");
                        }
                    }
                    else
                    {
                        return std::nullopt;
                    }
                } while (true);
                if (auto stream = m_stream.lock())
                {
                    std::lock_guard g{stream->m_mutex};
                    if (m_closed)
                    {
                        return std::nullopt;
                    }
                    if (index == stream->m_index && index > m_index)
                    {
                        stream->cancel_timer();
                        m_index = index;
                        if (++stream->m_nsend == stream->m_nbranch)
                        {
                            CFGO_INFO("reader {}, next", stream->m_nsend);
                            auto data = stream->m_current;
                            stream->m_current.reset();
                            stream->m_nsend = 0;
                            stream->m_index ++;
                            return std::move(data);
                        }
                        else
                        {
                            return stream->m_current;
                        }
                    }
                }
                else
                {
                    return std::nullopt;
                }
            } while (true);
        }
        auto receive_async(close_chan closer = nullptr) -> asio::awaitable<std::optional<T>>
        {
            do
            {
                std::optional<T> current;
                uint64_t index;
                do
                {
                    if (auto stream = m_stream.lock())
                    {
                        auto receiver = stream->m_data_notifier.make_notfiy_receiver();
                        {
                            std::lock_guard g{stream->m_mutex};
                            if (m_closed)
                            {
                                co_return std::nullopt;
                            }
                            if (!stream->m_current)
                            {
                                current = stream->m_ch.try_read();
                                if (current)
                                {
                                    CFGO_INFO("read succeed");
                                    stream->m_current = current;
                                    index = stream->m_index;
                                    assert(index > m_index);
                                    stream->reset_timer();
                                    break;
                                }
                            }
                            else if (stream->m_index > m_index)
                            {
                                current = stream->m_current;
                                index = stream->m_index;
                                break;
                            }
                        }
                        CFGO_INFO("waiting receiver...");
                        if (!co_await chan_read<void>(*receiver, closer))
                        {
                            CFGO_INFO("receiver not waited.");
                            co_return std::nullopt;
                        }
                        CFGO_INFO("receiver waited.");
                    }
                    else
                    {
                        co_return std::nullopt;
                    }
                } while (true);
                if (auto stream = m_stream.lock())
                {
                    std::lock_guard g{stream->m_mutex};
                    if (m_closed)
                    {
                        co_return std::nullopt;
                    }
                    if (index == stream->m_index && index > m_index)
                    {
                        stream->cancel_timer();
                        m_index = index;
                        if (++stream->m_nsend == stream->m_nbranch)
                        {
                            CFGO_INFO("reader {}, next", stream->m_nsend);
                            auto data = stream->m_current;
                            stream->m_current.reset();
                            stream->m_nsend = 0;
                            stream->m_index ++;
                            co_return std::move(data);
                        }
                        else
                        {
                            co_return stream->m_current;
                        }
                    }
                }
                else
                {
                    co_return std::nullopt;
                }
            } while (true);
        }

        friend class FurcateStream<T, BuffSize>;
    };

    template<typename T, asiochan::channel_buff_size BuffSize>
    class FurcateStream : public std::enable_shared_from_this<FurcateStream<T, BuffSize>>
    {
    public:
        using branch_t = FurcateBranch<T, BuffSize>;
    private:
        smart_list<branch_t> m_branches;
        asiochan::channel<T, BuffSize> m_ch;
        std::optional<T> m_current;
        uint64_t m_index = 1;
        int m_nsend = 0;
        int m_nbranch = 0;
        state_notifier m_data_notifier;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        asio::any_io_executor m_executor;
        std::chrono::high_resolution_clock::duration m_timeout;
        std::shared_ptr<asio::steady_timer> m_timer;

        void reset_timer()
        {
            if (m_timer)
            {
                m_timer->cancel();
            }
            m_timer = std::make_shared<asio::steady_timer>(m_executor);
            m_timer->expires_after(m_timeout);
            m_timer->async_wait([index = m_index, weak_self = this->weak_from_this()](const std::error_code & ec) {
                if (!ec)
                {
                    CFGO_INFO("timer expired");
                    if (auto self = weak_self.lock())
                    {
                        std::lock_guard g(self->m_mutex);
                        if (self->m_index == index)
                        {
                            CFGO_INFO("read new");
                            self->m_current.reset();
                            self->m_nsend = 0;
                            self->m_index ++;
                        }
                    }
                }
            });
        }

        void cancel_timer()
        {
            if (m_timer)
            {
                m_timer->cancel();
                m_timer.reset();
            }
        }

        FurcateStream(asio::any_io_executor executor, duration_t timeout)
        : m_executor(std::move(executor)), m_timeout(timeout)
        {
        }

    public:
        ~FurcateStream()
        {
            CFGO_INFO("destruct");
            std::lock_guard g(m_mutex);
            m_branches.for_each([](branch_t & branch) {
                branch.m_closed = true;
                return true;
            });
            m_data_notifier.notify();
            m_cv.notify_all();
        }

        static std::shared_ptr<FurcateStream<T, BuffSize>> create(asio::any_io_executor executor, duration_t timeout)
        {
            return std::shared_ptr<FurcateStream<T, BuffSize>> {new FurcateStream(std::move(executor), timeout)};
        }

        bool send_sync(T data, close_chan closer = nullptr)
        {
            asiochan::interrupter_t interrupter {};
            close_chan cleaner {};
            close_guard cg(cleaner);
            closer.after_close(m_executor, [&interrupter]() -> void {
                interrupter.interrupt();
            }, cleaner);
            auto res = m_ch.write_sync(interrupter, std::move(data));
            if (res)
            {
                m_data_notifier.notify();
                m_cv.notify_all();
            }
            return res;
        }

        auto send_async(T data, close_chan closer = nullptr) -> asio::awaitable<bool>
        {
            if (co_await chan_write<T>(m_ch, std::move(data), std::move(closer)))
            {
                m_data_notifier.notify();
                m_cv.notify_all();
                co_return true;
            }
            else
            {
                co_return false;
            }
        }
        smart_node<branch_t>::ptr create_branch()
        {
            std::lock_guard g(m_mutex);
            ++m_nbranch;
            return m_branches.emplace(this->weak_from_this());
        }

        void remove_branch(smart_node<branch_t>::ptr & branch)
        {
            std::lock_guard g(m_mutex);
            --m_nbranch;
            if (branch->m_index == m_index)
            {
                --m_nsend;
            }
            branch->m_closed = true;
            m_data_notifier.notify();
            m_cv.notify_all();
            m_branches.remove(branch);
        }

        friend class FurcateBranch<T, BuffSize>;
    };
    
} // namespace cfgo


#endif
