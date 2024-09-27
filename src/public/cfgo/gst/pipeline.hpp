#ifndef _CFGO_GST_PIPELINE_HPP_
#define _CFGO_GST_PIPELINE_HPP_

#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/gst/utils.hpp"
#include "cfgo/gst/link.hpp"
#include "gst/gst.h"
#include <set>

namespace cfgo
{
    namespace gst
    {
        namespace impl
        {
            class Pipeline;
        } // namespace impl
        

        class Pipeline : public ImplBy<impl::Pipeline>
        {
        public:
            using CtxPtr = std::shared_ptr<asio::execution_context>;
        public:
            Pipeline(const std::string & name);
            void run();
            void stop();
            [[nodiscard]] auto await(close_chan close_ch = nullptr) -> asio::awaitable<bool>;
            void add_node(const std::string & name, const std::string & type);
            [[nodiscard]] auto await_pad(std::string node, std::string pad, std::set<GstPad *> excludes, close_chan closer = nullptr) -> asio::awaitable<GstPadSPtr>;
            bool link(const std::string & src, const std::string & target);
            bool link(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad);
            [[nodiscard]] auto link_async(const std::string & src, const std::string & target) -> AsyncLinkPtr;
            [[nodiscard]] auto link_async(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad) -> AsyncLinkPtr;
            [[nodiscard]] GstElementSPtr node(const std::string & name) const;
            [[nodiscard]] GstElementSPtr require_node(const std::string & name) const;
            [[nodiscard]] const char * name() const noexcept;
        };
    } // namespace gst
} // namespace cfgo



#endif