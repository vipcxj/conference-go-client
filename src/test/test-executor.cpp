
#include "cfgo/executor.hpp"
#include "cfgo/asio.hpp"
#include "cfgo/async.hpp"

#include <iostream>
#include <chrono>

void proxy_fun(sr::executor_proxy_t::executor_execute_t fun)
{
    using namespace std::chrono;
    std::cout << "before fun" << std::endl;
    auto start = high_resolution_clock::now();
    fun();
    auto dur = high_resolution_clock::now() - start;
    std::cout << "after fun" << std::endl;
    std::cout << "use " << duration_cast<microseconds>(dur).count() * 1.0 / 1000 << " ms" << std::endl;
}

auto async_return_int(int v) -> asio::awaitable<int>
{
    std::cout << "async_return_int " << v << std::endl;
    co_return v;
}

int main()
{
    asio::io_context io_ctx;
    sr::wrappable_executor<asio::io_context::executor_type> executor (io_ctx.get_executor(), proxy_fun);
    asio::co_spawn(executor, []() -> asio::awaitable<void> {
        std::cout << "1" << std::endl;
        co_await async_return_int(0);
        std::cout << "2" << std::endl;
        co_await async_return_int(1);
        std::cout << "3" << std::endl;
        co_await async_return_int(2);
        std::cout << "4" << std::endl;
    }, asio::detached);
    io_ctx.run();
    return 0;
}