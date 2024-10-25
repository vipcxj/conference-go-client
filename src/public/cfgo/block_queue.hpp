#ifndef _CFGO_BLOCK_QUEUE_HPP_
#define _CFGO_BLOCK_QUEUE_HPP_

#include <queue>
#include <mutex>
#include <condition_variable>

namespace cfgo
{
    template <typename T>
    class BlockingQueue
    {
    private:
        std::mutex mut;
        std::queue<T> private_std_queue;
        std::condition_variable condNotEmpty;
        std::condition_variable condNotFull;
        int count = 0; // Guard with Mutex
        const int m_capacity;

    public:
        BlockingQueue(int capacity = 0): m_capacity(capacity) {}
        BlockingQueue(const BlockingQueue &) = default;
        BlockingQueue(BlockingQueue &&) = default;
        BlockingQueue & operator= (const BlockingQueue &) = default;
        BlockingQueue & operator= (BlockingQueue &&) = default;
        void put(T new_value)
        {

            std::unique_lock<std::mutex> lk(mut);
            if (m_capacity > 0)
            {
                // Condition takes a unique_lock and waits given the false condition
                condNotFull.wait(lk, [this]
                {
                    if (count == m_capacity)
                    {
                        return false;
                    }
                    else
                    {
                        return true;
                    }
                });
            }
            private_std_queue.push(new_value);
            count++;
            condNotEmpty.notify_one();
        }
        void take(T &value)
        {
            std::unique_lock<std::mutex> lk(mut);
            // Condition takes a unique_lock and waits given the false condition
            condNotEmpty.wait(lk, [this] { return !private_std_queue.empty(); });
            value = private_std_queue.front();
            private_std_queue.pop();
            count--;
            if (m_capacity > 0)
            {
                condNotFull.notify_one();
            }
        }
    };
} // namespace cfgo

#endif