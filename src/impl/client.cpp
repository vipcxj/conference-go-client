#include <assert.h>
#include <exception>
#include <cstdint>
#include "cfgo/track.hpp"
#include "cfgo/subscribation.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/async.hpp"
#include "cfgo/spd_helper.hpp"
#include "cfgo/rtc_helper.hpp"
#include "impl/client.hpp"
#include "impl/track.hpp"
#include "cpptrace/cpptrace.hpp"
#include "rtc/rtc.hpp"
#include "spdlog/spdlog.h"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "asiochan/asiochan.hpp"
#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

namespace cfgo
{
    namespace impl
    {

        Client::Client(const Configuration &config, close_chan closer) : Client(config, std::make_shared<asio::io_context>(), closer, true)
        {
        }

        Client::Client(const Configuration &config, const CtxPtr &io_ctx, close_chan closer) : Client(config, io_ctx, closer, config.m_thread_safe)
        {
        }

        Client::Client(
            const Configuration &config, const CtxPtr &io_ctx, close_chan closer, bool thread_safe
        ) : m_logger(cfgo::Log::instance().create_logger(cfgo::Log::CLIENT)),
            m_config(config),
            m_client(std::make_unique<sio::client>()),
            m_closer(closer),
            m_peer(std::make_shared<::rtc::PeerConnection>(config.m_rtc_config)),
            m_id(boost::uuids::to_string(boost::uuids::random_generator()())),
            m_io_context(io_ctx),
            m_thread_safe(thread_safe),
            m_mutex()
            #ifdef CFGO_SUPPORT_GSTREAMER
            , m_gst_sdp(nullptr)
            #endif
        {
            m_client->set_reconnect_attempts(0);
        }

        Client::~Client()
        {
            #ifdef CFGO_SUPPORT_GSTREAMER
            if (m_gst_sdp)
            {
                gst_sdp_message_free(m_gst_sdp);
            }
            #endif
            
            m_client->sync_close();
        }

        void Client::set_sio_logs_default() {
            m_client->set_logs_default();
        }

        void Client::set_sio_logs_verbose() {
            m_client->set_logs_verbose();
        }

        void Client::set_sio_logs_quiet() {
            m_client->set_logs_quiet();
        }

        Client::CtxPtr Client::execution_context() const noexcept
        {
            return m_io_context;
        }

        close_chan Client::get_closer() const noexcept
        {
            return m_closer;
        }

        void Client::lock()
        {
            if (!m_thread_safe)
            {
                m_mutex.lock();
            }
        }

        void Client::unlock() noexcept
        {
            if (!m_thread_safe)
            {
                m_mutex.unlock();
            }
        }

        msg_ptr create_setup_message()
        {
            auto setup_msg = sio::object_message::create();
            return setup_msg;
        }

        msg_ptr Client::create_auth_message() const
        {
            auto auth_msg = sio::object_message::create();
            auth_msg->get_map()["token"] = sio::string_message::create(m_config.m_token);
            auth_msg->get_map()["id"] = sio::string_message::create(m_id);
            return auth_msg;
        }

        msg_ptr create_add_cand_message(const ::rtc::Candidate &cand)
        {
            auto add_cand_msg = sio::object_message::create();
            add_cand_msg->get_map()["op"] = sio::string_message::create("add");
            auto cand_msg = sio::object_message::create();
            cand_msg->get_map()["candidate"] = sio::string_message::create(cand.candidate());
            cand_msg->get_map()["sdpMid"] = sio::string_message::create(cand.mid());
            add_cand_msg->get_map()["candidate"] = cand_msg;
            return add_cand_msg;
        }

        msg_ptr create_subscribe_message(const Pattern &pattern, const std::vector<std::string> &req_types)
        {
            auto msg = sio::object_message::create();
            msg->get_map()["op"] = sio::int_message::create(0);
            auto req_types_msg = sio::array_message::create();
            for (auto &&req_type : req_types)
            {
                req_types_msg->get_vector().push_back(sio::string_message::create(req_type));
            }
            msg->get_map()["reqTypes"] = req_types_msg;
            msg->get_map()["pattern"] = pattern.create_message();
            return msg;
        }

