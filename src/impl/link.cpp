#include "impl/pipeline.hpp"
#include "impl/link.hpp"
#include "cfgo/defer.hpp"
#include "cpptrace/cpptrace.hpp"

#include <cassert>

namespace cfgo
{
    namespace gst
    {
        namespace impl
        {
            Link::Link(
                Pipeline * pipeline,
                GstElement * src, const std::string & src_pad_name, GstPad * src_pad, 
                GstElement * tgt, const std::string & tgt_pad_name, GstPad * tgt_pad
            ):
            m_pipeline(pipeline),
            m_src(src), m_src_pad_name(src_pad_name), m_src_pad(src_pad),
            m_tgt(tgt), m_tgt_pad_name(tgt_pad_name), m_tgt_pad(tgt_pad)
            {
                assert(m_src);
                assert(m_tgt);
                gst_object_ref(m_src);
                gst_object_ref(m_tgt);
            }

            Link::~Link()
            {
                if (m_src_pad && m_tgt_pad)
                {
                    gst_pad_unlink(m_src_pad, m_tgt_pad);
                }
                if (m_src_pad)
                {
                    if (auto temp = GST_PAD_PAD_TEMPLATE (m_src_pad); temp && GST_PAD_TEMPLATE_PRESENCE (temp) == GST_PAD_REQUEST)
                    {
                        gst_element_release_request_pad(m_src, m_src_pad);
                    }
                    gst_object_unref(m_src_pad);
                    m_src_pad = nullptr;
                }
                gst_object_unref(m_src);
                m_src = nullptr;
                if (m_tgt_pad)
                {
                    if (auto temp = GST_PAD_PAD_TEMPLATE (m_tgt_pad); temp && GST_PAD_TEMPLATE_PRESENCE (temp) == GST_PAD_REQUEST)
                    {
                        gst_element_release_request_pad(m_tgt, m_tgt_pad);
                    }
                    gst_object_unref(m_tgt_pad);
                    m_tgt_pad = nullptr;
                }
                gst_object_unref(m_tgt);
                m_tgt = nullptr;
            }

            void Link::set_src_pad(GstPad * src_pad)
            {
                if (src_pad == nullptr)
                {
                    throw cpptrace::invalid_argument("Unable to set src pad to nullptr.");
                }
                
                std::lock_guard lock(this->m_mutex);
                if (this->m_src_pad != nullptr)
                {
                    if (this->m_src_pad != src_pad)
                        throw cpptrace::invalid_argument("Unable to set src pad, it has been set.");
                    else
                        return;
                }
                else
                {
                    g_object_ref_sink(src_pad);
                    this->m_src_pad = src_pad;
                }
            }

            void Link::set_tgt_pad(GstPad * tgt_pad)
            {
                if (tgt_pad == nullptr)
                {
                    throw cpptrace::invalid_argument("Unable to set tgt pad to nullptr.");
                }
                
                std::lock_guard lock(this->m_mutex);
                if (this->m_tgt_pad != nullptr)
                {
                    if (this->m_tgt_pad != tgt_pad)
                        throw cpptrace::invalid_argument("Unable to set tgt pad, it has been set.");
                    else
                        return;
                }
                else
                {
                    g_object_ref_sink(tgt_pad);
                    this->m_tgt_pad = tgt_pad;
                }
            }

            bool Link::notify_linked(bool linked)
            {
                return m_linked_ch.try_write(linked);
            }

            auto Link::wait_linked(cfgo::close_chan & close_ch) -> asio::awaitable<bool>
            {
                if (co_await m_a_mutex.accquire(close_ch))
                {
                    auto executor = co_await asio::this_coro::executor;
                    DEFER({
                        m_a_mutex.release(executor);
                    });
                    if (is_ready())
                    {
                        co_return true;
                    }
                    auto res = co_await cfgo::chan_read<bool>(m_linked_ch, close_ch);
                    if (res)
                    {
                        co_return res.value();
                    }
                    else
                    {
                        co_return false;
                    }
                }
                else
                    co_return false;
            }

            AsyncLink::AsyncLink(std::shared_ptr<Pipeline> pipeline, LinkPtr link, bool done): 
                m_pipeline(pipeline), m_link(link), m_done(done)
            {}

            auto AsyncLink::await(close_chan closer) -> asio::awaitable<LinkPtr>
            {
               if (m_done)
                {
                    co_return m_link;
                }
                auto res = co_await m_link->wait_linked(closer);
                if (!res)
                {
                    {
                        std::lock_guard lock(m_pipeline->m_mutex);
                        m_pipeline->_remove_link(m_link, true);
                    }
                    co_return nullptr;
                }
                co_return m_link;
            }
        } // namespace impl
    
    } // namespace gst
} // namespace cfgo
