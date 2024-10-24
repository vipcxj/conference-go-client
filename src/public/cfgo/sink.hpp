#ifndef _CFGO_SINK_HPP_
#define _CFGO_SINK_HPP_


#include "cfgo/allocate_tracer.hpp"
#include "rtc/rtc.hpp"

namespace cfgo
{
    using RTCTrackPtr = std::shared_ptr<rtc::Track>;
    using RTCTracks = std::vector<RTCTrackPtr>;

    class Sink
    {
    public:
        virtual ~Sink() = 0;
        virtual const RTCTracks & get_rtc_tracks() const = 0;
        virtual void start() = 0;
        virtual void close() = 0;
        virtual auto await() -> asio::awaitable<void> = 0;
    };
    using SinkUPtr = allocate_tracers::unique_ptr<Sink>;
} // namespace cfgo


#endif