#ifndef _CFGO_IMPL_PIPELINE_HPP_
#define _CFGO_IMPL_PIPELINE_HPP_

#include <memory>
#include <unordered_map>
#include <list>
#include <set>
#include "gst/gst.h"
#include "cfgo/asio.hpp"
#include "cfgo/alias.hpp"
#include "cfgo/gst/link.hpp"
#include "cfgo/gst/utils.hpp"
#include "impl/link.hpp"

namespace cfgo
{
    namespace gst
    {
        namespace impl
        {
            class Pipeline : public std::enable_shared_from_this<Pipeline>
            {
            public:
                using Strand = StandardStrand;
                using NODE_MAP = std::unordered_map<std::string, GstElement *>;
                using NODE_HANDLER_MAP = std::unordered_map<std::string, gulong>;
                using LinkPtr = std::shared_ptr<Link>;
                using LinkList = std::list<LinkPtr>;
                using LinkMap = std::unordered_map<std::string, std::unordered_map<std::string, LinkPtr>>;
                using PadMap = std::unordered_map<std::string, std::unordered_map<std::string, GstPad *>>;
                using Waiter = asiochan::channel<GstPad *, 1>;
                using WaiterList = std::list<std::pair<std::string, Waiter>>;
                using WaiterMap = std::unordered_map<std::string, WaiterList>;
                enum EndpointType
                {
                    SRC,
                    TGT,
                    ANY
                };
            private:
                GstPipeline * m_pipeline;
                GstBus * m_bus;
                NODE_MAP m_nodes;
                NODE_HANDLER_MAP m_node_handlers;
                LinkMap m_links_by_src;
                LinkList m_pending_links;
                mutex m_mutex;
                asiochan::unbounded_channel<GstMessage *> m_msg_ch;
                PadMap m_pads;
                WaiterMap m_waiters;
                void _add_pad(GstElement *src, GstPad * pad);
                void _remove_pad(GstElement *src, GstPad * pad);
                GstPad * _find_pad(const std::string & node_name, const std::string & pad_template, const std::set<GstPad *> excludes);
                WaiterList::iterator _add_waiter(const std::string & node_name, const std::string & pad_name, Waiter waiter);
                void _remove_waiter(const std::string & node_name, const WaiterList::iterator & iter);
                [[nodiscard]] auto _find_pending_link(const std::string & node, const std::string & pad, EndpointType endpoint_type) const -> LinkList::const_iterator;
                [[nodiscard]] auto _find_pending_link(const std::string & node, const std::string & pad, EndpointType endpoint_type) -> LinkList::iterator;
                [[nodiscard]] LinkPtr _link_by_src(const std::string & node_name, const std::string & pad_name) const;
                [[nodiscard]] bool _add_link(const LinkPtr & link, bool clean_pending);
                bool _remove_link(const LinkPtr & link, bool clean_pending);
                [[nodiscard]] GstElement * _node(const std::string & name) const;
                [[nodiscard]] GstElement * _require_node(const std::string & name) const;
                void _release_node(GstElement * node, bool remove);
            public:
                Pipeline(const std::string & name);
                ~Pipeline();
                void add_node(const std::string & name, const std::string & type);
                void run();
                void stop();
                [[nodiscard]] auto await(close_chan & close_ch) -> asio::awaitable<bool>;
                [[nodiscard]] GstElement * node(const std::string & name);
                [[nodiscard]] GstElement * require_node(const std::string & name);
                [[nodiscard]] auto await_pad(const std::string & node, const std::string & pad, const std::set<GstPad *> & excludes, close_chan closer) -> asio::awaitable<GstPadSPtr>;
                bool link(const std::string & src, const std::string & target);
                bool link(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad);
                [[nodiscard]] auto link_async(const std::string & src, const std::string & target) -> AsyncLink::Ptr;
                [[nodiscard]] auto link_async(const std::string & src, const std::string & src_pad, const std::string & tgt, const std::string & tgt_pad) -> AsyncLink::Ptr;
                [[nodiscard]] inline const char * name() const noexcept
                {
                    return GST_ELEMENT_NAME(m_pipeline);
                }
 
                friend class AsyncLink;
                friend void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data);
                friend gboolean on_bus_message(GstBus *bus, GstMessage *message, gpointer user_data);
            };
        } // namespace impl
    } // namespace gst
} // namespace cfgo


#endif