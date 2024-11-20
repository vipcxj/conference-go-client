#ifndef _CFGO_PUBLICATION_HPP_
#define _CFGO_PUBLICATION_HPP_

#include "cfgo/message.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/allocate_tracer.hpp"
#include "cfgo/video/media_source.hpp"

#include "rtc/rtc.hpp"

namespace cfgo
{
    using PubMsgPtr = allocate_tracers::unique_ptr<msg::PublishAddMessage>;
    using Labels = std::unordered_map<std::string, std::string>;
    namespace impl
    {
        struct Publication;
    } // namespace impl
    
    class Publication : public ImplBy<impl::Publication>
    {
        
    private:
        /* data */
    public:
        Publication(video::media_source_ptr_t media_source, Labels labels);
        void setup(rtc::PeerConnection & peer) const;
        bool bind(const msg::Track & meta) const;
        bool ready() const noexcept;
        void start() const;
        PubMsgPtr create_publish_msg() const;
    };

} // namespace cfgo


#endif