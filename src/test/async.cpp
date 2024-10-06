#include "cfgo/async.hpp"
#include "cfgo/async_locker.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/log.hpp"
#include "cfgo/measure.hpp"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <random>
#include <sstream>

std::shared_ptr<asio::thread_pool> TP = nullptr;

void do_async(std::function<asio::awaitable<void>()> func, bool wait = false, std::shared_ptr<asio::thread_pool> tp_ptr = nullptr) {
    auto tp = tp_ptr ? tp_ptr : TP;
    auto executor = tp->get_executor();
    if (wait)
    {
        auto res = asio::co_spawn(std::move(executor), std::move(func), asio::use_future);
        res.get();
    }
    else
    {
        asio::co_spawn(std::move(executor), [func = std::move(func)]() -> asio::awaitable<void> {
            co_await func();
            co_return;
        }, asio::detached);
    }
}

template<typename T>
struct Box {
    T value;

    Box(bool v): value(v) {}
};

TEST(Chan, CloseChan) {
    using namespace cfgo;
    close_chan close_ch;
    EXPECT_TRUE(is_valid_close_chan(close_ch));
    EXPECT_FALSE(is_valid_close_chan(INVALID_CLOSE_CHAN));
}

TEST(Chan, WaitTimeout) {
    using namespace cfgo;
    auto done = std::make_shared<Box<bool>>(false);
    auto task = cfgo::fix_async_lambda([done]() -> asio::awaitable<void> {
        DurationMeasure m(3);
        {
            ScopeDurationMeasurer sm(m);
            co_await wait_timeout(std::chrono::milliseconds{500});
        }
        done->value = true;
        using namespace std::chrono;
        CFGO_INFO("expect sleep {} ms, really sleep {} ms", 500, duration_cast<milliseconds>(*m.latest()).count());
        co_return;
    });
    do_async(std::move(task), false);
    std::this_thread::sleep_for(std::chrono::milliseconds{1600});
    EXPECT_TRUE(done->value);
}

TEST(Chan, MakeTimeout) {
    using namespace cfgo;
    auto done = std::make_shared<Box<bool>>(false);
    auto task = cfgo::fix_async_lambda([done]() -> asio::awaitable<void> {
        auto timeout = cfgo::make_timeout(std::chrono::milliseconds{500});
        co_await timeout.await();
        done->value = true;
        EXPECT_TRUE(timeout.is_closed());
        EXPECT_TRUE(timeout.is_timeout());
        co_return;
    });
    do_async(std::move(task));
    std::this_thread::sleep_for(std::chrono::milliseconds{600});
    EXPECT_TRUE(done->value);
}

TEST(Chan, ChanRead) {
    using namespace cfgo;
    auto task = []() -> asio::awaitable<void> {
        asiochan::channel<int> ch{};
        close_chan closer {};
        DEFER({
            closer.close();
        });
        do_async(cfgo::fix_async_lambda(([ch, closer]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{100}, closer);
            co_await chan_write<int>(ch, 1, closer);
            co_return;
        })));
        auto res = std::make_shared<Box<int>>(0);
        do_async(cfgo::fix_async_lambda([ch, res, closer]() mutable -> asio::awaitable<void> {
            auto res1 = co_await chan_read<int>(ch, closer);
            EXPECT_FALSE(res1.is_canceled());
            EXPECT_EQ(res1.value(), 1);
            res->value = 1;
        }));
        co_await wait_timeout(std::chrono::milliseconds{200}, closer);
        EXPECT_EQ(res->value, 1);

        auto canceled = std::make_shared<Box<bool>>(false);
        do_async([ch, closer]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{200}, closer);
            co_await chan_write<int>(ch, 1, closer);
            co_return;
        });
        do_async([ch, canceled, closer]() mutable -> asio::awaitable<void> {
            auto timeouter = closer.create_child();
            timeouter.set_timeout(std::chrono::milliseconds{100});
            auto res2 = co_await chan_read<int>(ch, timeouter);
            EXPECT_TRUE(res2.is_canceled());
            canceled->value = true;
        });
        co_await wait_timeout(std::chrono::milliseconds{300}, closer);
        EXPECT_TRUE(canceled->value);

    };
    do_async(std::move(task), true);
}

