#ifndef _CFGO_SINK_HPP_
#define _CFGO_SINK_HPP_


#include "rtc/rtc.hpp"
#include <memory>
#include <vector>

namespace cfgo
{
    using RtcTrackPtr = std::shared_ptr<rtc::Track>;
    using RtcTrackWPtr = std::weak_ptr<rtc::Track>;
    using RtcTracks = std::vector<RtcTrackPtr>;
    class Sink
    {
    public:
        virtual ~Sink() = 0;
        virtual RtcTrackPtr create_track(rtc::PeerConnection & peer, const rtc::Description::Media & media) = 0;
        virtual bool start() = 0;
        virtual bool close() = 0;
        virtual auto await() -> asio::awaitable<void> = 0;
    };
    using SinkPtr = std::shared_ptr<Sink>;

    SinkPtr make_camera_sink(int device_id);

} // namespace cfgo


#endif