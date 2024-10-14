#include "impl/pipeline.hpp"
#include "cpptrace/cpptrace.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/log.hpp"
#include "cfgo/gst/helper.h"
#include "cfgo/allocate_tracer.hpp"
#include <cctype>

namespace cfgo
{
    namespace gst
    {

        namespace impl
        {

            /**
             * only support %u
            */
            bool match_pad_name(const std::string & name1, const std::string & name2)
            {

                auto len1 = name1.length();
                auto len2 = name2.length();
                int f1 = 0, f2 = 0;
                size_t i = 0, j = 0;
                for (;i < len1 && j < len2;)
                {
                    auto c1 = name1[i];
                    auto c2 = name2[j];
                    if (f1 > 0)
                    {
                        if (f1 == 1)
                        {
                            if (c1 != 'u')
                            {
                                CFGO_WARN("Unsupport format. pad1: {}, pad2: {}.", name1, name2);
                                return false;
                            }
                            else
                            {
                                f1 = 2;
                            }
                        }
                        if (!std::isdigit(c2))
                        {
                            f1 = 0;
                            ++i;
                        }
                        else
                        {
                            ++j;
                        }
                    }
                    else if (f2 > 0)
                    {
                        if (f2 == 1)
                        {
                            if (c2 != 'u')
                            {
                                CFGO_WARN("Unsupport format. pad1: {}, pad2: {}.", name1, name2);
                                return false;
                            }
                            else
                            {
                                f2 = 2;
                            }
                        }
                        if (!std::isdigit(c1))
                        {
                            f2 = 0;
                            ++j;
                        }
                        else
                        {
                            ++i;
                        }
                    }
                    else
                    {
                        if (c1 != c2)
                        {
                            if (c1 == '%')
                            {
                                if (!std::isdigit(c2))
                                {
                                    return false;
                                }
                                f1 = 1;
                            }
                            else if (c2 == '%')
                            {
                                if (!std::isdigit(c1))
                                {
                                    return false;
                                }
                                f2 = 1;
                            }
                            else
                            {
                                return false;
                            }
                        }
                        ++i;
                        ++j;
                    }
                }
                return (i == len1 && j == len2) || f1 > 0 || f2 > 0;
            }

            void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data)
            {
                std::string node_name = GST_ELEMENT_NAME(src);
                std::string pad_name = GST_PAD_NAME(new_pad);
                if (auto pipeline = cast_weak_holder<Pipeline>(user_data)->lock())
                {
                    {
                        std::lock_guard lock(pipeline->m_mutex);
                        pipeline->_add_pad(src, new_pad);
                    }
                    {
                        std::lock_guard lock(pipeline->m_mutex);
                        auto waiters_iter = pipeline->m_waiters.find(node_name);
                        if (waiters_iter != pipeline->m_waiters.end())
                        {
                            for (auto && waiter_pair : waiters_iter->second)
                            {
                                if (match_pad_name(pad_name, waiter_pair.first))
                                {
                                    std::ignore = waiter_pair.second.try_write(new_pad);
                                }
                            }
                        }
                    }
                    {
                        std::lock_guard lock(pipeline->m_mutex);

                        auto link_iter = pipeline->_find_pending_link(node_name, pad_name, Pipeline::ANY);
                        if (link_iter != pipeline->m_pending_links.end())
                        {
                            auto link = *link_iter;
                            if (src == link->src())
                            {
                                link->set_src_pad(new_pad);
                            }
                            else
                            {
                                link->set_tgt_pad(new_pad);
                            }
                            if (link->is_ready())
                            {
                                bool linked = pipeline->_add_link(link, true);
                                link->notify_linked(linked);
                            }
                        }
                    }
                }
            }

            gboolean on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data) 
            {   
                if (auto pipeline = cast_weak_holder<Pipeline>(user_data)->lock())
                {
                    gst_message_ref(message);
                    pipeline->m_msg_ch.write(message);
                }
                return TRUE;
            }

