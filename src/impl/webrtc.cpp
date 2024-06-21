#include "impl/webrtc.hpp"
#include "impl/signal.hpp"
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

        class Webrtc : std::enable_shared_from_this<Webrtc>
        {
        private:
            using PeerPtr = std::shared_ptr<rtc::PeerConnection>;
            cfgo::SignalPtr m_signal;
            cfgo::Configuration m_conf;
            PeerPtr m_peer;
            mutex m_peer_mux;
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBRTC);
            asiochan::unbounded_channel<rtc::Candidate> m_cand_ch {};
            cfgo::AsyncMutex m_neg_mux {};
            std::vector<close_chan> m_signal_closers {};
            mutex m_signal_closers_mux {};
            std::vector<close_chan> m_peer_closers {};
            mutex m_peer_closers_mux {};
            [[nodiscard]] auto negotiate(close_chan closer, PeerPtr peer, int sdp_id, bool active) -> asio::awaitable<void>;
            static void add_candidate(PeerPtr peer, cfgo::Signal::CandMsgPtr msg) {
                if (msg->op == msg::CandidateOp::ADD)
                {
                    peer->addRemoteCandidate(rtc::Candidate {msg->candidate.candidate, msg->candidate.sdpMid});
                }
            }
            void register_peer_closer(close_chan closer) {
                std::lock_guard g(m_peer_closers_mux);
                m_peer_closers.push_back(std::move(closer));
            }
            PeerPtr access_peer(close_chan closer) {
                auto weak_self = weak_from_this();
                std::lock_guard g(m_peer_mux);
                if (!m_peer)
                {
                    m_peer = std::make_shared<rtc::PeerConnection>(m_conf.m_rtc_config);
                    m_peer->onStateChange([weak_self, weak_closer = closer.weak()](rtc::PeerConnection::State state) {
                        if (auto self = weak_self.lock())
                        {
                            if (state == rtc::PeerConnection::State::Closed || state == rtc::PeerConnection::State::Failed)
                            {
                                self->m_peer.reset();
                                if (auto closer = weak_closer.lock())
                                {
                                    closer.close(std::format("peer state changed to {}", peer_state_to_str(state)));
                                }
                            }
                        }
                    });
                }
                return m_peer;
            }
        public:
            auto setup(close_chan closer) -> asio::awaitable<void>;
            [[nodiscard]] auto subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr>;
            [[nodiscard]] auto unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void>;
        };

        auto Webrtc::setup(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
            m_peer->onStateChange([](rtc::PeerConnection::State state) {

            });
            m_peer->onLocalCandidate([self](rtc::Candidate candidate) {
                chan_must_write(self->m_cand_ch, std::move(candidate));
            });
            log_error(fix_async_lambda([self, closer = std::move(closer)]() -> asio::awaitable<void> {
                while (true)
                {
                    auto maybe_cand = co_await chan_read<rtc::Candidate>(self->m_cand_ch, closer);
                    if (!maybe_cand)
                    {
                        co_return;
                    }
                    auto & cand = maybe_cand.value();
                    auto msg = std::make_unique<msg::CandidateMessage>();
                    msg->op = msg::CandidateOp::ADD;
                    msg->candidate.candidate = cand.candidate();
                    msg->candidate.sdpMid = cand.mid();
                    co_await self->m_signal->send_candidate(closer, std::move(msg));
                }
            }), self->m_logger);
        }

        auto Webrtc::negotiate(close_chan closer, PeerPtr peer, int sdp_id, bool active) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto remoted = std::make_shared<std::atomic<bool>>(false);
            auto cands = std::make_shared<std::vector<cfgo::Signal::CandMsgPtr>>();
            auto executor = co_await asio::this_coro::executor;
            if (co_await self->m_neg_mux.accquire(closer))
            {
                DEFER({
                    m_neg_mux.release(executor);
                });
                auto cand_cb_id = self->m_signal->on_candidate([peer, remoted, cands](cfgo::Signal::CandMsgPtr msg) -> bool {
                    if (remoted->load(std::memory_order::acquire))
                    {
                        add_candidate(peer, std::move(msg));
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
                    peer->onLocalDescription([desc_ch, sdp_id](const rtc::Description & desc) {
                        auto req_sdp = std::make_shared<msg::SdpMessage>();
                        req_sdp->mid = sdp_id;
                        req_sdp->sdp = desc;
                        req_sdp->type = desc.typeString();
                        chan_must_write(desc_ch, std::move(req_sdp));
                    });
                    DEFER({
                        peer->onLocalDescription(nullptr);
                    });
                    peer->setLocalDescription(rtc::Description::Type::Offer);
                    auto sdp = co_await chan_read_or_throw<Signal::SdpMsgPtr>(desc_ch, closer);
                    co_await self->m_signal->send_sdp(closer, std::move(sdp));
                    while (true)
                    {
                        auto sdp_msg = co_await chan_read_or_throw<cfgo::Signal::SdpMsgPtr>(sdp_ch, closer);
                        peer->setRemoteDescription(rtc::Description(sdp_msg->sdp, sdp_msg->type));
                        remoted->store(true, std::memory_order::release);
                        for (auto m : *cands) {
                            add_candidate(peer, std::move(m));
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
                        peer->setRemoteDescription(rtc::Description {sdp->sdp, sdp->type});
                        remoted->store(true, std::memory_order::release);
                        for (auto m : *cands) {
                            add_candidate(peer, std::move(m));
                        }
                        cands->clear();
                        asiochan::unbounded_channel<Signal::SdpMsgPtr> answer_sdp_ch {};
                        peer->onLocalDescription([answer_sdp_ch, sdp_id](const rtc::Description & desc) {
                            assert(desc.type() == rtc::Description::Type::Answer || desc.type() == rtc::Description::Type::Pranswer);
                            auto req_sdp = std::make_shared<msg::SdpMessage>();
                            req_sdp->mid = sdp_id;
                            req_sdp->sdp = desc;
                            req_sdp->type = desc.typeString();
                            chan_must_write(answer_sdp_ch, std::move(req_sdp));
                        });
                        peer->setLocalDescription(rtc::Description::Type::Answer);
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

        }
    } // namespace impl
    
} // namespace cfgo
