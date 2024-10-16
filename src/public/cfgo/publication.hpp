#ifndef _CFGO_PUBLICATION_HPP_
#define _CFGO_PUBLICATION_HPP_

#include "rtc/rtc.hpp"
#include "cfgo/message.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/allocate_tracer.hpp"

namespace cfgo
{
    using RTCTrackPtr = std::shared_ptr<rtc::Track>;
    using RTCTracks = std::vector<RTCTrackPtr>;
    using PubMsgPtr = allocate_tracers::unique_ptr<msg::PublishAddMessage>;
    using Labels = std::unordered_map<std::string, std::string>;
    class Sink
    {
    public:
        virtual ~Sink() = 0;
        virtual RTCTracks get_rtc_tracks() = 0;
        virtual void start() = 0;
        virtual void close() = 0;
        virtual auto await() -> asio::awaitable<void> = 0;
    };
    using SinkUPtr = allocate_tracers::unique_ptr<Sink>;

    namespace impl
    {
        struct Publication;
    } // namespace impl
    
    class Publication : public ImplBy<impl::Publication>
    {
        
    private:
        /* data */
    public:
        Publication(SinkUPtr sink, Labels labels);
        bool bind(const msg::Track & track) const;
        bool ready() const;
        PubMsgPtr create_publish_msg() const;
        Sink & sink() const;
    };

} // namespace cfgo


#endif