            Pipeline::Pipeline(const std::string & name)
            {
                
                m_pipeline = GST_PIPELINE (gst_pipeline_new(name.c_str()));
                if (!m_pipeline)
                {
                    throw cpptrace::runtime_error("Unable to create the pipeline with name " + name + ".");
                }
                m_bus = gst_element_get_bus(GST_ELEMENT (m_pipeline));
                if (!m_bus)
                {
                    throw cpptrace::runtime_error("Unable to get the bus from the pipeline " + name + ".");
                }
            }

            Pipeline::~Pipeline()
            {
                stop();
                for (auto && [node_name, node] : m_nodes)
                {
                    _release_node(node, false);
                }
                gst_bus_remove_watch(m_bus);
                while (auto msg = m_msg_ch.try_read())
                {
                    gst_message_unref(*msg);
                }
                gst_object_unref(m_bus);
                for (auto && [node_name, pads] : m_pads)
                {
                    auto node = _require_node(node_name);
                    for (auto && [pad_name, pad] : pads)
                    {
                        cfgo_gst_release_pad(pad, node);
                    }
                }
                gst_object_unref(m_pipeline);
            }

            void Pipeline::run()
            {
                gst_bus_add_watch_full(m_bus, G_PRIORITY_DEFAULT, on_bus_message, make_weak_holder(weak_from_this()), destroy_weak_holder<Pipeline>);
                auto ret = gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PLAYING);
                if (ret == GST_STATE_CHANGE_FAILURE)
                {
                    throw cpptrace::runtime_error("Unable to set the pipeline " + std::string(name()) + " to the playing state.");
                }
            }

            void Pipeline::stop()
            {
                gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_NULL);
            }

            auto Pipeline::await(close_chan closer) -> asio::awaitable<bool>
            {
                do
                {
                    auto c_msg = co_await chan_read<GstMessage *>(m_msg_ch, closer);
                    if (!c_msg)
                    {
                        co_return false;
                    }
                    auto msg = c_msg.value();
                    DEFER({
                        gst_message_unref(c_msg.value());
                    });
                    switch (GST_MESSAGE_TYPE (msg))
                    {
                    case GST_MESSAGE_ERROR:
                    {
                        GError * err;
                        gchar * debug_info;
                        gst_message_parse_error(msg, &err, &debug_info);
                        DEFER({
                            g_clear_error(&err);
                            g_free(debug_info);
                        });
                        throw cpptrace::runtime_error(
                            std::string("Error received from element ")
                            + GST_OBJECT_NAME (msg->src) 
                            + ": " + err->message 
                            + " --- Debug info: " + (debug_info ? debug_info : "none")
                        );
                    }
                    case GST_MESSAGE_EOS:
                        CFGO_DEBUG("The pipeline accept eos message.");
                        co_return true;
                    }
                } while (true);
            }

            void Pipeline::add_node(const std::string & name, const std::string & type)
            {
                auto element = gst_element_factory_make(type.c_str(), name.c_str());
                if (!element)
                {
                    throw cpptrace::runtime_error("Unable to create the " + type + " element with name " + name + ".");
                }
                if (!gst_bin_add(GST_BIN (m_pipeline), element))
                {
                    throw cpptrace::runtime_error("Unable add the " + type + " element with name " + name + " to the pipeline " + this->name() + ".");
                }
                gst_element_sync_state_with_parent(element);
                gulong handler = 0;
                if (g_signal_lookup("pad-added", G_OBJECT_TYPE (element)))
                {
                    handler = g_signal_connect_data(element, "pad-added", G_CALLBACK(pad_added_handler), make_weak_holder(weak_from_this()), [](gpointer data, GClosure * closure) {
                        destroy_weak_holder<Pipeline>(data);
                    }, G_CONNECT_DEFAULT);
                }
                std::lock_guard lock(m_mutex);
                if (m_nodes.contains(name))
                {
                    throw cpptrace::runtime_error("The node " + name + " (" + type + ") has already exist in the pipeline " + this->name() + ".");
                }
                m_nodes.emplace(std::make_pair(name, element));
                if (handler)
                {
                    m_node_handlers.insert(std::make_pair(name, handler));
                }
            }

            void Pipeline::_release_node(GstElement * node, bool remove)
            {
                std::lock_guard lock(m_mutex);
                auto iter = m_node_handlers.find(GST_ELEMENT_NAME(node));
                if (iter != m_node_handlers.end())
                {
                    g_signal_handler_disconnect(node, iter->second);
                    m_node_handlers.erase(iter);
                }
                if (remove)
                {
                    auto iter = m_nodes.find(GST_ELEMENT_NAME(node));
                    if (iter != m_nodes.end())
                    {
                        m_nodes.erase(iter);
                    }
                    gst_bin_remove(GST_BIN(m_pipeline), node);
                }
            }

            void Pipeline::_add_pad(GstElement *src, GstPad * pad)
            {
                if (!src)
                {
                    src = gst_pad_get_parent_element(pad);
                }
                auto [pads_iter, _] = m_pads.try_emplace(GST_ELEMENT_NAME(src));
                auto pad_iter = pads_iter->second.find(GST_PAD_NAME(pad));
                gst_object_ref_sink(pad);
                if (pad_iter != pads_iter->second.end())
                {
                    gst_object_unref(pad_iter->second);
                    pad_iter->second = pad;
                }
                else
                {
                    pads_iter->second.insert(std::make_pair(GST_PAD_NAME(pad), pad));
                }
            }

            void Pipeline::_remove_pad(GstElement *src, GstPad * pad)
            {
                if (!src)
                {
                    src = gst_pad_get_parent_element(pad);
                }
                auto pads_iter = m_pads.find(GST_ELEMENT_NAME(src));
                if (pads_iter == m_pads.end())
                {
                    return;
                }
                auto pad_iter = pads_iter->second.find(GST_PAD_NAME(pad));
                if (pad_iter == pads_iter->second.end())
                {
                    return;
                }
                gst_object_unref(pad_iter->second);
                pads_iter->second.erase(pad_iter);
            }

            GstPad * Pipeline::_find_pad(const std::string & node_name, const std::string & pad_template, const std::set<GstPad *> excludes)
            {
                auto pads_iter = m_pads.find(node_name);
                if (pads_iter == m_pads.end())
                {
                    return nullptr;
                }
                for (auto && [pad_name, pad] : pads_iter->second)
                {
                    if (match_pad_name(pad_name, pad_template) && !excludes.contains(pad))
                    {
                        return pad;
                    }
                }
                return nullptr;
            }

            auto Pipeline::_add_waiter(const std::string & node_name, const std::string & pad_name, Waiter waiter) -> WaiterList::iterator
            {
                auto [waiters_iter, _] = m_waiters.try_emplace(node_name);
                return waiters_iter->second.insert(waiters_iter->second.end(), std::make_pair(pad_name, waiter));
            }

            void Pipeline::_remove_waiter(const std::string & node_name, const WaiterList::iterator & iter)
            {
                auto waiters_iter = m_waiters.find(node_name);
                if (waiters_iter != m_waiters.end())
                {
                    waiters_iter->second.erase(iter);
                }
            }

            [[nodiscard]] auto Pipeline::_find_pending_link(const std::string & node, const std::string & pad, EndpointType endpoint_type) const -> LinkList::const_iterator
            {
                return std::find_if(m_pending_links.begin(), m_pending_links.end(), [endpoint_type, &node, &pad](auto && link) {
                    if (endpoint_type == Pipeline::SRC)
                        return node == GST_ELEMENT_NAME (link->src()) && match_pad_name(pad, link->src_pad_name());
                    else if (endpoint_type == Pipeline::TGT)
                        return node == GST_ELEMENT_NAME (link->tgt()) && match_pad_name(pad, link->tgt_pad_name());
                    else
                        return (node == GST_ELEMENT_NAME (link->src()) && match_pad_name(pad, link->src_pad_name())) || (node == GST_ELEMENT_NAME (link->tgt()) && match_pad_name(pad, link->tgt_pad_name()));     
                });
            }
            [[nodiscard]] auto Pipeline::_find_pending_link(const std::string & node, const std::string & pad, EndpointType endpoint_type) -> LinkList::iterator
            {
                return std::find_if(m_pending_links.begin(), m_pending_links.end(), [endpoint_type, &node, &pad](auto && link) {
                    if (endpoint_type == Pipeline::SRC)
                        return node == GST_ELEMENT_NAME (link->src()) && match_pad_name(pad, link->src_pad_name());
                    else if (endpoint_type == Pipeline::TGT)
                        return node == GST_ELEMENT_NAME (link->tgt()) && match_pad_name(pad, link->tgt_pad_name());
                    else
                        return (node == GST_ELEMENT_NAME (link->src()) && match_pad_name(pad, link->src_pad_name())) || (node == GST_ELEMENT_NAME (link->tgt()) && match_pad_name(pad, link->tgt_pad_name()));     
                });
            }
            [[nodiscard]] Pipeline::LinkPtr Pipeline::_link_by_src(const std::string & node_name, const std::string & pad_name) const
            {
                auto iter0 = m_links_by_src.find(node_name);
                if (iter0 != m_links_by_src.end())
                {
                    auto iter1 = iter0->second.find(pad_name);
                    if (iter1 != iter0->second.end())
                    {
                        return iter1->second;
                    }
                }
                auto iter = _find_pending_link(node_name, pad_name, Pipeline::SRC);
                return iter != m_pending_links.end() ? *iter : nullptr;
            }

            #define CFGO_UNABLE_TO_LINK_MSG(SRC, SRC_PAD, TGT, TGT_PAD, MSG) \
                "Unable to link from pad " + SRC_PAD + " of " + SRC + " to pad " + TGT_PAD + " of " + TGT + ". " + MSG

            bool Pipeline::_add_link(const LinkPtr & link, bool clean_pending)
            {
                if (!link->is_ready())
                {
                    throw cpptrace::logic_error(THIS_IS_IMPOSSIBLE);
                }
                std::string src_name = GST_ELEMENT_NAME (link->src());
                std::string src_pad_name = link->src_pad_name();

                if (GST_PAD_LINK_FAILED (gst_pad_link(link->src_pad(), link->tgt_pad())))
                {
                    _remove_link(link, false);
                    return false;
                }
                if (clean_pending)
                {
                    auto iter = _find_pending_link(src_name, src_pad_name, Pipeline::SRC);
                    if (iter != m_pending_links.end())
                    {
                        m_pending_links.erase(iter);
                    }
                }
                if (auto iter = m_links_by_src.find(src_name); iter == m_links_by_src.end())
                {
                    m_links_by_src[src_name] = std::unordered_map<std::string, LinkPtr>();
                }
                m_links_by_src[src_name][src_pad_name] = link;
                return true;
            }

            bool Pipeline::_remove_link(const LinkPtr & link, bool clean_pending)
            {
                std::string src_name = GST_ELEMENT_NAME (link->src());
                std::string src_pad_name = link->src_pad_name();
                if (clean_pending)
                {
                    auto iter = _find_pending_link(src_name, src_pad_name, Pipeline::SRC);
                    if (iter != m_pending_links.end())
                    {
                        m_pending_links.erase(iter);
                        return true;
                    }
                }
                auto iter1 = m_links_by_src.find(src_name);
                if (iter1 == m_links_by_src.end())
                {
                    return false;
                }
                auto iter2 = iter1->second.find(src_pad_name);
                if (iter2 == iter1->second.end())
                {
                    return false;
                }
                iter1->second.erase(iter2);
                if (iter1->second.empty())
                {
                    m_links_by_src.erase(iter1);
                }
                return true;
            }

            auto Pipeline::await_pad(std::string node_name, std::string pad_name, std::set<GstPad *> excludes, close_chan closer) -> asio::awaitable<GstPadSPtr>
            {
                Waiter waiter{};
                GstPad * pad = nullptr;
                {
                    {
                        std::lock_guard lock(m_mutex);
                        pad = _find_pad(node_name, pad_name, excludes);
                    }
                    if (!pad)
                    {
                        auto node = this->require_node(node_name);
                        pad = gst_element_get_static_pad(node, pad_name.c_str());
                        if (pad && !excludes.contains(pad))
                        {
                            std::lock_guard lock(m_mutex);
                            auto [pads_iter, _] = m_pads.try_emplace(node_name);
                            pads_iter->second.insert(std::make_pair(GST_PAD_NAME(pad), pad));
                        }
                        else if (pad)
                        {
                            gst_object_unref(pad);
                            co_return nullptr;
                        }
                        else
                        {
                            pad = gst_element_request_pad_simple(node, pad_name.c_str());
                            if (pad && !excludes.contains(pad))
                            {
                                std::lock_guard lock(m_mutex);
                                auto [pads_iter, _] = m_pads.try_emplace(node_name);
                                pads_iter->second.insert(std::make_pair(GST_PAD_NAME(pad), pad));
                            }
                            else if (pad)
                            {
                                gst_object_unref(pad);
                                gst_element_release_request_pad(node, pad);
                                co_return nullptr;
                            }
                            else
                            {
                                std::lock_guard lock(m_mutex);
                                auto [waiters_iter, _] = m_waiters.try_emplace(node_name);
                                waiters_iter->second.push_back(std::make_pair(pad_name, waiter));
                            }
                        }
                    }
                }
                if (pad)
                {
                    co_return make_shared_gst_pad(pad);
                }
                auto c_pad = co_await chan_read<GstPad *>(waiter, closer);
                if (c_pad)
                {
                    co_return make_shared_gst_pad(c_pad.value());
                }
                else
                {
                    co_return nullptr;
                }
            }

            bool Pipeline::link(const std::string & src, const std::string & target)
            {
                throw cpptrace::runtime_error(NOT_IMPLEMENTED_YET);
            }

            bool Pipeline::link(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad)
            {
                throw cpptrace::runtime_error(NOT_IMPLEMENTED_YET);
            }

            auto Pipeline::link_async(const std::string & src, const std::string & target) -> AsyncLink::Ptr
            {
                throw cpptrace::runtime_error(NOT_IMPLEMENTED_YET);
            }

            auto Pipeline::link_async(const std::string & src_name, const std::string & src_pad_name, const std::string & tgt_name, const std::string & tgt_pad_name) -> AsyncLink::Ptr
            {
                bool done = false;
                LinkPtr _link = nullptr;
                {
                    {
                        std::lock_guard lock(m_mutex);
                        if (_link_by_src(src_name, src_pad_name))
                        {
                            throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "Link already exist."
                            ));
                        }
                    }
                    
                    auto src_node = this->require_node(src_name);
                    GstPad * src_pad = gst_element_get_static_pad(src_node, src_pad_name.c_str());
                    if (!src_pad)
                    {
                        auto pad_template = gst_element_get_pad_template(src_node, src_pad_name.c_str());
                        if (!pad_template)
                        {
                            throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "No pad " + src_pad_name + " found on node " + src_name + "."
                            ));
                        }
                        if (GST_PAD_TEMPLATE_DIRECTION (pad_template) != GST_PAD_SRC)
                        {
                            throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "The pad " + src_pad_name + " of " + src_name + " is not a src pad."
                            ));
                        }
                        switch (GST_PAD_TEMPLATE_PRESENCE (pad_template))
                        {
                        case GST_PAD_REQUEST:
                            src_pad = gst_element_request_pad_simple(src_node, src_pad_name.c_str());
                            if (!src_pad)
                            {
                                throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                    src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                    "Unable to request the pad " + src_pad_name + " from " + src_name + "."
                                ));
                            }
                            break;
                        case GST_PAD_SOMETIMES:
                            break;
                        case GST_PAD_ALWAYS:
                            throw cpptrace::runtime_error(cfgo::THIS_IS_IMPOSSIBLE);
                        default:
                            throw cpptrace::logic_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "Unknown pad presence value: " + std::to_string((int) GST_PAD_TEMPLATE_PRESENCE (pad_template))
                            ));
                        }
                    }
                    auto tgt_node = this->require_node(tgt_name);
                    GstPad * tgt_pad = gst_element_get_static_pad(tgt_node, tgt_pad_name.c_str());
                    if (!tgt_pad)
                    {
                        auto pad_template = gst_element_get_pad_template(tgt_node, tgt_pad_name.c_str());
                        if (!pad_template)
                        {
                            throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "No pad " + tgt_pad_name + " found on node " + tgt_name + "."
                            ));
                        }
                        if (GST_PAD_TEMPLATE_DIRECTION (pad_template) != GST_PAD_SINK)
                        {
                            throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "The pad " + tgt_pad_name + " of " + tgt_name + " is not a sink pad."
                            ));
                        }
                        switch (GST_PAD_TEMPLATE_PRESENCE (pad_template))
                        {
                        case GST_PAD_REQUEST:
                            tgt_pad = gst_element_request_pad_simple(tgt_node, tgt_pad_name.c_str());
                            if (!tgt_pad)
                            {
                                throw cpptrace::runtime_error(CFGO_UNABLE_TO_LINK_MSG(
                                    src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                    "Unable to request the pad " + tgt_pad_name + " from " + tgt_name + "."
                                ));
                            }
                            break;
                        case GST_PAD_SOMETIMES:
                            break;
                        case GST_PAD_ALWAYS:
                            throw cpptrace::runtime_error(cfgo::THIS_IS_IMPOSSIBLE);
                        default:
                            throw cpptrace::logic_error(CFGO_UNABLE_TO_LINK_MSG(
                                src_name, src_pad_name, tgt_name, tgt_pad_name, 
                                "Unknown pad presence value: " + std::to_string((int) GST_PAD_TEMPLATE_PRESENCE (pad_template))
                            ));
                        }
                    }
                    _link = allocate_tracers::make_shared<impl::Link>(
                        this,
                        src_node, src_pad_name, src_pad,
                        tgt_node, tgt_pad_name, tgt_pad
                    );
                    done = _link->is_ready();
                    if (done)
                    {
                        std::lock_guard lock(m_mutex);
                        if (!_add_link(_link, false))
                        {
                            return nullptr;
                        }
                    }
                    else
                    {
                        std::lock_guard lock(m_mutex);
                        m_pending_links.push_back(_link);
                    }
                }
                return AsyncLink::Ptr(new AsyncLink(shared_from_this(), _link, done));
            }

            GstElement * Pipeline::_node(const std::string & name) const
            {
                auto && iter = m_nodes.find(name);
                return iter != m_nodes.end() ? iter->second : nullptr;
            }

            GstElement * Pipeline::_require_node(const std::string & name) const
            {
                auto node = this->_node(name);
                if (!node)
                {
                    throw cpptrace::runtime_error("Unable to find the node " + name + ".");
                }
                return node;
            }

            GstElement * Pipeline::node(const std::string & name)
            {
                std::lock_guard lock(m_mutex);
                return _node(name);
            }

            GstElement * Pipeline::require_node(const std::string & name)
            {
                std::lock_guard lock(m_mutex);
                return _require_node(name);
            }
        } // namespace impl
  
    } // namespace gst  
} // namespace cfgo