        msg_ptr create_unsubscribe_message(const std::string &sub_id)
        {
            auto msg = sio::object_message::create();
            msg->get_map()["op"] = sio::int_message::create(2);
            msg->get_map()["id"] = sio::string_message::create(sub_id);
            return msg;
        }

        msg_ptr create_sdp_message(int sdp_id, const ::rtc::Description &desc)
        {
            auto sdp_msg = sio::object_message::create();
            sdp_msg->get_map()["type"] = sio::string_message::create(desc.typeString());
            sdp_msg->get_map()["sdp"] = sio::string_message::create(desc);
            sdp_msg->get_map()["mid"] = sio::int_message::create(sdp_id);
            return sdp_msg;
        }

        void Client::update_gst_sdp()
        {
            #ifdef CFGO_SUPPORT_GSTREAMER
            if (m_gst_sdp)
            {
                gst_sdp_message_free(m_gst_sdp);
                m_gst_sdp = nullptr;
            }
            auto &&desc = m_peer->localDescription();
            if (desc)
            {
                auto res = gst_sdp_message_new_from_text(((std::string) *desc).c_str(), &m_gst_sdp);
                if (res != GST_SDP_OK)
                {
                    throw cpptrace::runtime_error("unable to generate the gst sdp message from local desc.");
                }
            }
            #endif
        }

        std::optional<rtc::Description> Client::peer_local_desc() const
        {
            return m_peer->localDescription();
        }

        std::optional<rtc::Description> Client::peer_remote_desc() const
        {
            return m_peer->remoteDescription();
        }

        template <class T>
        concept construct_with_msg_ptr = requires(msg_ptr a) {
            std::make_shared<T>(a);
        };
        template <class T>
        concept convertible_from_msg_ptr = std::is_convertible_v<std::string, T> || std::is_convertible_v<std::int64_t, T>;

        template <construct_with_msg_ptr T>
        auto get_msg_object_field(msg_ptr msg, std::string field) -> std::shared_ptr<T>
        {
            if (!msg)
            {
                return nullptr;
            }
            auto iter = msg->get_map().find(field);
            if (iter == msg->get_map().end() || !iter->second)
            {
                return nullptr;
            }
            if constexpr (std::is_same_v<std::decay_t<T>, sio::message>)
            {
                return iter->second;
            }
            else
            {
                return std::make_shared<T>(iter->second);
            }
        }

        template <construct_with_msg_ptr T>
        void get_msg_object_array_field(msg_ptr msg, std::string field, std::vector<std::shared_ptr<T>> &result)
        {
            if (!msg)
            {
                return;
            }
            auto iter = msg->get_map().find(field);
            if (iter == msg->get_map().end() || !iter->second)
            {
                return;
            }
            for (auto &&m : iter->second->get_vector())
            {
                if (m)
                {
                    result.push_back(std::make_shared<T>(m));
                }
                else
                {
                    result.push_back(nullptr);
                }
            }
        }

        template <convertible_from_msg_ptr T>
        auto constexpr cast_msg_to_base(const msg_ptr msg) -> std::optional<T>
        {
            if constexpr (std::is_convertible_v<std::string, T>)
            {
                return msg->get_string();
            }
            else if constexpr (std::is_convertible_v<std::int64_t, T>)
            {
                return msg->get_int();
            }
            else
            {
                return std::nullopt;
            }
        }

        template <convertible_from_msg_ptr T>
        auto get_msg_base_field(msg_ptr msg, std::string field) -> std::optional<T>
        {
            if (!msg)
            {
                return std::nullopt;
            }
            auto iter = msg->get_map().find(field);
            if (iter == msg->get_map().end() || !iter->second)
            {
                return std::nullopt;
            }
            return cast_msg_to_base<T>(iter->second);
        }

        auto to_description(msg_ptr msg) -> std::optional<::rtc::Description>
        {
            auto &&sdp = get_msg_base_field<std::string>(msg, "sdp");
            auto &&type = get_msg_base_field<std::string>(msg, "type");
            if (!sdp || !type)
            {
                return std::nullopt;
            }
            else
            {
                return ::rtc::Description{sdp.value(), type.value()};
            }
        }

        void Client::setup_socket_close_callback(const close_chan & closer)
        {
            m_client->set_close_listener([closer](auto reason) {
                closer.close();
            });
            m_client->set_fail_listener([closer]() {
                closer.close();
            });
        }

