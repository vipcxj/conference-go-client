#pragma once
#ifndef _CFGO_COEVENT_HPP_
#define _CFGO_COEVENT_HPP_

#include <chrono>
#include <mutex>
#include <atomic>
#include "asio.hpp"
#include "cfgo/move_only_function.hpp"

namespace cfgo {

    class CoEvent : public std::enable_shared_from_this<CoEvent>
    {
    public:
        using handler_type = asio::async_result<std::decay<decltype(asio::use_awaitable)>::type, void(std::exception_ptr)>::handler_type;
        using Ptr = std::shared_ptr<CoEvent>;
        using WeakPtr = std::weak_ptr<CoEvent>;
        using TimerPtr = std::unique_ptr<asio::steady_timer>;
    private:
        int m_times;
        std::mutex m_mutex;
        std::atomic_bool m_done;
        std::exception_ptr m_exception;
        cfgo::unique_function<void(std::exception_ptr)> m_handler;
        TimerPtr m_timer;
    protected:
        CoEvent(int times = 1) noexcept: m_times(times), m_exception(), m_handler(), m_timer(), m_mutex(), m_done(false) {}
        CoEvent(const CoEvent&) = delete;
        CoEvent& operator = (const CoEvent&) = delete;
    public:
        CoEvent(CoEvent &&) = default;
        CoEvent& operator = (CoEvent &&) = default;
        ~CoEvent() = default;
        inline static Ptr create(int times = 1) {
            return std::shared_ptr<CoEvent>(new CoEvent(times));
        }
        bool trigger();

        asio::awaitable<void> await(std::chrono::nanoseconds timeout = std::chrono::nanoseconds{0});
    };
    
    
}

#endif