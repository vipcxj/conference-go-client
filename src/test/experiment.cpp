#include "cfgo/async.hpp"
#include "cfgo/async_locker.hpp"
#include "cfgo/measure.hpp"
#include "cfgo/log.hpp"
#ifdef CFGO_CLOSER_ALLOCATOR_TRACER
#include "cfgo/allocator_tracer.hpp"
#endif
#include "gtest/gtest.h"
#include <random>

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

TEST(Timeout, CompareWithReal)
{
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        close_chan closer;
        close_guard cg(closer);
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(0, 90);
        DurationMeasure m(3);
        for (size_t i = 0; i < 30; i++)
        {
            auto amp = 10 + distrib(gen);
            using namespace std::chrono;
            {
                ScopeDurationMeasurer sm(m);
                co_await wait_timeout(milliseconds{amp}, closer);
            }
            auto really = duration_cast<milliseconds>(*m.latest()).count();
            CFGO_INFO("expect: {} ms, really: {} ms, diff: {}", amp, really, std::abs(really - amp));
        }
    }, true);
}

TEST(ChanRead, CompareWithReal)
{
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        close_chan closer;
        close_guard cg(closer);
        unique_void_chan ch {};
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(0, 90);
        DurationMeasure m(3);
        for (size_t i = 0; i < 30; i++)
        {
            auto amp = 10 + distrib(gen);
            using namespace std::chrono;
            {
                ScopeDurationMeasurer sm(m);
                auto timeouter = closer.create_child();
                timeouter.set_timeout(milliseconds{amp});
                co_await chan_read<void>(ch, timeouter);
            }
            auto really = duration_cast<milliseconds>(*m.latest()).count();
            CFGO_INFO("expect: {} ms, really: {} ms, diff: {}", amp, really, std::abs(really - amp));
        }
    }, true);
}

TEST(TaskAll, CompareWithReal)
{
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        close_chan closer;
        close_guard cg(closer);
        unique_void_chan ch {};
        std::mt19937 gen(1);
        std::uniform_int_distribution<int> distrib(0, 90);
        DurationMeasure m(3);
        for (size_t i = 0; i < 30; i++)
        {
            auto amp = 10 + distrib(gen);
            using namespace std::chrono;
            AsyncTasksAll<void> tasks(closer);
            for (size_t j = 0; j < 10; j++)
            {
                tasks.add_task(fix_async_lambda([ch, amp](close_chan closer) -> asio::awaitable<void> {
                    auto timeouter = closer.create_child();
                    timeouter.set_timeout(milliseconds{amp});
                    co_await chan_read<void>(ch, timeouter);
                }));
            }
            
            {
                ScopeDurationMeasurer sm(m);
                co_await tasks.await();
            }
            auto really = duration_cast<milliseconds>(*m.latest()).count();
            CFGO_INFO("expect: {} ms, really: {} ms, diff: {}", amp, really, std::abs(really - amp));
        }
    }, true);
}

TEST(AsyncBlocker, CompareWithReal)
{
    using namespace cfgo;
    do_async([]() -> asio::awaitable<void> {
        #ifdef CFGO_CLOSER_ALLOCATOR_TRACER
            EXPECT_EQ(0, cfgo::close_allocator_tracer::ref_count());
        #endif
        {
            close_chan closer;
            close_guard cg(closer);
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER
                EXPECT_EQ(1, cfgo::close_allocator_tracer::ref_count());
            #endif
            const auto block_timeout = 30;
            AsyncBlockerManager::Configure conf {
                .block_timeout = std::chrono::milliseconds {block_timeout},
                .target_batch = 10,
                .min_batch = 1,
            };
            AsyncBlockerManager manager {conf};
            DurationMeasure m(3);
            auto executor = co_await asio::this_coro::executor;
            for (size_t i = 0; i < 20; i++)
            {
                asio::co_spawn(executor, [manager, closer, i]() -> asio::awaitable<void> {
                    auto blocker = co_await manager.add_blocker(0, closer);
                    AsyncBlockerRemover remover(manager, blocker);
                    blocker.set_user_data((std::int64_t) i);
                    std::mt19937 gen(i);
                    std::uniform_int_distribution<int> distrib(0, 800);
                    try
                    {
                        do
                        {
                            auto amp = distrib(gen);
                            co_await wait_timeout(std::chrono::milliseconds {200 + amp}, closer);
                            co_await manager.wait_blocker(blocker.id(), closer);
                        } while (true);
                    }
                    catch(const CancelError& e) {}
                }, asio::detached);
            }
            std::mt19937 gen(0);
            std::uniform_int_distribution<int> distrib(0, 20);
            for (size_t i = 0; i < 30; i++)
            {
                {
                    ScopeDurationMeasurer sdm(m);
                    co_await manager.lock(closer);
                }
                AsyncBlockerUnlocker unlocker(manager);
                co_await wait_timeout(std::chrono::milliseconds {distrib(gen)}, closer);

                CFGO_INFO("expect: {} ms, really: {} ms, diff: {}", block_timeout, cast_ms(m.latest()), std::abs(cast_ms(m.latest()) - block_timeout));
            }
        }
        // co_await wait_timeout(std::chrono::milliseconds {1});
        // #ifdef CFGO_CLOSER_ALLOCATOR_TRACER
        //     EXPECT_EQ(0, cfgo::close_allocator_tracer::ref_count());
        // #endif
    }, true);
}

int main(int argc, char **argv) {
    TP = std::make_unique<asio::thread_pool>();
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::Category::DEFAULT, cfgo::LogLevel::trace);
    auto ret = RUN_ALL_TESTS();
    TP = nullptr;
    return ret;
}