        void Client::clean_socket_close_callback()
        {
            m_client->set_close_listener(nullptr);
            m_client->set_fail_listener(nullptr);
        }

        void Client::emit(const std::string &evt, msg_ptr msg)
        {
            m_client->socket()->emit(evt, std::move(msg));
        }

        auto Client::emit_with_ack(const std::string &evt, msg_ptr msg, close_chan &close_chan) const -> asio::awaitable<cancelable<msg_ptr>>
        {
            auto ack_ch = std::make_shared<msg_chan>();
            msg_chan_weak_ptr weak_ack_ch = ack_ch;
            auto self = shared_from_this();
            auto weak_self = weak_from_this();
            CFGO_THIS_DEBUG("[send msg {}] sending msg...", evt);
            m_client->socket()->emit(evt, msg, [&evt, &weak_self, weak_ack_ch](auto &&ack_msgs)
            {
                if (auto ack_ch = weak_ack_ch.lock())
                {
                    if (auto self = weak_self.lock())
                    {
                        if (ack_msgs.size() > 0)
                        {
                            CFGO_SELF_DEBUG("[send msg {}] got a ack msg.", evt);
                            auto&& ack_msg = ack_msgs[0];
                            self->write_ch(*ack_ch, ack_msg);
                        }
                        else
                        {
                            CFGO_SELF_DEBUG("[send msg {}] got a empty ack msg.", evt);
                            self->write_ch(*ack_ch, msg_ptr());
                        }
                    }
                    else
                    {
                        CFGO_DEBUG("[send msg {}] this has been released.", evt);
                    }
                }
                else
                {
                    CFGO_DEBUG("[send msg {}] ack channel has been released.", evt);
                }
            });
            auto result = co_await chan_read<msg_ptr>(*ack_ch, close_chan);
            if (result.is_canceled())
            {
                CFGO_SELF_DEBUG("[send msg {}] timeout.", evt);
                co_return make_canceled<msg_ptr>();
            }
            else
            {
                CFGO_SELF_DEBUG("[send msg {}] acked.", evt);
                co_return result.value();
            }
        }

        auto Client::wait_for_msg(const std::string &evt, MsgChanner &msg_channer, close_chan &close_chan, std::function<bool(msg_ptr)> cond) -> asio::awaitable<cancelable<msg_ptr>>
        {
            auto &&ch = msg_channer.chan(evt);
            msg_ptr msg = nullptr;
            while (true)
            {
                auto result = co_await chan_read<msg_ptr>(ch, close_chan);
                if (!result)
                {
                    msg_channer.release(evt);
                    co_return result;
                }
                msg = result.value();
                if (cond(msg))
                {
                    msg_channer.release(evt);
                    break;
                }
            }
            co_return msg;
        }

        #define OBSERVE_SIGNALING_STATE(self, peer) \
        CFGO_SELF_DEBUG("current signaling state is {}", signaling_state_to_str(peer->signalingState())); \
        peer->onSignalingStateChange([self](rtc::PeerConnection::SignalingState state) { \
            CFGO_SELF_DEBUG("signaling state changed to {}", signaling_state_to_str(state)); \
        }); \
        DEFER({ \
            CFGO_SELF_DEBUG("{}", "clean onSignalingStateChange callback."); \
            peer->onSignalingStateChange(nullptr); \
        })

        #define OBSERVE_GATHERING_STATE(self, peer) \
        CFGO_SELF_DEBUG("current gathering state is {}", gathering_state_to_str(peer->gatheringState())); \
        peer->onGatheringStateChange([self](rtc::PeerConnection::GatheringState state) { \
            CFGO_SELF_DEBUG("gathering state changed to {}", gathering_state_to_str(state)); \
        }); \
        DEFER({ \
            CFGO_SELF_DEBUG("{}", "clean onGatheringStateChange callback."); \
            peer->onGatheringStateChange(nullptr); \
        })

        #define OBSERVE_ICE_STATE(self, peer) \
        CFGO_SELF_DEBUG("current ice state is {}", ice_state_to_str(peer->iceState())); \
        peer->onIceStateChange([self](rtc::PeerConnection::IceState state) { \
            CFGO_SELF_DEBUG("ice state changed to {}", ice_state_to_str(state)); \
        }); \
        DEFER({ \
            CFGO_SELF_DEBUG("{}", "clean onIceStateChange callback."); \
            peer->onIceStateChange(nullptr); \
        })

