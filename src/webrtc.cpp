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
            asiochan::unbounded_channel<std::shared_ptr<rtc::Track>> track_ch {};

            PeerBox(const rtc::Configuration & conf): peer(conf) {}
        };

        class Webrtc : public std::enable_shared_from_this<Webrtc>, public cfgo::Webrtc
        {
        private:
            using PeerBoxPtr = std::shared_ptr<PeerBox>;
            using TrackPtr = std::shared_ptr<rtc::Track>;
            static constexpr const int PEER_STATE_NEW = 0;
            static constexpr const int PEER_STATE_INITIALIZING = 1;
            static constexpr const int PEER_STATE_INITIALIZED = 2;

            cfgo::SignalPtr m_signal;
            cfgo::Configuration m_conf;
            PeerBoxPtr m_peer_box;
            mutex m_peer_mutex;
            close_chan m_peer_signal {nullptr};
            int m_peer_state {PEER_STATE_NEW};
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBRTC);
            cfgo::AsyncMutex m_neg_mux {};
            std::vector<close_chan> m_signal_closers {};
            mutex m_signal_closers_mux {};
            std::vector<close_chan> m_peer_closers {};
            mutex m_peer_closers_mux {};

            #ifdef CFGO_SUPPORT_GSTREAMER
            GstSDPMessage * m_gst_sdp {nullptr};
            #endif

            [[nodiscard]] auto negotiate(close_chan closer, PeerBoxPtr peer, int sdp_id, bool active) -> asio::awaitable<void>;
            static void add_candidate(PeerBoxPtr box, cfgo::Signal::CandMsgPtr msg) {
                if (msg->op == msg::CandidateOp::ADD)
                {
                    box->peer.addRemoteCandidate(rtc::Candidate {msg->candidate.candidate, msg->candidate.sdpMid});
                }
            }
            void register_peer_closer(close_chan closer) {
                std::lock_guard g(m_peer_closers_mux);
                m_peer_closers.push_back(std::move(closer));
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
            auto access_peer_box(close_chan closer) -> asio::awaitable<PeerBoxPtr> {
                auto self = shared_from_this();
                bool do_init = false;
                bool done = false;
                unique_void_chan done_ch {};
                PeerBoxPtr box;
                close_chan peer_signal = nullptr;
                close_chan worker_closer = nullptr;
                do
                {
                    {
                        std::lock_guard g(self->m_peer_mutex);
                        if (self->m_peer_state == PEER_STATE_NEW)
                        {
                            box = std::make_shared<PeerBox>(m_conf.m_rtc_config);
                            self->m_peer_box = box;
                            self->m_peer_state = PEER_STATE_INITIALIZING;
                            self->m_peer_signal = close_chan {};
                            worker_closer = close_chan {};
                            do_init = true;
                        }
                        else if (self->m_peer_state == PEER_STATE_INITIALIZED)
                        {
                            done = true;
                            box = self->m_peer_box;
                        }
                        else
                        {
                            peer_signal = self->m_peer_signal;
                        }
                    }
                    if (done)
                    {
                        co_return box;
                    }
                    if (do_init)
                    {
                        box->peer.onStateChange([box, weak_self = self->weak_from_this(), weak_closer = worker_closer.weak()](rtc::PeerConnection::State state) {
                            if (auto self = weak_self.lock())
                            {
                                if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed)
                                {
                                    {
                                        std::lock_guard g(self->m_peer_mutex);
                                        if (self->m_peer_box == box)
                                        {
                                            self->m_peer_box.reset();
                                            self->m_peer_state = PEER_STATE_NEW;
                                        }
                                    }
                                    if (auto closer = weak_closer.lock())
                                    {
                                        closer.close(std::format("peer state changed to {}", peer_state_to_str(state)));
                                    }
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
                        asio::co_spawn(executor, log_error(fix_async_lambda([self, closer = std::move(worker_closer), cand_ch]() -> asio::awaitable<void> {
                            while (true)
                            {
                                auto cand = co_await chan_read_or_throw<rtc::Candidate>(cand_ch, closer);
                                auto msg = std::make_unique<msg::CandidateMessage>();
                                msg->op = msg::CandidateOp::ADD;
                                msg->candidate.candidate = cand.candidate();
                                msg->candidate.sdpMid = cand.mid();
                                co_await self->m_signal->send_candidate(closer, std::move(msg));
                            }
                        }), self->m_logger), asio::detached);
                    }
                    else
                    {
                        auto waiter = peer_signal.get_waiter();
                        if (waiter)
                        {
                            co_await chan_read_or_throw<void>(waiter.value(), closer);
                        }
                    }
                } while (true);
            }
        public:
            Webrtc(SignalPtr signal, const cfgo::Configuration & conf):
                m_signal(signal), 
                m_conf(conf)
            {
                m_conf.m_rtc_config.disableAutoNegotiation = true;
            }
            ~Webrtc() {
                #ifdef CFGO_SUPPORT_GSTREAMER
                if (m_gst_sdp)
                {
                    gst_sdp_message_free(m_gst_sdp);
                }
                #endif
            }
            [[nodiscard]] auto subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr> override;
            [[nodiscard]] auto unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void> override;
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
                    box->peer.onLocalDescription([self, desc_ch, sdp_id](const rtc::Description & desc) {
                        self->update_gst_sdp(desc);
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
                        box->peer.onLocalDescription([self, answer_sdp_ch, sdp_id](const rtc::Description & desc) {
                            assert(desc.type() == rtc::Description::Type::Answer || desc.type() == rtc::Description::Type::Pranswer);
                            self->update_gst_sdp(desc);
                            auto req_sdp = std::make_shared<msg::SdpMessage>();
                            req_sdp->mid = sdp_id;
                            req_sdp->sdp = desc;
                            req_sdp->type = desc.typeString();
                            chan_must_write(answer_sdp_ch, std::move(req_sdp));
                        });
                        box->peer.setLocalDescription(rtc::Description::Type::Answer);
                        do
                        {
                            sdp = co_await chan_read_or_throw<Signal::SdpMsgPtr>(answer_sdp_ch, closer);
                            co_await self->m_signal->send_sdp(closer, std::move(sdp));
                        } while (sdp->type != msg::SDP_TYPE_ANSWER);
                    } else {
                        throw cpptrace::runtime_error(fmt::format("Expect an offer sdp msg, but got {}. The sdp: {}.", sdp->type, sdp->sdp));
                    }
                }
            } else {
                throw CancelError(closer);
            }
        }

        auto Webrtc::subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr> {
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
                sub_ptr->tracks().emplace_back(track, self);
            }
            std::vector<cfgo::TrackPtr> uncompleted_tracks(sub_ptr->tracks());
            auto box = co_await self->access_peer_box(closer);
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
                    (*iter)->prepare_track();
                    uncompleted_tracks.erase(iter, uncompleted_tracks.end());
                }
            }
            co_return sub_ptr;
        }
    } // namespace impl

    WebrtcUPtr make_webrtc(SignalPtr signal, const cfgo::Configuration & conf) {
        return std::make_unique<impl::Webrtc>(std::move(signal), conf);
    }
    
} // namespace cfgo
