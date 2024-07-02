#include "cfgo/webrtc.hpp"
#include "cfgo/track.hpp"
#include "rtc/rtc.hpp"
#include "cfgo/defer.hpp"

#include <unordered_set>

template<>
struct std::hash<rtc::Candidate> {
    std::size_t operator()(const rtc::Candidate & k) const {
        using std::hash;
        using std::string;
        return (hash<string>()(k.candidate()) ^ (hash<string>()(k.mid()) << 1)) >> 1;
    }
};

namespace cfgo
{
    namespace impl
    {
        constexpr const char *peer_state_to_str(rtc::PeerConnection::State state)
        {
            using State = rtc::PeerConnection::State;
            switch (state)
            {
            case State::Closed:
                return "closed";
            case State::Connected:
                return "connected";
            case State::Connecting:
                return "connecting";
            case State::Disconnected:
                return "disconnected";
            case State::Failed:
                return "failed";
            case State::New:
                return "new";
            default:
                return "unknown";
            }
        }

        struct PeerBox {
            rtc::PeerConnection peer;
            #ifdef CFGO_SUPPORT_GSTREAMER
            GstSDPMessage * m_gst_sdp {nullptr};
            #endif
            asiochan::unbounded_channel<std::shared_ptr<rtc::Track>> track_ch {};
            std::unordered_set<close_chan> m_peer_closers {};
            mutex m_peer_closers_mux {};

            PeerBox(const rtc::Configuration & conf): peer(conf) {}
            ~PeerBox() {
                #ifdef CFGO_SUPPORT_GSTREAMER
                if (m_gst_sdp)
                {
                    gst_sdp_message_free(m_gst_sdp);
                }
                #endif
            }

            void update_gst_sdp(const rtc::Description & desc)
            {
                #ifdef CFGO_SUPPORT_GSTREAMER
                if (m_gst_sdp)
                {
                    gst_sdp_message_free(m_gst_sdp);
                    m_gst_sdp = nullptr;
                }
                auto res = gst_sdp_message_new_from_text(((std::string) desc).c_str(), &m_gst_sdp);
                if (res != GST_SDP_OK)
                {
                    throw cpptrace::runtime_error("unable to generate the gst sdp message from local desc.");
                }
                #endif
            }

            void register_peer_closer(const close_chan & closer) {
                std::lock_guard g(m_peer_closers_mux);
                m_peer_closers.insert(closer);
            }

            void unregister_peer_closer(const close_chan & closer) {
                std::lock_guard g(m_peer_closers_mux);
                m_peer_closers.erase(closer);
            }

            void process_peer_closers(std::string_view reason) {
                std::lock_guard g(m_peer_closers_mux);
                for (auto && closer : m_peer_closers) {
                    std::string the_reason {reason};
                    closer.close_no_except(std::move(the_reason));
                }
            }
        };
        using PeerBoxPtr = std::shared_ptr<PeerBox>;

        class Webrtc : public std::enable_shared_from_this<Webrtc>, public cfgo::Webrtc
        {
        private:
            using TrackPtr = std::shared_ptr<rtc::Track>;
            static constexpr const int PEER_STATE_NEW = 0;
            static constexpr const int PEER_STATE_INITIALIZING = 1;
            static constexpr const int PEER_STATE_INITIALIZED = 2;

            close_chan m_closer;
            cfgo::SignalPtr m_signal;
            cfgo::Configuration m_conf;
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBRTC);
            cfgo::AsyncMutex m_neg_mux {};
            InitableBox<PeerBoxPtr> m_access_peer;
            
            [[nodiscard]] auto negotiate(close_chan closer, PeerBoxPtr peer, int sdp_id, bool active) -> asio::awaitable<void>;
            static void add_candidate(PeerBoxPtr box, cfgo::Signal::CandMsgPtr msg) {
                if (msg->op == msg::CandidateOp::ADD)
                {
                    box->peer.addRemoteCandidate(rtc::Candidate {msg->candidate.candidate, msg->candidate.sdpMid.value_or("")});
                }
            }
            auto _access_peer_box(close_chan closer) -> asio::awaitable<PeerBoxPtr> {
                auto self = shared_from_this();
                close_chan peer_closer = self->m_closer.create_child();
                auto box = std::make_shared<PeerBox>(m_conf.m_rtc_config);
                box->peer.onStateChange([box, weak_self = self->weak_from_this(), peer_closer](rtc::PeerConnection::State state) {
                    if (auto self = weak_self.lock())
                    {
                        if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed)
                        {
                            box->process_peer_closers(std::format("peer state changed to {}", peer_state_to_str(state)));
                            peer_closer.close(std::format("peer state changed to {}", peer_state_to_str(state)));
                        }
                    }
                });
                asiochan::unbounded_channel<rtc::Candidate> cand_ch {};
                box->peer.onLocalCandidate([cand_ch](rtc::Candidate candidate) {
                    chan_must_write(cand_ch, std::move(candidate));
                });
                box->peer.onTrack([box](TrackPtr track) {
                    chan_must_write(box->track_ch, track);
                });
                auto executor = co_await asio::this_coro::executor;
                asio::co_spawn(executor, fix_async_lambda(log_error([self, peer_closer, box]() -> asio::awaitable<void> {
                    co_await peer_closer.await();
                    box->peer.close();

                }, self->m_logger)), asio::detached);
                asio::co_spawn(executor, log_error(fix_async_lambda([self, cand_ch]() -> asio::awaitable<void> {
                    while (true)
                    {
                        auto cand = co_await chan_read_or_throw<rtc::Candidate>(cand_ch, self->m_closer);
                        auto msg = std::make_unique<msg::CandidateMessage>();
                        msg->op = msg::CandidateOp::ADD;
                        msg->candidate.candidate = cand.candidate();
                        msg->candidate.sdpMid = cand.mid();
                        co_await self->m_signal->send_candidate(self->m_closer, std::move(msg));
                    }
                }), self->m_logger), asio::detached);
                co_return box;
            }