        auto Client::subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan close_ch) -> asio::awaitable<cfgo::Subscribation::Ptr>
        {
            auto self = shared_from_this();
            close_chan closer = nullptr;
            if (!close_ch && m_closer)
            {
                closer = m_closer.create_child();
            }
            else
            {
                closer = close_ch.create_child();
            }
            
            if (co_await m_a_mutex.accquire(closer))
            {
                DEFER({
                    CFGO_SELF_DEBUG("{}", "release.");
                    m_a_mutex.release(asio::get_associated_executor(m_io_context));
                });
                self->setup_socket_close_callback(closer);
                DEFER({
                    self->clean_socket_close_callback();
                });
                // self->m_client->socket()->on_any([self](sio::event& event) {
                //     self->m_logger->debug("receive {} msg.", event.get_name());
                //     if (event.need_ack())
                //     {
                //          event.put_ack_message(sio::message::list("ack"));
                //     }
                // });
                // unique_void_chan open_ch {};
                // m_client->set_socket_open_listener([open_ch](std::string const& nsp) {
                //     chan_must_write(open_ch);
                // });
                // DEFER({
                //     m_client->set_socket_open_listener(nullptr);
                // });
                m_client->connect(m_config.m_signal_url, create_auth_message());
                // auto open_res = co_await chan_read<void>(open_ch, closer);
                // if (!open_res)
                // {
                //     self->m_logger->debug("timeout when open the socket.");
                //     co_return nullptr;
                // }
                auto setup_ack = co_await self->emit_with_ack("setup", create_setup_message(), closer);
                if (!setup_ack)
                {
                    CFGO_SELF_DEBUG("timeout when sending setup msg.");
                    co_return nullptr;
                }
                DEFERS_WHEN_FAIL(defers);
                mutex cand_mux;
                std::vector<msg_ptr> cands;
                bool remoted = false;
                m_peer->onLocalCandidate([self](auto &&cand)
                {
                    CFGO_SELF_DEBUG("send local candidate to remote.");
                    self->emit("candidate", create_add_cand_message(cand));
                });
                DEFER({
                    CFGO_SELF_DEBUG("{}", "clean onLocalCandidate callback.");
                    m_peer->onLocalCandidate(nullptr);
                });
                OBSERVE_SIGNALING_STATE(self, m_peer);
                OBSERVE_GATHERING_STATE(self, m_peer);
                OBSERVE_ICE_STATE(self, m_peer);
                asiochan::channel<::rtc::PeerConnection::State> peer_state_chan{};
                m_peer->onStateChange([self, &peer_state_chan](auto &&state)
                {
                    CFGO_SELF_DEBUG("peer state changed to {}", peer_state_to_str(state));
                    switch (state)
                    {
                    case ::rtc::PeerConnection::State::Failed:
                    case ::rtc::PeerConnection::State::Closed:
                    case ::rtc::PeerConnection::State::Connected:
                        self->write_ch(peer_state_chan, state);
                        break;
                    } 
                });
                DEFER({
                    CFGO_SELF_DEBUG("{}", "clean onStateChange callback.");
                    m_peer->onStateChange(nullptr);
                });
                m_client->socket()->on("candidate", [self, &remoted, &cands, &cand_mux](auto &&evt)
                {
                    if (evt.need_ack())
                    {
                        CFGO_SELF_DEBUG("[receive candidate msg] ack");
                        evt.put_ack_message(sio::message::list("ack"));
                    }
                    std::lock_guard guard(cand_mux);
                    if (!remoted)
                    {
                        CFGO_SELF_DEBUG("[receive candidate msg] add candidate to cache.");
                        cands.push_back(evt.get_message());
                    }
                    else
                    {
                        CFGO_SELF_DEBUG("[receive candidate msg] add candidate to peer.");
                        self->add_candidate(evt.get_message());
                    } 
                });
                DEFER({
                    CFGO_SELF_DEBUG("{}", "clean candidate callback.");
                    m_client->socket()->off("candidate");
                });
                MsgChanner msg_channer(this);
                msg_channer.prepare("sdp");
                msg_channer.prepare("subscribed");
                auto sub_msg = create_subscribe_message(pattern, req_types);
                auto sub_res = co_await emit_with_ack("subscribe", create_subscribe_message(pattern, req_types), closer);
                if (!sub_res)
                {
                    CFGO_SELF_DEBUG("timeout when waiting ack of subscribe msg.");
                    co_return nullptr;
                }
                auto sub_id = get_msg_base_field<std::string>(sub_res.value(), "id");
                if (!sub_id)
                {
                    throw std::runtime_error("no id found on subscribe ack msg.");
                }
                CFGO_SELF_DEBUG("sub id: {}", sub_id);
                defers.add_defer([self, sub_id = sub_id.value()]()
                {
                    CFGO_SELF_DEBUG("unsubscribe.");
                    if (self->m_client->opened())
                    {
                        self->emit("subscribe", create_unsubscribe_message(std::move(sub_id)));
                    }
                });

                auto subed_msg = co_await wait_for_msg("subscribed", msg_channer, closer, [&sub_id](auto &&msg)
                { 
                    return get_msg_base_field<std::string>(msg, "subId") == sub_id; 
                });
                if (!subed_msg)
                {
                    CFGO_SELF_DEBUG("timeout when waiting subscribed msg.");
                    co_return nullptr;
                }
                auto sdp_id = get_msg_base_field<std::int64_t>(subed_msg.value(), "sdpId");
                if (!sdp_id)
                {
                    throw std::runtime_error("no sdpId found on subscribed msg.");
                }
                auto pub_id = get_msg_base_field<std::string>(subed_msg.value(), "pubId");
                if (!pub_id)
                {
                    throw std::runtime_error("no pubId found on subscribed msg.");
                }
                auto sub_ptr = std::make_shared<cfgo::Subscribation>(sub_id.value(), pub_id.value());
                get_msg_object_array_field<cfgo::Track>(subed_msg.value(), "tracks", sub_ptr->tracks());
                CFGO_SELF_DEBUG("subscribed with sdp id: {}, pub id: {} and {} tracks", sdp_id, pub_id, sub_ptr->tracks().size());
                if (sub_ptr->tracks().empty())
                {
                    CFGO_SELF_DEBUG("subscribed with no tracks.");
                    defers.success();
                    co_return sub_ptr;
                }
                std::vector<TrackPtr> uncompleted_tracks(sub_ptr->tracks());
                asiochan::channel<void> tracks_ch{};
                m_peer->onTrack([&uncompleted_tracks, &tracks_ch, self](auto &&track) mutable
                {
                    CFGO_SELF_DEBUG("accept track with mid {}.", track->mid());
                    auto&& iter = std::partition(uncompleted_tracks.begin(), uncompleted_tracks.end(), [&track](const TrackPtr& t) -> bool {
                        return t->bind_id() == track->mid();
                    });
                    if (iter != uncompleted_tracks.end())
                    {
                        (*iter)->track() = track;
                        uncompleted_tracks.erase(iter, uncompleted_tracks.end());
                    }
                    if (uncompleted_tracks.empty())
                    {
                        self->write_ch(tracks_ch);
                    } 
                });
                DEFER({
                    CFGO_SELF_DEBUG("{}", "clean onTrack callback.");
                    m_peer->onTrack(nullptr);
                });

                auto sdp_msg = co_await wait_for_msg("sdp", msg_channer, closer, [sdp_id](auto &&msg)
                {
                    return get_msg_base_field<std::int64_t>(msg, "mid") == sdp_id;
                });
                if (!sdp_msg)
                {
                    CFGO_SELF_DEBUG("timeout when waiting sdp msg.");
                    co_return nullptr;
                }  
                m_peer->onLocalDescription([self, sdp_id = sdp_id.value()](const rtc::Description& desc) {
                    self->update_gst_sdp();
                    CFGO_SELF_DEBUG("send local desc to remote.");
                    self->m_client->socket()->emit("sdp", create_sdp_message(sdp_id, desc));
                });
                DEFER({
                    CFGO_SELF_DEBUG("{}", "clean onLocalDescription callback.");
                    m_peer->onLocalDescription(nullptr);
                });
                auto &&desc = to_description(sdp_msg.value());
                if (!desc)
                {
                    throw std::runtime_error("bad sdp msg");
                }
                CFGO_SELF_DEBUG("set remote description");
                m_peer->setRemoteDescription(desc.value());
                {
                    std::lock_guard guard(cand_mux);
                    remoted = true;
                    for (auto &&m : cands)
                    {
                        CFGO_SELF_DEBUG("add cached candidate to peer.");
                        add_candidate(m);
                    }
                }

                CFGO_SELF_DEBUG("waiting peer state changed...");
                auto &&state_res = co_await chan_read<rtc::PeerConnection::State>(peer_state_chan, closer);
                if (!state_res)
                {
                    CFGO_SELF_DEBUG("timeout when waiting peer state.");
                    co_return nullptr;
                }
                auto state = state_res.value();
                if (state != ::rtc::PeerConnection::State::Connected)
                {
                    CFGO_SELF_DEBUG("peer is not connected: {}", (int)state);
                    co_return nullptr;
                }

                auto &&res = co_await chan_read<void>(tracks_ch, closer);
                if (!res)
                {
                    co_return nullptr;
                }
                else
                {
                    for (auto &&track : sub_ptr->tracks())
                    {
                        track->impl()->bind_client(shared_from_this());
                        track->impl()->prepare_track();
                    }
                    defers.success();
                    co_return sub_ptr;
                }
            }
            else
            {
                co_return nullptr;
            }
        }

