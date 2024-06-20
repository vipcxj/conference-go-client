#ifndef _CFGO_IMP_WEBRTC_HPP_
#define _CFGO_IMP_WEBRTC_HPP_

#include "cfgo/utils.hpp"
#include "cfgo/async.hpp"
#include "cfgo/pattern.hpp"

namespace cfgo
{
    namespace impl
    {
        class Webrtc;
    } // namespace impl
    

    class Webrtc : ImplBy<impl::Webrtc>
    {
    public:
        [[nodiscard]] auto subscribe(close_chan closer, const Pattern & pattern, std::vector<std::string> && req_types) -> asio::awaitable<SubPtr>;
        [[nodiscard]] auto unsubscribe(close_chan closer, std::string_view sub_id) -> asio::awaitable<cancelable<void>>;
    };
    
} // namespace cfgo


#endif 