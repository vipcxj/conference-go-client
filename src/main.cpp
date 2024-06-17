#include "cfgo/asio.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <future>
#include "cfgo/cothread.hpp"

using namespace asio;

std::string what(const std::exception_ptr &eptr = std::current_exception())
{
    if (!eptr) { throw std::bad_exception(); }

    try { std::rethrow_exception(eptr); }
    catch (const std::exception  &e)  { return e.what()   ; }
    catch (const std::string     &e)  { return e          ; }
    catch (const char            *e)  { return e          ; }
    catch (const std::error_code &e) { return e.message(); }
    catch (...)                      { return "who knows"; }
}

template<typename ... Args>
void withIndex(int index, Args && ... args) {
    std::cout << index << ") ";
    ([&]() {
        std::cout << args;
    } (), ...);
    std::cout << std::endl;
}

int main()
{
    io_context io_ctx;
    asio::thread_pool pool(4);
    for (size_t i = 0; i < 4; i++)
    {
        asio::post(pool, [&, i]() {
            auto interval = std::chrono::seconds(1);
            withIndex(i, "thread: ", std::this_thread::get_id());
            co_spawn(io_ctx, [i, &io_ctx, interval]() -> awaitable<void> {
                steady_timer timer(io_ctx, interval);
                int times = 3;
                withIndex(i, "tick");
                while (times-- > 0)
                {
                    timer.expires_at(timer.expires_at() + interval);
                    co_await timer.async_wait(use_awaitable);
                    co_await cfgo::make_void_awaitable([i]() {
                        withIndex(i, "waiting 1 sec...");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        withIndex(i, "waited.");
                    }, asio::use_awaitable);
                    auto three = co_await cfgo::make_awaitable<int>([]() -> int {
                        return 3;
                    }, 0, asio::use_awaitable);
                    try
                    {
                        co_await cfgo::make_awaitable<int>([]() -> int {
                            throw std::make_error_code(std::errc::address_in_use);
                        }, 0, asio::use_awaitable);
                    }
                    catch(...)
                    {
                        withIndex(i, what());
                    }
                    
                }
            }, detached);
            io_ctx.run();
        });
    }

    pool.join();
}