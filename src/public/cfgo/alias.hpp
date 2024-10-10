#ifndef _CFGO_ALIAS_HPP_
#define _CFGO_ALIAS_HPP_

#include <memory>
#include <mutex>
#include <chrono>
// #include "sio_message.h"
// #include "cfgo/asio.hpp"
#include "cfgo/macros.h"
#if CFGO_DEBUG_MODE
#include "yamc/checked_mutex.hpp"
#include "yamc/checked_shared_mutex.hpp"
#endif

namespace cfgo
{
    using string = std::string;
    // using msg_ptr = sio::message::ptr;
    // using msg_chan = asiochan::channel<msg_ptr>;
    // using msg_chan_ptr = std::shared_ptr<msg_chan>;
    // using msg_chan_weak_ptr = std::weak_ptr<msg_chan>;

    struct Track;
    using TrackPtr = std::shared_ptr<Track>;
    struct Subscribation;
    using SubPtr = std::shared_ptr<Subscribation>;

    using duration_t = std::chrono::steady_clock::duration;
    
    #if CFGO_DEBUG_MODE
    using mutex = yamc::checked::mutex;
    using timed_mutex = yamc::checked::timed_mutex;
    using recursive_mutex = yamc::checked::recursive_mutex;
    using recursive_timed_mutex = yamc::checked::recursive_timed_mutex;
    #else
    using mutex = std::mutex;
    using timed_mutex = std::timed_mutex;
    using recursive_mutex = std::recursive_mutex;
    using recursive_timed_mutex = std::recursive_timed_mutex;
    #endif
} // namespace cfgo


#endif