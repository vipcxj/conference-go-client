#include "cfgo/async.hpp"
#include "cfgo/async_locker.hpp"
#include "cfgo/measure.hpp"
#include "cfgo/log.hpp"
#include "cfgo/allocate_tracer.hpp"
#include "cfgo/black_magic.hpp"
#include "cfgo/furcate_stream.hpp"
#include "cfgo/video/media_source.hpp"
#include "gtest/gtest.h"
#include <random>

struct TestObject1
{
    TestObject1(int a): a(a) {};
    virtual ~TestObject1() {}
    int a;
    virtual void m() = 0;
};

struct TestObject2 : public TestObject1
{
    int b;

    TestObject2(int a, int b): TestObject1(a), b(b) {}
    void m() override
    {
        CFGO_INFO("call m in test object 2");
    }
};

template<typename T>
class TestBase
{
private:
    T m_data;
public:
    TestBase(const T & data): m_data(data) {};
    TestBase(T && data): m_data(std::move(data)) {};

    auto operator->() noexcept -> T & requires requires (T & a) { a.operator->(); }
    {
        return m_data;
    }
    auto operator->() noexcept -> T *
    {
        return &m_data;
    }
    auto operator->() const noexcept -> const T & requires requires (const T & a) { a.operator->(); }
    {
        return m_data;
    }
    auto operator->() const noexcept -> T const *
    {
        return &m_data;
    }
};


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

TEST(MediaSource, TestIt)
{
    using namespace cfgo::video;
    asio::io_context io_ctx {};
    cfgo::close_chan closer {};
    cfgo::close_guard {closer};
    auto media_source = make_media_source(io_ctx.get_executor(), media_source_type_t::DEVICE, "", media_source_mode_t::AUTO, closer);
}

// TEST(AllocateTracer, Tracer)
// {
//     using namespace cfgo;
//     using namespace std::chrono_literals;
//     asio::io_context io_ctx;
//     do_async([]() -> asio::awaitable<void> {
//         close_chan closer {};
//         closer.set_timeout(1s, "timeout after 1s");
//         close_guard cg {closer};
//         auto executor = co_await asio::this_coro::executor;
//         auto stream = FurcateStream<int, 1>::create(executor, 50ms);
//         for (int i = 0; i < 3; i++)
//         {
//             std::thread t([stream, i, closer]() {
//                 auto branch = stream->create_branch();
//                 std::mt19937 gen(i);
//                 std::uniform_int_distribution<int> distrib(30, 120);
//                 for (int j = 1; j < 10; j += 2)
//                 {
//                     CFGO_INFO("[sync {}]({}) reading...", i, j);
//                     auto delay = std::chrono::milliseconds { distrib(gen) };
//                     std::this_thread::sleep_for(delay);
//                     auto res = branch->value().receive_sync(closer);
//                     if (res)
//                     {
//                         CFGO_INFO("[sync {}]({}) got {} after delay {} ms", i, j, *res, delay.count());
//                     }
//                     else
//                     {
//                         CFGO_INFO("[sync {}]({}) got nothing after delay {} ms", i, j, *res, delay.count());
//                     }
//                 }
//             });
//             t.detach();
//             asio::co_spawn(executor, [stream, i, closer]() -> asio::awaitable<void> {
//                 auto branch = stream->create_branch();
//                 std::mt19937 gen(i);
//                 std::uniform_int_distribution<int> distrib(30, 120);
//                 for (int j = 0; j < 10; j += 2)
//                 {
//                     CFGO_INFO("[async {}]({}) reading...", i, j);
//                     auto delay = std::chrono::milliseconds { distrib(gen) };
//                     co_await wait_timeout(delay);
//                     auto res = co_await branch->value().receive_async(closer);
//                     if (res)
//                     {
//                         CFGO_INFO("[async {}]({}) got {} after delay {} ms", i, j, *res, delay.count());
//                     }
//                     else
//                     {
//                         CFGO_INFO("[async {}]({}) got nothing after delay {} ms", i, j, *res, delay.count());
//                     }
//                 }
//             }, asio::detached);
//         }
        
//         std::thread t([closer, stream]() {
//             for (int i = 1; i < 10; i += 2)
//             {
//                 CFGO_INFO("sync sending {}", i);
//                 stream->send_sync(i, closer);
//                 CFGO_INFO("sync sended {}", i);
//             }
//         });
//         co_await asio::co_spawn(executor, [closer, stream]() -> asio::awaitable<void> {
//             for (int i = 0; i < 10; i += 2)
//             {
//                 CFGO_INFO("async sending {}", i);
//                 co_await stream->send_async(i, closer);
//                 CFGO_INFO("async sended {}", i);
//             }
//         }, asio::use_awaitable);
//         t.join();
//     }, true);
// }

int main(int argc, char **argv) {
    TestBase<TestObject2> t1 {TestObject2 {1, 2}};
    TestBase<TestBase<TestObject2>> t2 {t1};
    CFGO_INFO("{}", t2->a);

    

    TP = std::make_unique<asio::thread_pool>();
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::Category::DEFAULT, cfgo::LogLevel::trace);
    auto ret = RUN_ALL_TESTS();
    TP = nullptr;
    return ret;
}