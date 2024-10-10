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

TEST(Closer, ParentAndChildrenCloseTogether) {
    using namespace cfgo;
    std::random_device rd {};
    std::mt19937 gen(rd());
    std::vector<close_chan> parents {};
    std::vector<close_chan> children {};
    for (size_t i = 0; i < 100; i++)
    {
        close_chan parent {};
        parents.push_back(parent);
        for (size_t i = 0; i < 10; i++)
        {
            auto child = parent.create_child();
            children.push_back(child);
        }
    }
    std::shuffle(parents.begin(), parents.end(), gen);
    for (auto && closer : parents)
    {
        std::thread([closer]() {
            std::this_thread::sleep_for(std::chrono::milliseconds {100});
            closer.close();
        }).detach();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds {1000});
    for (auto && closer : parents)
    {
        EXPECT_TRUE(closer.is_closed());
    }
    for (auto && closer : children)
    {
        EXPECT_TRUE(closer.is_closed());
    }
}

int main(int argc, char **argv) {
    TP = std::make_unique<asio::thread_pool>();
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::Category::DEFAULT, cfgo::LogLevel::trace);
    auto ret = RUN_ALL_TESTS();
    TP = nullptr;
    return ret;
}