        auto Client::unsubscribe(const std::string &sub_id, close_chan &close_ch) -> asio::awaitable<cancelable<void>>
        {
            auto closer = close_ch;
            if (!is_valid_close_chan(closer) && is_valid_close_chan(m_closer))
            {
                closer = m_closer;
            }
            if (co_await m_a_mutex.accquire(closer))
            {
                DEFER({
                    m_a_mutex.release(asio::get_associated_executor(m_io_context));
                });
                auto res = co_await emit_with_ack("subscribe", create_unsubscribe_message(sub_id), closer);
                if (!res)
                {
                    co_return res;
                }
                co_return get_msg_base_field<std::string>(res.value(), "id") == sub_id;
            }
            else
            {
                co_return make_canceled();
            }
        }

        void Client::add_candidate(const msg_ptr &msg)
        {
            auto &&op = get_msg_base_field<std::string>(msg, "op");
            if (!op)
            {
                throw std::runtime_error("no op found on candidate msg.");
            }
            if (op == "add")
            {
                msg_ptr candidate_msg = get_msg_object_field<sio::message>(msg, "candidate");
                auto &&candidate = get_msg_base_field<std::string>(candidate_msg, "candidate");
                auto &&mid = get_msg_base_field<std::string>(candidate_msg, "sdpMid");
                m_peer->addRemoteCandidate(::rtc::Candidate{candidate.value_or(""), mid.value_or("")});
            }
        }

