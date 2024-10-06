#include "cfgo/async.hpp"
#include "cfgo/measure.hpp"
#include "cfgo/log.hpp"
#include "gtest/gtest.h"
#include <random>

std::shared_ptr<asio::thread_pool> TP = std::make_unique<asio::thread_pool>();

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

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::Category::DEFAULT, cfgo::LogLevel::trace);
    return RUN_ALL_TESTS();
}