#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
#include "asio.hpp"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <random>

void do_async(std::function<asio::awaitable<void>()> func, bool wait = false) {
    auto tp = std::make_shared<asio::thread_pool>();
    if (wait)
    {
        auto res = asio::co_spawn(*tp, func, asio::use_future);
        res.get();
    }
    else
    {
        asio::co_spawn(*tp, cfgo::fix_async_lambda([tp, func]() -> asio::awaitable<void> {
            co_await func();
            co_return;
        }), asio::detached);
    }
}

TEST(Chan, CloseChan) {
    using namespace cfgo;
    close_chan close_ch;
    EXPECT_TRUE(is_valid_close_chan(close_ch));
    EXPECT_FALSE(is_valid_close_chan(INVALID_CLOSE_CHAN));
}

TEST(Chan, WaitTimeout) {
    using namespace cfgo;
    bool done = false;
    do_async(cfgo::fix_async_lambda([&done]() -> asio::awaitable<void> {
        co_await wait_timeout(std::chrono::milliseconds{500});
        done = true;
        co_return;
    }), false);
    std::this_thread::sleep_for(std::chrono::milliseconds{1600});
    EXPECT_TRUE(done);
}

TEST(Chan, MakeTimeout) {
    using namespace cfgo;
    bool done = false;
    do_async(cfgo::fix_async_lambda([&done]() -> asio::awaitable<void> {
        auto timeout = cfgo::make_timeout(std::chrono::milliseconds{500});
        co_await timeout.await();
        done = true;
        EXPECT_TRUE(timeout.is_closed());
        EXPECT_TRUE(timeout.is_timeout());
        co_return;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds{600});
    EXPECT_TRUE(done);
}

TEST(Chan, ChanRead) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        asiochan::channel<int> ch{};
        do_async(cfgo::fix_async_lambda(([ch]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{100});
            co_await ch.write(1);
            co_return;
        })));
        int res = 0;
        do_async(cfgo::fix_async_lambda([ch, &res]() mutable -> asio::awaitable<void> {
            auto res1 = co_await chan_read<int>(ch);
            EXPECT_FALSE(res1.is_canceled());
            EXPECT_EQ(res1.value(), 1);
            res = 1;
        }));
        co_await wait_timeout(std::chrono::milliseconds{200});
        EXPECT_EQ(res, 1);

        bool canceled = false;
        do_async(cfgo::fix_async_lambda([ch]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{200});
            co_await ch.write(1);
            co_return;
        }));
        do_async([ch, &canceled]() mutable -> asio::awaitable<void> {
            auto timeout = cfgo::make_timeout(std::chrono::milliseconds{100});
            auto res2 = co_await chan_read<int>(ch, timeout);
            EXPECT_TRUE(res2.is_canceled());
            canceled = true;
        });
        co_await wait_timeout(std::chrono::milliseconds{300});
        EXPECT_TRUE(canceled);

    }, true);
}

TEST(Chan, ChanReadOrThrow) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        asiochan::channel<int> ch{};
        do_async([ch]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{100});
            co_await ch.write(1);
            co_return;
        });
        int res = 0;
        do_async([ch, &res]() mutable -> asio::awaitable<void> {
            res = co_await chan_read_or_throw<int>(ch);
        });
        co_await wait_timeout(std::chrono::milliseconds{200});
        EXPECT_EQ(res, 1);

        bool canceled = false;
        do_async([ch]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{200});
            co_await ch.write(1);
            co_return;
        });
        res = 0;
        do_async([ch, &res, &canceled]() mutable -> asio::awaitable<void> {
            auto timeout = cfgo::make_timeout(std::chrono::milliseconds{100});
            try
            {
                res = co_await chan_read_or_throw<int>(ch, timeout);
            }
            catch(const cfgo::CancelError& e)
            {
                canceled = true;
            }
        });
        co_await wait_timeout(std::chrono::milliseconds{300});
        EXPECT_TRUE(canceled);
        EXPECT_EQ(res, 0);
    }, true);
}

TEST(Chan, AsyncTasksAllT) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(-100, 100);
        std::size_t n_tasks = 5;

        {
            AsyncTasksAll<int> tasks{};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{200 + amp});
                    co_return (int) i;
                });
            }
            auto res_int_vec = co_await tasks.await();
            EXPECT_EQ(res_int_vec.size(), n_tasks);
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                auto iter = std::find(res_int_vec.begin(), res_int_vec.end(), (int) i);
                EXPECT_TRUE(iter != res_int_vec.end());
            }
        }

        {
            close_chan closer{};
            AsyncTasksAll<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp}, closer);
                    co_return (int) i;
                });
            }
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, [closer]() mutable -> asio::awaitable<void> {
                co_await wait_timeout(std::chrono::milliseconds{100});
                closer.close();
            }, asio::detached);
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_FALSE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            closer.set_timeout(std::chrono::milliseconds{100});
            AsyncTasksAll<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    co_return (int) i;
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_TRUE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            AsyncTasksAll<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), n_tasks](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    if (i == (std::size_t) (n_tasks / 2))
                    {
                        throw std::runtime_error("an error");
                    }
                    else
                    {
                        co_return (int) i;
                    }
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const std::runtime_error& e)
            {
                canceled = true;
                EXPECT_STREQ(e.what(), "an error");
            }
            EXPECT_TRUE(canceled);
        }
    }, true);
}