TEST(Chan, ChanReadOrThrow) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        asiochan::channel<int> ch{};
        close_chan closer {};
        DEFER({
            closer.close();
        });
        do_async([ch, closer]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{100}, closer);
            co_await chan_write<int>(ch, 1, closer);
            co_return;
        });
        auto res = std::make_shared<Box<int>>(0);
        do_async([ch, res, closer]() mutable -> asio::awaitable<void> {
            res->value = co_await chan_read_or_throw<int>(ch, closer);
        });
        co_await wait_timeout(std::chrono::milliseconds{200}, closer);
        EXPECT_EQ(res->value, 1);

        auto canceled = std::make_shared<Box<bool>>(false);
        do_async([ch, closer]() mutable -> asio::awaitable<void> {
            co_await wait_timeout(std::chrono::milliseconds{200}, closer);
            co_await chan_write<int>(ch, 1, closer);
            co_return;
        });
        res->value = 0;
        do_async([ch, res, canceled, closer]() mutable -> asio::awaitable<void> {
            auto timeouter = closer.create_child();
            timeouter.set_timeout(std::chrono::milliseconds{100});
            try
            {
                res->value = co_await chan_read_or_throw<int>(ch, timeouter);
            }
            catch(const cfgo::CancelError& e)
            {
                canceled->value = true;
            }
        });
        co_await wait_timeout(std::chrono::milliseconds{300}, closer);
        EXPECT_TRUE(canceled->value);
        EXPECT_EQ(res->value, 0);
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
            cfgo::mutex mutex;
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
            cfgo::mutex mutex;
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
            cfgo::mutex mutex;
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
            cfgo::mutex mutex;
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

TEST(LazyBox, Get) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        auto sbox = LazyBox<shared_ptr<int>>::create();
        sbox->init(std::make_shared<int>(0));
        auto sdata = co_await sbox->get(nullptr);
        EXPECT_TRUE((bool) sdata);
        EXPECT_EQ(0, *sdata);
        sdata = co_await sbox->get(nullptr);
        EXPECT_TRUE((bool) sdata);
        EXPECT_EQ(0, *sdata);
        auto ubox = LazyBox<unique_ptr<int>>::create();
        ubox->init(std::make_unique<int>(0));
        auto udata = co_await ubox->move(nullptr);
        EXPECT_TRUE((bool) udata);
        EXPECT_EQ(0, *udata);
        udata = co_await ubox->move(nullptr);
        EXPECT_FALSE((bool) udata);
    }, true);
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

TEST(Closer, DependOnSelf) {
    using namespace cfgo;
    close_chan closer0 {};
    close_chan closer1 = closer0;
    do_async(fix_async_lambda([closer0, closer1]() -> asio::awaitable<void> {
        co_await closer1.depend_on(closer0);
        closer0.close();
        EXPECT_TRUE(closer0.is_closed());
        EXPECT_TRUE(closer1.is_closed());
    }), true);
}

TEST(AsyncBlocker, CheckDeadLock) {
    using namespace cfgo;
    AsyncBlockerManager::Configure conf {
        .block_timeout = std::chrono::milliseconds {30},
        .target_batch = 10,
        .min_batch = 1,
    };
    AsyncBlockerManager manager {conf};
    close_chan closer {};
    asio::thread_pool m_pool {};
    auto mutex = std::make_shared<cfgo::mutex>();
    for (size_t i = 0; i < 10; i++)
    {
        asio::co_spawn(m_pool.get_executor(), fix_async_lambda([i, manager, closer, mutex]() -> asio::awaitable<void> {
            auto blocker = co_await manager.add_blocker(0, closer);
            blocker.set_user_data((std::int64_t) i);
            try
            {
                do
                {
                    co_await wait_timeout(std::chrono::milliseconds {40}, closer);
                    CFGO_INFO("[blocker {}] checking block", i);
                    co_await manager.wait_blocker(blocker.id());
                } while (true);
            }
            catch(const CancelError& e) {}
        }), asio::detached);
    }
    do_async(fix_async_lambda([manager, closer, mutex]() -> asio::awaitable<void> {
        std::vector<AsyncBlocker> blockers {};
        for (size_t i = 0; i < 20; i++)
        {
            CFGO_INFO("[{}] locking", i);
            co_await manager.lock(closer);
            CFGO_INFO("[{}] locked", i);
            DEFER({
                manager.unlock();
            });
            manager.collect_locked_blocker(blockers);
            for (auto && blocker : blockers)
            {
                CFGO_INFO("got blocker {}", blocker.get_integer_user_data());
            }
        }
        closer.close();
        CFGO_INFO("closed");
    }), true);
}

TEST(InitableBox, Invoke) {
    using namespace cfgo;
    do_async(fix_async_lambda([]() -> asio::awaitable<void> {
        InitableBox<void, std::string> test([](close_chan closer, std::string arg) -> asio::awaitable<void> {
            CFGO_INFO("{}", arg);
            co_return;
        }, false);
        close_chan closer {};
        std::string str = "test";
        co_await test(std::move(closer), std::move(str));
    }), true);
}

