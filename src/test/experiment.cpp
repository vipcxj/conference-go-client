#include "cfgo/async.hpp"
#include "cfgo/async_locker.hpp"
#include "cfgo/measure.hpp"
#include "cfgo/log.hpp"
#include "cfgo/allocate_tracer.hpp"
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
    using alc_tracers =  allocate_tracers;
    using state_t = alc_tracers::tracer_state;
    state_t & state = state_t::instance();
    {
        auto ptr = alc_tracers::make_shared<int>(3);
        #ifdef CFGO_GENERAL_ALLOCATE_TRACER_DETAIL
        EXPECT_TRUE(state.entry(ptr.get()).has_detail());
        #else
        EXPECT_FALSE(state.entry(ptr.get()).has_detail());
        #endif
        EXPECT_EQ(alc_tracers::ref_count(typeid(int)), 1);
    }
    EXPECT_EQ(alc_tracers::ref_count(typeid(int)), 0);
    {
        auto ptr = state.make_shared<TestObj>();
        EXPECT_TRUE(state.entry(ptr.get()).has_detail());
        EXPECT_EQ(alc_tracers::ref_count(typeid(TestObj)), 1);
        alc_tracers::tracer_entry_result_set result;
        alc_tracers::collect_max_n_ref_count(result, 1);
        EXPECT_EQ(result.size(), 1);
        EXPECT_EQ(result[0].get().type_name(), boost::core::demangle(typeid(TestObj).name()));
    }
    EXPECT_EQ(alc_tracers::ref_count(typeid(TestObj)), 0);
}

int main(int argc, char **argv) {
    TP = std::make_unique<asio::thread_pool>();
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::Category::DEFAULT, cfgo::LogLevel::trace);
    auto ret = RUN_ALL_TESTS();
    TP = nullptr;
    return ret;
}