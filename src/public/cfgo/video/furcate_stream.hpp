#ifndef _CFGO_VIDEO_FURCATE_STREAM_HPP_
#define _CFGO_VIDEO_FURCATE_STREAM_HPP_

#include "cfgo/async.hpp"
#include "cfgo/smart_list.hpp"

namespace cfgo
{
    namespace video
    {
        template<typename T, asiochan::channel_buff_size BuffSize>
        class FurcateStream;

        template<typename T, asiochan::channel_buff_size BuffSize>
        class FurcateBranch
        {
        private:
            FurcateStream<T, BuffSize> * m_stream;
            uint64_t m_index = 0;
        public:
            FurcateBranch(FurcateStream<T, BuffSize> * stream): m_stream(stream) {}
            FurcateBranch(const FurcateBranch &) = delete;
            FurcateBranch & operator= (const FurcateBranch &) = delete;
            
            std::optional<T> receive_sync()
            {
                do
                {
                    std::optional<T> current;
                    uint64_t index;
                    do
                    {
                        auto lock = std::unique_lock {m_stream->m_mutex};
                        if (!m_stream->m_current)
                        {
                            current = m_stream->m_ch.try_read();
                            if (current)
                            {
                                m_stream->m_current = current;
                                index = m_stream->m_index;
                                break;
                            }
                            else
                            {
                                m_stream->m_cv.wait(lock);
                            }
                        }
                        else
                        {
                            current = m_stream->m_current;
                            index = m_stream->m_index;
                            break;
                        }
                    } while (true);
                    std::lock_guard g{m_stream->m_mutex};
                    if (index == m_stream->m_index)
                    {
                        if (++m_stream->m_nsend == m_stream->m_branches)
                        {
                            auto data = std::move(*m_stream->m_current);
                            m_stream->m_current.reset();
                            m_stream->m_nsend = 0;
                            m_stream->m_index ++;
                            return std::move(data);
                        }
                        else
                        {
                            return m_stream->m_current;
                        }
                    }
                } while (true);
            }
            auto receive_async(close_chan closer) -> asio::awaitable<T>
            {
                do
                {
                    std::optional<T> current;
                    uint64_t index;
                    do
                    {
                        auto receiver = m_stream->m_data_notifier.make_notfiy_receiver();
                        {
                            std::lock_guard g{m_stream->m_mutex};
                            if (!m_stream->m_current)
                            {
                                current = m_stream->m_ch.try_read();
                                if (current)
                                {
                                    m_stream->m_current = current;
                                    index = m_stream->m_index;
                                    break;
                                }
                            }
                            else
                            {
                                current = m_stream->m_current;
                                index = m_stream->m_index;
                                break;
                            }
                        }
                        if (!co_await chan_read<void>(*receiver, closer))
                        {
                            co_return std::nullopt;
                        }
                    } while (true);
                    std::lock_guard g{m_stream->m_mutex};
                    if (index == m_stream->m_index)
                    {
                        if (++m_stream->m_nsend == m_stream->m_branches)
                        {
                            auto data = std::move(*m_stream->m_current);
                            m_stream->m_current.reset();
                            m_stream->m_nsend = 0;
                            m_stream->m_index ++;
                            return std::move(data);
                        }
                        else
                        {
                            return m_stream->m_current;
                        }
                    }
                } while (true);
            }

            friend class FurcateStream<T, BuffSize>;
        };

        template<typename T, asiochan::channel_buff_size BuffSize>
        class FurcateStream
        {
        private:
            smart_list<FurcateBranch> m_branches;
            asiochan::channel<T, BuffSize> m_ch;
            std::optional<T> m_current;
            uint64_t m_index = 1;
            int m_nsend = 0;
            int m_nbranch = 0;
            state_notifier m_data_notifier;
            std::mutex m_mutex;
            std::condition_variable m_cv;

        public:
            FurcateStream(/* args */);
            ~FurcateStream();

            bool send_sync(T data, asiochan::interrupter_t & interrupter)
            {
                auto res = m_ch.write_sync(interrupter, std::move(data));
                if (res)
                {
                    m_data_notifier.notify();
                    m_cv.notify_all();
                }
                return res;
            }

            auto send_async(T data, close_chan closer) -> asio::awaitable<bool>
            {
                if (co_await chan_write<T>(m_ch, std::move(data), std::move(closer)))
                {
                    m_data_notifier.notify();
                    m_cv.notify_all();
                    return true;
                }
                else
                {
                    return false;
                }
            }
            smart_node<FurcateBranch<T, BuffSize>>::ptr create_branch()
            {
                std::lock_guard g(m_mutex);
                ++m_nbranch;
                return m_branches.add(FurcateBranch<T, BuffSize> {this});
            }

            void remove_branch(smart_node<FurcateBranch<T, BuffSize>>::ptr & branch)
            {
                std::lock_guard g(m_mutex);
                --m_nbranch;
                if (branch->m_index == m_index)
                {
                    --m_nsend;
                }
                m_branches.remove(branch);
            }

            friend class FurcateBranch<T, BuffSize>;
        };
        
    } // namespace video
    
} // namespace cfgo


#endif
