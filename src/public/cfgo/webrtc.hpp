#ifndef _CFGO_WEBRTC_HPP_
#define _CFGO_WEBRTC_HPP_

#include "cfgo/utils.hpp"
#include "cfgo/async.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/subscribation.hpp"
#include "cfgo/signal.hpp"
#include "cfgo/config/configuration.h"

#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

namespace cfgo
{
    namespace impl
    {
        class Webrtc;
    } // namespace impl

    class Webrtc
    {
    public:
        virtual ~Webrtc() = 0;
        virtual auto subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr> = 0;
        virtual auto unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void> = 0;
        virtual close_chan get_notify_closer() = 0;
        virtual void close();
    };
    using WebrtcPtr = std::shared_ptr<Webrtc>;
    using WebrtcWPtr = std::weak_ptr<Webrtc>;
    using WebrtcUPtr = std::unique_ptr<Webrtc>;

    WebrtcPtr make_webrtc(close_chan closer, SignalPtr signal, const cfgo::Configuration & conf);
    
} // namespace cfgo


#endif 