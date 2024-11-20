#ifndef _CFGO_TASK_POOL_HPP_
#define _CFGO_TASK_POOL_HPP_

#include <thread>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <functional>


namespace cfgo
{
    class TaskQueue
    {
    public:
        TaskQueue() = default;
        virtual ~TaskQueue() noexcept = default;

        virtual bool enqueue(std::function<void()> fn) = 0;
        virtual void shutdown(bool wait = true) = 0;

        virtual void on_idle() {}
    };

    class ThreadPool : public TaskQueue
    {
    public:
        explicit ThreadPool(size_t n, size_t mqr = 0)
            : shutdown_(false), max_queued_requests_(mqr)
        {
            while (n)
            {
                threads_.emplace_back(worker(*this));
                n--;
            }
        }

        ThreadPool(const ThreadPool &) = delete;
        ~ThreadPool() noexcept override
        {
            shutdown(false);
        }

        bool enqueue(std::function<void()> fn) override
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (max_queued_requests_ > 0 && jobs_.size() >= max_queued_requests_)
                {
                    return false;
                }
                jobs_.push_back(std::move(fn));
            }

            cond_.notify_one();
            return true;
        }

        void shutdown(bool wait) override
        {
            // Stop all worker threads...
            {
                std::unique_lock<std::mutex> lock(mutex_);
                shutdown_ = true;
            }

            cond_.notify_all();

            if (wait)
            {
                // Join...
                for (auto &t : threads_)
                {
                    t.join();
                }
            }
            else
            {
                for (auto &t : threads_)
                {
                    t.detach();
                }
            }
        }

    private:
        struct worker
        {
            explicit worker(ThreadPool &pool) : pool_(pool) {}

            void operator()()
            {
                for (;;)
                {
                    std::function<void()> fn;
                    {
                        std::unique_lock<std::mutex> lock(pool_.mutex_);

                        pool_.cond_.wait(
                            lock, [&]
                            { return !pool_.jobs_.empty() || pool_.shutdown_; });

                        if (pool_.shutdown_ && pool_.jobs_.empty())
                        {
                            break;
                        }

                        fn = std::move(pool_.jobs_.front());
                        pool_.jobs_.pop_front();
                    }

                    assert(true == static_cast<bool>(fn));
                    fn();
                }
            }

            ThreadPool &pool_;
        };
        friend struct worker;

        std::vector<std::thread> threads_;
        std::list<std::function<void()>> jobs_;

        bool shutdown_;
        size_t max_queued_requests_ = 0;

        std::condition_variable cond_;
        std::mutex mutex_;
    };
} // namespace cfgo

#endif