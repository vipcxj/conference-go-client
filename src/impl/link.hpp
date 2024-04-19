#ifndef _CFGO_GST_LINK_IMPL_HPP_
#define _CFGO_GST_LINK_IMPL_HPP_

#include "gst/gst.h"
#include <string>
#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"

namespace cfgo
{
    namespace gst
    {
        namespace impl
        {
            class Pipeline;
            
            class Link
            {
                GstElement * m_src;
                std::string m_src_pad_name;
                GstPad * m_src_pad = nullptr;
                GstElement * m_tgt;
                std::string m_tgt_pad_name;
                GstPad * m_tgt_pad = nullptr;
                asiochan::channel<bool, 1> m_linked_ch;
                std::mutex m_mutex;
                cfgo::AsyncMutex m_a_mutex;
                Pipeline * m_pipeline;

            public:
                Link(Pipeline * pipeline, GstElement * src, const std::string & src_pad_name, GstPad * src_pad, GstElement * tgt, const std::string & tgt_pad_name, GstPad * tgt_pad);
                ~Link();

                void set_src_pad(GstPad * src_pad);

                void set_tgt_pad(GstPad * tgt_pad);

                inline bool is_ready() const noexcept
                {
                    return m_src_pad != nullptr && m_tgt_pad != nullptr;
                }

                bool notify_linked(bool linked);

                auto wait_linked(cfgo::close_chan & close_ch = cfgo::INVALID_CLOSE_CHAN) -> asio::awaitable<bool>;

                inline GstElement * src() const noexcept
                {
                    return m_src;
                }

                inline const std::string src_name() const noexcept
                {
                    return m_src ? GST_ELEMENT_NAME(m_src) : "";
                }

                inline GstPad * src_pad() const noexcept
                {
                    return m_src_pad;
                }

                inline const std::string src_pad_name() const noexcept
                {
                    return m_src_pad_name;
                }

                inline GstElement * tgt() const noexcept
                {
                    return m_tgt;
                }

                inline const std::string tgt_name() const noexcept
                {
                    return m_tgt ? GST_ELEMENT_NAME(m_tgt) : "";
                }

                inline GstPad * tgt_pad() const noexcept
                {
                    return m_tgt_pad;
                }

                inline const std::string tgt_pad_name() const noexcept
                {
                    return m_tgt_pad_name;
                }
            };

            class AsyncLink
            {
            public:
                using Ptr = std::shared_ptr<AsyncLink>;
                using LinkPtr = std::shared_ptr<Link>;
            private:
                bool m_done;
                std::shared_ptr<Pipeline> m_pipeline;
                LinkPtr m_link;
            public:
                AsyncLink(std::shared_ptr<Pipeline> pipeline, LinkPtr link, bool done);
                auto await(close_chan closer) -> asio::awaitable<LinkPtr>;
                inline GstElement * src() const noexcept
                {
                    return m_link->src();
                }

                inline const std::string src_name() const noexcept
                {
                    return m_link->src_name();
                }

                inline GstElement * tgt() const noexcept
                {
                    return m_link->tgt();
                }

                inline const std::string tgt_name() const noexcept
                {
                    return m_link->tgt_name();
                }
            };
        } // namespace impl
    } // namespace gst
    
} // namespace cfgo


#endif