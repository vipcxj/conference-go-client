#include "cfgo/gst/link.hpp"
#include "impl/link.hpp"

namespace cfgo
{
    namespace gst
    {
        Link::Link(impl_ptr<impl::Link> impl):ImplBy(impl) {}

        GstElement * Link::src() const noexcept
        {
            return impl()->src();
        }

        const std::string Link::src_name() const noexcept
        {
            return impl()->src_name();
        }

        GstPad * Link::src_pad() const noexcept
        {
            return impl()->src_pad();
        }

        const std::string Link::src_pad_name() const noexcept
        {
            return impl()->src_pad_name();
        }

        GstElement * Link::tgt() const noexcept
        {
            return impl()->tgt();
        }

        const std::string Link::tgt_name() const noexcept
        {
            return impl()->tgt_name();
        }

        GstPad * Link::tgt_pad() const noexcept
        {
            return impl()->tgt_pad();
        }

        const std::string Link::tgt_pad_name() const noexcept
        {
            return impl()->tgt_pad_name();
        }

        AsyncLink::AsyncLink(impl_ptr<impl::AsyncLink> impl): ImplBy(impl) {}

        auto AsyncLink::await(close_chan closer) -> asio::awaitable<LinkPtr>
        {
            auto ptr = co_await impl()->await(std::move(closer));
            if (ptr)
            {
                co_return std::make_shared<Link>(ptr);
            }
            else
            {
                co_return nullptr;
            }
        }

        GstElement * AsyncLink::src() const noexcept
        {
            return impl()->src();
        }

        const std::string AsyncLink::src_name() const noexcept
        {
            return impl()->src_name();
        }

        GstElement * AsyncLink::tgt() const noexcept
        {
            return impl()->tgt();
        }

        const std::string AsyncLink::tgt_name() const noexcept
        {
            return impl()->tgt_name();
        }
    } // namespace gst
} // namespace cfgo
