#include "cfgo/gst/pipeline.hpp"
#include "impl/pipeline.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/allocate_tracer.hpp"
#include "cpptrace/cpptrace.hpp"

#include <set>

namespace cfgo
{
    namespace gst
    {
        Pipeline::Pipeline(const std::string & name): ImplBy(name) {}

        void Pipeline::run()
        {
            impl()->run();
        }

        void Pipeline::stop()
        {
            impl()->stop();
        }

        auto Pipeline::await(close_chan closer) -> asio::awaitable<bool>
        {
            return impl()->await(std::move(closer));
        }

        void Pipeline::add_node(const std::string & name, const std::string & type)
        {
            impl()->add_node(name, type);
        }

        GstElementSPtr Pipeline::node(const std::string & name) const
        {
            return make_shared_gst_element(impl()->node(name));
        }

        GstElementSPtr Pipeline::require_node(const std::string & name) const
        {
            return make_shared_gst_element(impl()->require_node(name));
        }

        auto Pipeline::await_pad(std::string node, std::string pad, std::set<GstPad *> excludes, close_chan closer) -> asio::awaitable<GstPadSPtr>
        {
            return impl()->await_pad(std::move(node), std::move(pad), std::move(excludes), std::move(closer));
        }

        bool Pipeline::link(const std::string & src, const std::string & target)
        {
            return impl()->link(src, target);
        }

        bool Pipeline::link(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad)
        {
            return impl()->link(src, src_pad, tgt, tgt_pad);
        }

        auto Pipeline::link_async(const std::string & src, const std::string & target) -> AsyncLinkPtr
        {
            auto link_impl = impl()->link_async(src, target);
            return link_impl ? allocate_tracers::make_shared<AsyncLink>(link_impl) : nullptr;
        }

        auto Pipeline::link_async(const std::string & src_name, const std::string & src_pad_name, const std::string & tgt_name, const std::string & tgt_pad_name) -> AsyncLinkPtr
        {
            auto link_impl = impl()->link_async(src_name, src_pad_name, tgt_name, tgt_pad_name);
            return link_impl ? allocate_tracers::make_shared<AsyncLink>(link_impl) : nullptr;
        }

        const char * Pipeline::name() const noexcept
        {
            return impl()->name();
        }
    } // namespace gst
} // namespace cfgo