            auto access_peer_box(close_chan closer) -> asio::awaitable<PeerBoxPtr> {
                return m_access_peer(std::move(closer));
            }
        public:
            Webrtc(SignalPtr signal, const cfgo::Configuration & conf):
                m_closer(signal->get_notify_closer()),
                m_signal(signal), 
                m_conf(conf),
                m_access_peer([this](auto closer) {
                    return _access_peer_box(std::move(closer));
                }, false)
            {
                m_conf.m_rtc_config.disableAutoNegotiation = true;
            }
            ~Webrtc() noexcept {}
            [[nodiscard]] auto subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr> override;
            [[nodiscard]] auto unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void> override;
            close_chan get_notify_closer() override {
                return m_closer.create_child();
            }
            close_chan get_closer() noexcept override {
                return m_closer;
            }
            void close() override {
                m_closer.close_no_except();
            }
        };

        auto Webrtc::negotiate(close_chan closer, PeerBoxPtr box, int sdp_id, bool active) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto remoted = std::make_shared<std::atomic<bool>>(false);
            auto cands = std::make_shared<std::vector<cfgo::Signal::CandMsgPtr>>();
            auto executor = co_await asio::this_coro::executor;
            if (co_await self->m_neg_mux.accquire(closer))
            {
                DEFER({
                    m_neg_mux.release(executor);
                });
                auto cand_cb_id = self->m_signal->on_candidate([box, remoted, cands](cfgo::Signal::CandMsgPtr msg) -> bool {
                    if (remoted->load(std::memory_order::acquire))
                    {
                        add_candidate(box, std::move(msg));
                    } else {
                        cands->push_back(std::move(msg));
                    }
                    return true;
                });
                DEFER({
                    self->m_signal->off_candidate(cand_cb_id);
                });
                asiochan::unbounded_channel<cfgo::Signal::SdpMsgPtr> sdp_ch {};
                auto sdp_cb_id = self->m_signal->on_sdp([sdp_ch, sdp_id](cfgo::Signal::SdpMsgPtr msg) -> bool {
                    if (msg->mid == sdp_id && (msg->type == msg::SDP_TYPE_ANSWER || msg->type == msg::SDP_TYPE_PRANSWER))
                    {
                        chan_must_write(sdp_ch, msg);
                    }
                    return true;
                });
                DEFER({
                    self->m_signal->off_sdp(sdp_cb_id);
                });
                if (active)
                {
                    asiochan::unbounded_channel<Signal::SdpMsgPtr> desc_ch;
                    box->peer.onLocalDescription([box, desc_ch, sdp_id](const rtc::Description & desc) {
                        box->update_gst_sdp(desc);
                        auto req_sdp = std::make_shared<msg::SdpMessage>();
                        req_sdp->mid = sdp_id;
                        req_sdp->sdp = desc;
                        req_sdp->type = desc.typeString();
                        chan_must_write(desc_ch, std::move(req_sdp));
                    });
                    DEFER({
                        box->peer.onLocalDescription(nullptr);
                    });
                    box->peer.setLocalDescription(rtc::Description::Type::Offer);
                    auto sdp = co_await chan_read_or_throw<Signal::SdpMsgPtr>(desc_ch, closer);
                    co_await self->m_signal->send_sdp(closer, std::move(sdp));
                    while (true)
                    {
                        auto sdp_msg = co_await chan_read_or_throw<cfgo::Signal::SdpMsgPtr>(sdp_ch, closer);
                        box->peer.setRemoteDescription(rtc::Description(sdp_msg->sdp, sdp_msg->type));
                        remoted->store(true, std::memory_order::release);
                        for (auto m : *cands) {
                            add_candidate(box, std::move(m));
                        }
                        cands->clear();
                        if (sdp_msg->type == msg::SDP_TYPE_ANSWER)
                        {
                            break;
                        }
                    }
                } else {
                    unique_chan<Signal::SdpMsgPtr> off_sdp_ch {};
                    auto sdp_cb_id = self->m_signal->on_sdp([off_sdp_ch, sdp_id](Signal::SdpMsgPtr sdp) -> bool {
                        if (sdp->mid == sdp_id)
                        {
                            chan_must_write(std::move(off_sdp_ch), std::move(sdp));
                            return false;
                        }
                        return true;
                    });
                    DEFER({
                        self->m_signal->off_sdp(sdp_cb_id);
                    });
                    auto sdp = co_await chan_read_or_throw<Signal::SdpMsgPtr>(off_sdp_ch, closer);
                    if (sdp->type == msg::SDP_TYPE_OFFER) {
                        box->peer.setRemoteDescription(rtc::Description {sdp->sdp, sdp->type});
                        remoted->store(true, std::memory_order::release);
                        for (auto m : *cands) {
                            add_candidate(box, std::move(m));
                        }
                        cands->clear();
                        asiochan::unbounded_channel<Signal::SdpMsgPtr> answer_sdp_ch {};
                        box->peer.onLocalDescription([box, answer_sdp_ch, sdp_id](const rtc::Description & desc) {
                            assert(desc.type() == rtc::Description::Type::Answer || desc.type() == rtc::Description::Type::Pranswer);
                            box->update_gst_sdp(desc);
                            auto req_sdp = std::make_shared<msg::SdpMessage>();
                            req_sdp->mid = sdp_id;
                            req_sdp->sdp = desc;
                            req_sdp->type = desc.typeString();
                            chan_must_write(answer_sdp_ch, std::move(req_sdp));
                        });
                        box->peer.setLocalDescription(rtc::Description::Type::Answer);
                        std::string sdp_type;
                        do
                        {
                            sdp = co_await chan_read_or_throw<Signal::SdpMsgPtr>(answer_sdp_ch, closer);
                            sdp_type = sdp->type;
                            co_await self->m_signal->send_sdp(closer, std::move(sdp));
                        } while (sdp_type != msg::SDP_TYPE_ANSWER);
                    } else {
                        throw cpptrace::runtime_error(fmt::format("Expect an offer sdp msg, but got {}. The sdp: {}.", sdp->type, sdp->sdp));
                    }
                }
            } else {
                throw CancelError(closer);
            }
        }

        auto Webrtc::subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr> {
            closer = closer.create_child();
            auto self = shared_from_this();
            self->m_logger->debug("subscribing...");
            auto sub_req_msg = std::make_unique<msg::SubscribeMessage>();
            sub_req_msg->op = msg::SubscribeOp::ADD;
            sub_req_msg->reqTypes = std::move(req_types);
            sub_req_msg->pattern = std::move(pattern);
            auto sub_req_res = co_await self->m_signal->subsrcibe(closer, std::move(sub_req_msg));
            auto sub_res = co_await self->m_signal->wait_subscribed(closer, std::move(sub_req_res));
            auto sub_ptr = std::make_shared<cfgo::Subscribation>(sub_res->subId, sub_res->pubId);
            if (sub_res->tracks.empty())
            {
                co_return sub_ptr;
            }
            for (auto && track : sub_res->tracks)
            {
                sub_ptr->tracks().push_back(std::make_shared<cfgo::Track>(track));
            }
            std::vector<cfgo::TrackPtr> uncompleted_tracks(sub_ptr->tracks());
            auto box = co_await self->access_peer_box(closer);
            box->register_peer_closer(closer);
            DEFER({
                box->unregister_peer_closer(closer);
            });
            co_await self->negotiate(closer, box, sub_res->sdpId, false);
            while (!uncompleted_tracks.empty())
            {
                auto rtc_track = co_await chan_read_or_throw<TrackPtr>(box->track_ch, closer);
                auto&& iter = std::partition(uncompleted_tracks.begin(), uncompleted_tracks.end(), [&rtc_track](const cfgo::TrackPtr& t) -> bool {
                    return t->bind_id() == rtc_track->mid();
                });
                if (iter != uncompleted_tracks.end())
                {
                    (*iter)->track() = rtc_track;
                    (*iter)->prepare_track(
                        #ifdef CFGO_SUPPORT_GSTREAMER
                        box->m_gst_sdp
                        #endif
                    );
                    uncompleted_tracks.erase(iter, uncompleted_tracks.end());
                }
            }
            co_return sub_ptr;
        }

        auto Webrtc::unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto sub_req_msg = std::make_unique<msg::SubscribeMessage>();
            sub_req_msg->op = msg::SubscribeOp::REMOVE;
            sub_req_msg->id = sub_id;
            co_await self->m_signal->subsrcibe(closer, std::move(sub_req_msg));
            co_return;
        }
    } // namespace impl

    WebrtcPtr make_webrtc(SignalPtr signal, const cfgo::Configuration & conf) {
        return std::make_shared<impl::Webrtc>(std::move(signal), conf);
    }
    
} // namespace cfgo
