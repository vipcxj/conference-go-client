#ifndef _CFGO_GST_APPSINK_HPP_
#define _CFGO_GST_APPSINK_HPP_

#include "gst/app/gstappsink.h"
#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/gst/utils.hpp"

namespace cfgo
{
    namespace gst
    {
        namespace detail
        {
            class AppSink;
        } // namespace detail
        

        class AppSink : public ImplBy<detail::AppSink>
        {
        public:
            AppSink(GstAppSink * sink, int cache_capicity);
            void init();
            /**
             * throw CancelError when closer is closed. return null shared_ptr when eos and no sample available.
            */
            auto pull_sample(close_chan closer = INVALID_CLOSE_CHAN) -> asio::awaitable<GstSampleSPtr>;
        };
    } // namespace gst
    
} // namespace cfgo


#endif