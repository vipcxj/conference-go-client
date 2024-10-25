#ifndef _CFGO_PUBLICATION_HPP_
#define _CFGO_PUBLICATION_HPP_

#include "cfgo/message.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/allocate_tracer.hpp"
#include "cfgo/sink.hpp"

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
        Publication(Labels labels);
        void add_track(RtcTrackPtr track) const;
        bool bind(const msg::Track & track) const;
        bool ready() const noexcept;
        PubMsgPtr create_publish_msg() const;
    };

} // namespace cfgo


#endif