TEST(AsyncBlocker, CheckBlockTime) {
    using namespace cfgo;
    AsyncBlockerManager::Configure conf {
        .block_timeout = std::chrono::milliseconds {30},
        .target_batch = 3,
        .min_batch = 1,
    };
    AsyncBlockerManager manager {conf};
    close_chan closer {};
    auto m_pool = std::make_shared<asio::thread_pool>();
    std::mt19937 gen(1);
    std::uniform_int_distribution<int> distrib(0, 1500);
    for (size_t i = 0; i < 30; i++)
    {
        asio::co_spawn(m_pool->get_executor(), fix_async_lambda([i, manager, closer, amp = distrib(gen)]() -> asio::awaitable<void> {
            auto blocker = co_await manager.add_blocker(0, closer);
            DEFER({
                manager.remove_blocker(blocker.id());
            });
            blocker.set_user_data((std::int64_t) i);
            DurationMeasure measure(3);
            try
            {
                do
                {
                    co_await wait_timeout(std::chrono::milliseconds {100 + amp}, closer);
                    {
                        ScopeDurationMeasurer measurer(measure);
                        co_await manager.wait_blocker(blocker.id(), closer);
                    }
                    measure.run_greater_than(std::chrono::milliseconds(70), [i](const DurationMeasure & m) {
                        CFGO_INFO("blocker {} block {} ms", i, std::chrono::duration_cast<std::chrono::milliseconds>(*m.latest()).count());
                    });
                } while (true);
            }
            catch(const CancelError& e) {}
        }), asio::detached);
    }
    do_async(fix_async_lambda([manager, closer]() -> asio::awaitable<void> {
        close_guard cg(closer);
        DurationMeasure measure(3);
        for (size_t i = 0; i < 1000; i++)
        {
            {
                ScopeDurationMeasurer measurer(measure);
                co_await manager.lock(closer);
            }
            DEFER({
                manager.unlock();
            });
            measure.run_greater_than(std::chrono::milliseconds(70), [i](const DurationMeasure & m) {
                CFGO_INFO("blocker manager locked {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(*m.latest()).count());
            });
        }
    }), true, m_pool);
}

TEST(AsyncBlocker, CheckBlockedNum) {
    using namespace cfgo;
    AsyncBlockerManager::Configure conf {
        .block_timeout = std::chrono::milliseconds {100},
        .target_batch = 3,
        .min_batch = 1,
    };
    AsyncBlockerManager manager {conf};
    close_chan closer {};
    auto m_pool = std::make_shared<asio::thread_pool>();
    std::mt19937 gen(1);
    std::uniform_int_distribution<int> distrib(-10, 10);
    for (size_t i = 0; i < 30; i++)
    {
        asio::co_spawn(m_pool->get_executor(), fix_async_lambda([i, manager, closer, amp = distrib(gen)]() -> asio::awaitable<void> {
            auto blocker = co_await manager.add_blocker(0, closer);
            DEFER({
                manager.remove_blocker(blocker.id());
            });
            blocker.set_user_data((std::int64_t) i);
            try
            {
                do
                {
                    co_await wait_timeout(std::chrono::milliseconds {200 + amp}, closer);
                    co_await manager.wait_blocker(blocker.id(), closer);
                } while (true);
            }
            catch(const CancelError& e) {}
        }), asio::detached);
    }
    do_async(fix_async_lambda([manager, closer]() -> asio::awaitable<void> {
        std::vector<AsyncBlocker> blockers {};
        for (size_t i = 0; i < 100; i++)
        {
            co_await manager.lock(closer);
            DEFER({
                manager.unlock();
            });
            manager.collect_locked_blocker(blockers);
            EXPECT_LE(blockers.size(), 3);
        }
        closer.close();
        CFGO_INFO("closed");
    }), true, m_pool);
}

TEST(StateNotifier, Notify) {
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        state_notifier nt1 {};
        state_notifier nt2 {};
        close_chan closer {};
        close_guard g(closer);
        auto sum1 = std::make_shared<Box<std::atomic_int>>(0);
        auto sum2 = std::make_shared<Box<std::atomic_int>>(0);
        for (size_t i = 0; i < 100; i++)
        {
            do_async([nt1, nt2, sum1, sum2, closer]() -> asio::awaitable<void> {
                do
                {
                    auto ch = nt1.make_notfiy_receiver();
                    sum1->value += 1;
                    nt2.notify();
                    if (co_await chan_read<void>(ch, closer))
                    {
                        sum2->value += 1;
                    }
                    else
                    {
                        break;
                    }
                } while (true);
            });
        }
        auto i = 0;
        do
        {
            auto ch = nt2.make_notfiy_receiver();
            if (sum1->value.load() % 100 == 0)
            {
                nt1.notify();
                ++i;
            }
            if (!co_await chan_read<void>(ch, closer))
            {
                break;
            }
            if (i == 10)
            {
                break;
            }
            
        } while (true);
        co_await wait_timeout(std::chrono::milliseconds(300), closer);
        EXPECT_EQ(1000, sum2->value.load());
    }, true);
}

int main(int argc, char **argv) {
    TP = std::make_unique<asio::thread_pool>();
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::DEFAULT, spdlog::level::trace);
    auto ret = RUN_ALL_TESTS();
    TP = nullptr;
    return ret;
}