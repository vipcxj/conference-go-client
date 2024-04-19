#ifndef _CFGO_ALIAS_HPP_
#define _CFGO_ALIAS_HPP_

#include <memory>
#include "sio_message.h"
#include "asiochan/channel.hpp"

namespace cfgo
{
    using string = std::string;
    using msg_ptr = sio::message::ptr;
    using msg_chan = asiochan::channel<msg_ptr>;
    using msg_chan_ptr = std::shared_ptr<msg_chan>;
    using msg_chan_weak_ptr = std::weak_ptr<msg_chan>;

    struct Track;
    using TrackPtr = std::shared_ptr<Track>;
    struct Subscribation;
    using SubPtr = std::shared_ptr<Subscribation>;
    
} // namespace cfgo


#endif