TEST(Chan, AsyncTasksAllVoid) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(-100, 100);
        std::size_t n_tasks = 5;
        {
            AsyncTasksAll<void> tasks{};
            mutex mutex;
            int sum = 0;
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), &sum, &mutex](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{200 + amp});
                    {
                        std::lock_guard lock(mutex);
                        sum += i;
                    }
                    co_return;
                });
            }
            co_await tasks.await();
            int sum_expect = 0;
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                sum_expect += i;
            }
            EXPECT_EQ(sum_expect, sum);
        }

        {
            close_chan closer{};
            AsyncTasksAll<void> tasks{closer};
            mutex mutex;
            int sum = 0;
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), &sum, &mutex](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    {
                        std::lock_guard lock(mutex);
                        sum += i;
                    }
                    co_return;
                });
            }
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, [closer]() mutable -> asio::awaitable<void> {
                co_await wait_timeout(std::chrono::milliseconds{100});
                closer.close();
            }, asio::detached);
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_FALSE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            closer.set_timeout(std::chrono::milliseconds{100});
            AsyncTasksAll<void> tasks{closer};
            mutex mutex;
            int sum = 0;
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), &sum, &mutex](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    {
                        std::lock_guard lock(mutex);
                        sum += i;
                    }
                    co_return;
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_TRUE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            AsyncTasksAll<void> tasks{closer};
            mutex mutex;
            int sum = 0;
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), &sum, &mutex, n_tasks](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    if (i == (std::size_t) (n_tasks / 2))
                    {
                        throw std::runtime_error("an error");
                    }
                    else
                    {
                        std::lock_guard lock(mutex);
                        sum += i;
                    }
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const std::runtime_error& e)
            {
                canceled = true;
                EXPECT_STREQ(e.what(), "an error");
            }
            EXPECT_TRUE(canceled);
        }
    }, true);
}

TEST(Chan, AsyncTasksAnyT) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(-100, 100);
        std::size_t n_tasks = 5;

        {
            AsyncTasksAny<int> tasks{};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{200 + amp});
                    co_return (int) i;
                });
            }
            auto res = co_await tasks.await();
            EXPECT_TRUE(res >= 0 && res < 5);
        }

        {
            close_chan closer{};
            AsyncTasksAny<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    co_return (int) i;
                });
            }
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, [closer]() mutable -> asio::awaitable<void> {
                co_await wait_timeout(std::chrono::milliseconds{100});
                closer.close();
            }, asio::detached);
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_FALSE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            closer.set_timeout(std::chrono::milliseconds{100});
            AsyncTasksAny<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen)](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    co_return (int) i;
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_TRUE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            AsyncTasksAny<int> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), n_tasks](auto closer) -> asio::awaitable<int> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    if (i == (std::size_t) (n_tasks / 2))
                    {
                        throw std::runtime_error("an error");
                    }
                    else
                    {
                        co_return (int) i;
                    }
                });
            }
            auto res = co_await tasks.await();
            EXPECT_TRUE(res >= 0 && res < 5);
        }
    }, true);
}

TEST(Chan, AsyncTasksAnyVoid) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(-100, 100);
        std::size_t n_tasks = 5;

        {
            AsyncTasksAny<void> tasks{};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([amp = distrib(gen)](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{200 + amp});
                });
            }
            co_await tasks.await();
        }

        {
            close_chan closer{};
            AsyncTasksAny<void> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([amp = distrib(gen)](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                });
            }
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, [closer]() mutable -> asio::awaitable<void> {
                co_await wait_timeout(std::chrono::milliseconds{100});
                closer.close();
            }, asio::detached);
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_FALSE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            closer.set_timeout(std::chrono::milliseconds{100});
            AsyncTasksAny<void> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([amp = distrib(gen)](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                });
            }
            bool canceled = false;
            try
            {
                co_await tasks.await();
            }
            catch(const CancelError& e)
            {
                canceled = true;
                EXPECT_TRUE(e.is_timeout());
            }
            EXPECT_TRUE(canceled);
        }

        {
            close_chan closer{};
            AsyncTasksAny<void> tasks{closer};
            for (std::size_t i = 0; i < n_tasks; i++)
            {
                tasks.add_task([i, amp = distrib(gen), n_tasks](auto closer) -> asio::awaitable<void> {
                    co_await wait_timeout(std::chrono::milliseconds{300 + amp});
                    if (i == (std::size_t) (n_tasks / 2))
                    {
                        throw std::runtime_error("an error");
                    }
                });
            }
            co_await tasks.await();
        }
    }, true);
}

TEST(Helper, SharedPtrHolder) {
    auto ptr = std::make_shared<int>();
    EXPECT_EQ(ptr.use_count(), 1);
    auto holder = cfgo::make_shared_holder(ptr);
    EXPECT_EQ(ptr.use_count(), 2);
    cfgo::destroy_shared_holder(holder);
    EXPECT_EQ(ptr.use_count(), 1);
}

TEST(Closer, ParentAndChildrenCloseTogether) {
    using namespace cfgo;
    std::random_device rd {};
    std::mt19937 gen(rd());
    std::vector<close_chan> closers {};
    for (size_t i = 0; i < 100; i++)
    {
        close_chan parent {};
        closers.push_back(parent);
        for (size_t i = 0; i < 10; i++)
        {
            auto child = parent.create_child();
            closers.push_back(child);
        }
    }
    std::shuffle(closers.begin(), closers.end(), gen);
    for (auto && closer : closers)
    {
        std::thread([closer]() {
            std::this_thread::sleep_for(std::chrono::milliseconds {100});
            closer.close();
        }).detach();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds {1000});
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::DEFAULT, spdlog::level::trace);
    return RUN_ALL_TESTS();
}