        Client::MsgChanner::MsgChanner(Client *const client) : m_client(client) {}

        void Client::MsgChanner::prepare(const std::string &evt, msg_ptr const &ack)
        {
            if (m_chan_map.contains(evt))
            {
                return;
            }
            msg_chan ch{};
            m_chan_map[evt] = ch;
            m_client->m_client->socket()->on(evt, [ch, c = m_client, ack = std::move(ack)](auto &&evt) mutable
            {
                if (evt.need_ack())
                {
                    evt.put_ack_message(sio::message::list(ack));
                }
                c->write_ch(ch, evt.get_message());
            });
        }

        void Client::MsgChanner::release(const std::string &evt)
        {
            m_client->m_client->socket()->off(evt);
            m_chan_map.erase(evt);
        }

        msg_chan &Client::MsgChanner::chan(const std::string &evt)
        {
            if (!m_chan_map.contains(evt))
            {
                throw std::logic_error("evt " + evt + " has not been prepared.");
            }
            return m_chan_map.at(evt);
        }

        const msg_chan &Client::MsgChanner::chan(const std::string &evt) const
        {
            if (!m_chan_map.contains(evt))
            {
                throw std::logic_error("evt " + evt + " has not been prepared.");
            }
            return m_chan_map.at(evt);
        }

        Client::MsgChanner::~MsgChanner()
        {
            for (auto &&p : m_chan_map)
            {
                m_client->m_client->socket()->off(p.first);
            }
        }
    }
}
