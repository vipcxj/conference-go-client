#include "gtest/gtest.h"
#include "cfgo/asio.hpp"
#include "cfgo/async.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/log.hpp"
#include "cfgo/signal.hpp"
#include "cfgo/token.hpp"

#define AUTH_HOST "localhost"
#define AUTH_PORT 3100
#define SIGNAL_HOST "localhost"
#define SIGNAL_PORT 8080

auto get_token(std::string_view uid, std::string_view room, bool auto_join = false) -> asio::awaitable<std::string> {
    return cfgo::utils::get_token(AUTH_HOST, AUTH_PORT, uid, uid, uid, "test", room, auto_join);
}

void do_async(std::function<asio::awaitable<void>()> func, bool multithread = true) {
    auto io_context = std::make_shared<asio::io_context>();
    auto nthread = multithread ? 8 : 1;
    std::vector<std::thread> threads {};
    for (size_t i = 0; i < nthread; i++)
    {
        threads.emplace_back([io_context]() {
            auto work = asio::make_work_guard(io_context->get_executor());
            io_context->run();
        });
    }
    auto strand = asio::make_strand(*io_context);
    asio::co_spawn(strand, cfgo::fix_async_lambda([io_context, func = std::move(func)]() -> asio::awaitable<void> {
        DEFER({
            io_context->stop();
        });
        try
        {
            co_await func();
        }
        catch(...)
        {
            CFGO_ERROR("{}", cfgo::what());
        }
    }), asio::detached);
    for (size_t i = 0; i < nthread; i++)
    {
        threads[i].join();
    }
}

TEST(Signal, JoinAndLeave) {
    do_async([]() -> asio::awaitable<void> {
        using namespace cfgo;
        close_chan closer {};
        auto token = co_await get_token("1", "root.*", false);
        auto signal = make_websocket_signal(closer, cfgo::WebsocketSignalConfigure{
            .url = fmt::format("ws://{}:{}/ws", SIGNAL_HOST, SIGNAL_PORT),
            .token = token,
            .ready_timeout = std::chrono::seconds(30),
        });
        {
            co_await signal->join(closer, "root.room1");
            EXPECT_EQ(1, signal->rooms().size());
            EXPECT_TRUE(signal->rooms().contains("root.room1"));
        }
        {
            Rooms rooms_arg({"root.room3", "root.room2"});
            co_await signal->join(closer, std::move(rooms_arg));
            RoomSet expect({"root.room1", "root.room2", "root.room3"});
            EXPECT_EQ(expect, signal->rooms());
        }
        {
            RoomSet expect({"root.room1", "root.room2", "root.room3"});
            EXPECT_THROW({
                try
                {
                    co_await signal->join(closer, "room1");
                }
                catch(const std::exception& e)
                {
                    EXPECT_STREQ("no right for room room1", e.what());
                    throw;
                }
            }, ServerError);
            EXPECT_EQ(expect, signal->rooms());
            EXPECT_THROW({
                try
                {
                    Rooms rooms_arg({"room2", "room3"});
                    co_await signal->join(closer, std::move(rooms_arg));
                }
                catch(const std::exception& e)
                {
                    EXPECT_STREQ("no right for room room2", e.what());
                    throw;
                }
            }, ServerError);
            EXPECT_EQ(expect, signal->rooms());
            EXPECT_THROW({
                try
                {
                    Rooms rooms_arg({"root.room4", "room4"});
                    co_await signal->join(closer, std::move(rooms_arg));
                }
                catch(const std::exception& e)
                {
                    EXPECT_STREQ("no right for room room4", e.what());
                    throw;
                }
            }, ServerError);
            EXPECT_EQ(expect, signal->rooms());
        }
        {
            co_await signal->leave(closer, "root.room1");
            RoomSet expect({"root.room2", "root.room3"});
            EXPECT_EQ(expect, signal->rooms());
        }
        {
            co_await signal->leave(closer, "root.room4");
            RoomSet expect({"root.room2", "root.room3"});
            EXPECT_EQ(expect, signal->rooms());
        }
        {
            co_await signal->leave(closer, "root.room3");
            RoomSet expect({"root.room2"});
            EXPECT_EQ(expect, signal->rooms());
        }
    });
}

