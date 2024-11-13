#include "cfgo/async.hpp"
#include "cfgo/async_locker.hpp"
#include "cfgo/measure.hpp"
#include "cfgo/log.hpp"
#include "cfgo/allocate_tracer.hpp"
#include "cfgo/black_magic.hpp"
#include "cfgo/furcate_stream.hpp"
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

// TEST(Closer, ParentAndChildrenCloseTogether) {
//     using namespace cfgo;
//     std::random_device rd {};
//     std::mt19937 gen(rd());
//     std::vector<close_chan> parents {};
//     std::vector<close_chan> children {};
//     for (size_t i = 0; i < 100; i++)
//     {
//         close_chan parent {};
//         parents.push_back(parent);
//         for (size_t i = 0; i < 10; i++)
//         {
//             auto child = parent.create_child();
//             children.push_back(child);
//         }
//     }
//     std::shuffle(parents.begin(), parents.end(), gen);
//     for (auto && closer : parents)
//     {
//         std::thread([closer]() {
//             std::this_thread::sleep_for(std::chrono::milliseconds {100});
//             closer.close();
//         }).detach();
//     }
//     std::this_thread::sleep_for(std::chrono::milliseconds {1000});
//     for (auto && closer : parents)
//     {
//         EXPECT_TRUE(closer.is_closed());
//     }
//     for (auto && closer : children)
//     {
//         EXPECT_TRUE(closer.is_closed());
//     }
// }

struct TestObj
{
    static constexpr bool allocate_tracer_detail = true;
};

TEST(AllocateTracer, Tracer)
{
    using namespace cfgo;
    using namespace std::chrono_literals;
    asio::io_context io_ctx;
    do_async([]() -> asio::awaitable<void> {
        close_chan closer {};
        closer.set_timeout(1s, "timeout after 1s");
        close_guard cg {closer};
        auto executor = co_await asio::this_coro::executor;
        auto stream = FurcateStream<int, 1>::create(executor, 50ms);
        for (int i = 0; i < 3; i++)
        {
            std::thread t([stream, i, closer]() {
                auto branch = stream->create_branch();
                std::mt19937 gen(i);
                std::uniform_int_distribution<int> distrib(30, 120);
                for (int j = 1; j < 10; j += 2)
                {
                    CFGO_INFO("[sync {}]({}) reading...", i, j);
                    auto delay = std::chrono::milliseconds { distrib(gen) };
                    std::this_thread::sleep_for(delay);
                    auto res = branch->value().receive_sync(closer);
                    if (res)
                    {
                        CFGO_INFO("[sync {}]({}) got {} after delay {} ms", i, j, *res, delay.count());
                    }
                    else
                    {
                        CFGO_INFO("[sync {}]({}) got nothing after delay {} ms", i, j, *res, delay.count());
                    }
                }
            });
            t.detach();
            asio::co_spawn(executor, [stream, i, closer]() -> asio::awaitable<void> {
                auto branch = stream->create_branch();
                std::mt19937 gen(i);
                std::uniform_int_distribution<int> distrib(30, 120);
                for (int j = 0; j < 10; j += 2)
                {
                    CFGO_INFO("[async {}]({}) reading...", i, j);
                    auto delay = std::chrono::milliseconds { distrib(gen) };
                    co_await wait_timeout(delay);
                    auto res = co_await branch->value().receive_async(closer);
                    if (res)
                    {
                        CFGO_INFO("[async {}]({}) got {} after delay {} ms", i, j, *res, delay.count());
                    }
                    else
                    {
                        CFGO_INFO("[async {}]({}) got nothing after delay {} ms", i, j, *res, delay.count());
                    }
                }
            }, asio::detached);
        }
        
        std::thread t([closer, stream]() {
            for (int i = 1; i < 10; i += 2)
            {
                CFGO_INFO("sync sending {}", i);
                stream->send_sync(i, closer);
                CFGO_INFO("sync sended {}", i);
            }
        });
        co_await asio::co_spawn(executor, [closer, stream]() -> asio::awaitable<void> {
            for (int i = 0; i < 10; i += 2)
            {
                CFGO_INFO("async sending {}", i);
                co_await stream->send_async(i, closer);
                CFGO_INFO("async sended {}", i);
            }
        }, asio::use_awaitable);
        t.join();
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