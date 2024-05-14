#include "cfgo/coevent.hpp"
#include "asio.hpp"
#include <iostream>
#include <thread>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

class CoEventPartner
{
public:
    MOCK_METHOD(void, before_trigger, ());
    MOCK_METHOD(void, after_trigger, (bool res));
    MOCK_METHOD(void, before_await, ());
    MOCK_METHOD(void, after_await, ());
};

template<unsigned int N = 1>
class CoEventWrpper
{
protected:
    CoEventWrpper():event(cfgo::CoEvent::create(N)), partner() {}
public:
    cfgo::CoEvent::Ptr event;
    CoEventPartner partner;

    static std::shared_ptr<CoEventWrpper<N>> create() {
        return std::shared_ptr<CoEventWrpper<N>>(new CoEventWrpper<N>());
    }
    ~CoEventWrpper() = default;

    bool trigger() {
        partner.before_trigger();
        bool res = event->trigger();
        partner.after_trigger(res);
        return res;
    }

    asio::awaitable<void> await(std::chrono::nanoseconds timeout = std::chrono::nanoseconds{0}) {
        partner.before_await();
        co_await event->await(timeout);
        partner.after_await();
    }
};

class CoEventTest : public testing::Test 
{
public:
    asio::io_context io_ctx;
protected:

    CoEventTest(): io_ctx() {}
    ~CoEventTest() = default;

    void do_test(std::function<asio::awaitable<void>(CoEventTest *)> task) {
        asio::co_spawn(io_ctx, [this, task = std::move(task)]() -> asio::awaitable<void> {
            return task(this);
        }, asio::detached);
        io_ctx.run();
    }
};

TEST_F(CoEventTest, TriggerOnce) {
    do_test([](CoEventTest* self) -> asio::awaitable<void> {
        auto event = CoEventWrpper<>::create();
        testing::Sequence s1, s2, s3;
        // event->trigger();
        EXPECT_CALL(event->partner, before_trigger())
            .InSequence(s1)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_trigger(true))
            .InSequence(s1, s2)
            .WillOnce(testing::Return());
        // event->trigger();
        EXPECT_CALL(event->partner, before_trigger())
            .InSequence(s2)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_trigger(false))
            .InSequence(s2)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .InSequence(s3)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .InSequence(s1, s3)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .InSequence(s3)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .InSequence(s3)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .InSequence(s3)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .InSequence(s3)
            .WillOnce(testing::Return());

        std::thread([event, self]() {
            auto work = asio::make_work_guard(self->io_ctx);
            std::this_thread::sleep_for(std::chrono::seconds{1});
            event->trigger();
            event->trigger();
        }).detach();
        co_await event->await();
        co_await event->await();
        co_await event->await();
    });
}

TEST_F(CoEventTest, TriggerTwo) {
    do_test([](CoEventTest* self) -> asio::awaitable<void> {
        auto event = CoEventWrpper<2>::create();
        testing::Sequence s1, s2, s3;
        // event->trigger();
        EXPECT_CALL(event->partner, before_trigger())
            .InSequence(s1)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_trigger(false))
            .InSequence(s1)
            .WillOnce(testing::Return());
        // event->trigger();
        EXPECT_CALL(event->partner, before_trigger())
            .InSequence(s1)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_trigger(true))
            .InSequence(s1, s2)
            .WillOnce(testing::Return());
        // event->trigger();
        EXPECT_CALL(event->partner, before_trigger())
            .InSequence(s2)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_trigger(false))
            .InSequence(s2)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .InSequence(s3)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .InSequence(s1, s3)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .InSequence(s1)
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .InSequence(s1)
            .WillOnce(testing::Return());

        std::thread([event, self]() {
            auto work = asio::make_work_guard(self->io_ctx);
            std::this_thread::sleep_for(std::chrono::seconds{1});
            event->trigger();
            event->trigger();
            event->trigger();
        }).detach();
        co_await event->await();
        co_await event->await();
    });
}

TEST_F(CoEventTest, TriggerException) {
    do_test([](CoEventTest* self) -> asio::awaitable<void> {
        auto event = CoEventWrpper<>::create();
        testing::Sequence s1, s2, s3;
        // event->trigger();
        auto&& first_before_trigger = EXPECT_CALL(event->partner, before_trigger())
            .WillOnce(testing::Return());
        auto&& first_after_trigger = EXPECT_CALL(event->partner, after_trigger(true))
            .After(first_before_trigger)
            .WillOnce(testing::Return());
        // event->trigger();
        auto&& second_before_trigger = EXPECT_CALL(event->partner, before_trigger())
            .After(first_after_trigger)
            .WillOnce(testing::Return());
        auto&& second_after_trigger = EXPECT_CALL(event->partner, after_trigger(false))
            .After(second_before_trigger)
            .WillOnce(testing::Return());
        // co_await event->await();
        EXPECT_CALL(event->partner, before_await())
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .Times(0);

        std::thread([event, self]() {
            auto work = asio::make_work_guard(self->io_ctx);
            std::this_thread::sleep_for(std::chrono::seconds{1});
            try
            {
                throw "error";
            }
            catch(...)
            {
                event->trigger();
                event->trigger();
            }
        }).detach();
        EXPECT_THROW({
            co_await event->await();
        }, const char*);
    });
}

TEST_F(CoEventTest, TriggerTimeout) {
    do_test([](CoEventTest* self) -> asio::awaitable<void> {
        auto event = CoEventWrpper<>::create();
        EXPECT_CALL(event->partner, before_await())
            .WillOnce(testing::Return());
        EXPECT_CALL(event->partner, after_await())
            .Times(0);
        EXPECT_THROW({
            co_await event->await(std::chrono::milliseconds{500});
        }, decltype(std::make_error_code(std::errc::timed_out)));
    });
}


int main(int argc, char **argv) {
    testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}