TEST(Signal, SendMessage) {
    do_async([]() -> asio::awaitable<void> {
        using namespace cfgo;
        close_chan closer {};
        auto token1 = co_await get_token("1", "room", true);
        auto signal1 = make_websocket_signal(closer, cfgo::WebsocketSignalConfigure{
            .url = fmt::format("ws://{}:{}/ws", SIGNAL_HOST, SIGNAL_PORT),
            .token = token1,
            .ready_timeout = std::chrono::seconds(30),
        });
        auto id1 = co_await signal1->id(closer);
        auto token2 = co_await get_token("2", "room", true);
        auto signal2 = make_websocket_signal(closer, cfgo::WebsocketSignalConfigure{
            .url = fmt::format("ws://{}:{}/ws", SIGNAL_HOST, SIGNAL_PORT),
            .token = token2,
            .ready_timeout = std::chrono::seconds(30),
        });
        auto id2 = co_await signal2->id(closer);
        {
            unique_void_chan signal1_cb_done {};
            auto cb_id_1 = signal1->on_message(fix_async_lambda([closer, signal1_cb_done, id2](SignalMsgPtr msg, SignalAckerPtr acker) -> asio::awaitable<bool> {
                EXPECT_EQ("room", msg->room());
                EXPECT_EQ(id2, msg->socket_id());
                EXPECT_EQ("hello", msg->evt());
                EXPECT_EQ("world from 2", msg->payload());
                EXPECT_EQ(false, msg->ack());
                auto content = msg->consume();
                EXPECT_EQ("world from 2", content);
                co_await acker->ack(closer, "ack");
                chan_maybe_write(signal1_cb_done);
                co_return false;
            }));
            unique_void_chan signal2_cb_done {};
            auto cb_id_2 = signal2->on_message(fix_async_lambda([closer, signal2_cb_done, id1](SignalMsgPtr msg, SignalAckerPtr acker) -> asio::awaitable<bool> {
                EXPECT_EQ("room", msg->room());
                EXPECT_EQ(id1, msg->socket_id());
                EXPECT_EQ("hello", msg->evt());
                EXPECT_EQ("world from 1", msg->payload());
                EXPECT_EQ(false, msg->ack());
                auto content = msg->consume();
                EXPECT_EQ("world from 1", content);
                co_await acker->ack(closer, "ack");
                chan_maybe_write(signal2_cb_done);
                co_return false;
            }));
            DEFER({
                signal1->off_message(cb_id_1);
                signal2->off_message(cb_id_2);
            });
            auto ack_from_2 = co_await signal1->send_message(closer, signal1->create_message("hello", false, "room", id2, "world from 1"));
            EXPECT_EQ("", ack_from_2);
            auto ack_from_1 = co_await signal2->send_message(closer, signal2->create_message("hello", false, "room", id1, "world from 2"));
            EXPECT_EQ("", ack_from_1);
            co_await chan_read_or_throw<void>(signal1_cb_done, closer);
            co_await chan_read_or_throw<void>(signal2_cb_done, closer);
        }
    });
}

TEST(Signal, KeepAlive) {
    do_async([]() -> asio::awaitable<void> {
        using namespace cfgo;
        close_chan closer {};
        DEFER({
            closer.close();
        });
        auto token1 = co_await get_token("1", "room", true);
        auto signal1 = make_websocket_signal(closer, cfgo::WebsocketSignalConfigure{
            .url = fmt::format("ws://{}:{}/ws", SIGNAL_HOST, SIGNAL_PORT),
            .token = token1,
            .ready_timeout = std::chrono::seconds(30),
        });
        auto id1 = co_await signal1->id(closer);
        auto token2 = co_await get_token("2", "room", true);
        auto signal2 = make_websocket_signal(closer, cfgo::WebsocketSignalConfigure{
            .url = fmt::format("ws://{}:{}/ws", SIGNAL_HOST, SIGNAL_PORT),
            .token = token2,
            .ready_timeout = std::chrono::seconds(30),
        });
        auto id2 = co_await signal2->id(closer);
        auto keep_alive_closer = closer.create_child();
        co_await signal1->keep_alive(keep_alive_closer, "room", id2, true, std::chrono::seconds{1}, [](const KeepAliveContext & ctx) -> bool {
            if (ctx.err)
            {
                ADD_FAILURE() << what(ctx.err);
                return true;
            }
            else if (ctx.timeout_num > 1)
            {
                ADD_FAILURE() << "pong timeout";
                return true;
            }
            else
            {
                return false;
            }
        });
        co_await signal2->keep_alive(keep_alive_closer, "room", id1, false, std::chrono::seconds{2}, [](const KeepAliveContext & ctx) -> bool {
            if (ctx.err)
            {
                ADD_FAILURE() << what(ctx.err);
                return true;
            }
            else if (ctx.timeout_num > 1)
            {
                ADD_FAILURE() << "ping timeout";
                return true;
            }
            else
            {
                return false;
            }
        });
        co_await wait_timeout(std::chrono::seconds{10}, closer);
        keep_alive_closer.close();
        keep_alive_closer = close_chan {};
        co_await signal1->keep_alive(keep_alive_closer, "room", id2, true, std::chrono::seconds{1}, make_keep_alive_callback(keep_alive_closer, 0, duration_t{2}, -1, duration_t{0}));
        co_await wait_timeout(std::chrono::seconds{5}, closer);
        EXPECT_FALSE(keep_alive_closer.is_closed());
        signal2->close();
        co_await wait_timeout(std::chrono::seconds{2}, closer);
        EXPECT_TRUE(keep_alive_closer.is_closed());
    });
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    // cfgo::Log::instance().set_level(cfgo::Log::DEFAULT, spdlog::level::trace);
    return RUN_ALL_TESTS();
}