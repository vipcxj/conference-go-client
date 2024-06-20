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
        class Webrtc : std::enable_shared_from_this<Webrtc>
        {
        private:
            cfgo::Signal m_signal;
            rtc::PeerConnection m_peer;
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBRTC);
            asiochan::unbounded_channel<rtc::Candidate> m_cand_ch {};
            cfgo::AsyncMutex m_neg_mux;
            [[nodiscard]] auto negotiate(close_chan closer, int sdp_id, bool active) -> asio::awaitable<void>;
            void add_candidate(cfgo::Signal::CandMsgPtr msg) {
                if (msg->op == msg::CandidateOp::ADD)
                {
                    m_peer.addRemoteCandidate(rtc::Candidate {msg->candidate.candidate, msg->candidate.sdpMid});
                }
            }
        public:
            auto setup(close_chan closer) -> asio::awaitable<void>;
            [[nodiscard]] auto subscribe(close_chan closer, Pattern pattern, std::vector<std::string> req_types) -> asio::awaitable<SubPtr>;
            [[nodiscard]] auto unsubscribe(close_chan closer, std::string sub_id) -> asio::awaitable<void>;
        };

        auto Webrtc::setup(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
            m_peer.onLocalCandidate([self] (rtc::Candidate candidate) {
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
                    co_await self->m_signal.send_candidate(closer, std::move(msg));
                }
            }), self->m_logger);
        }

        auto Webrtc::negotiate(close_chan closer, int sdp_id, bool active) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto remoted = std::make_shared<std::atomic<bool>>(false);
            auto cands = std::make_shared<std::vector<cfgo::Signal::CandMsgPtr>>();
            auto executor = co_await asio::this_coro::executor;
            if (co_await self->m_neg_mux.accquire(closer))
            {
                DEFER({
                    m_neg_mux.release(executor);
                });
                auto cand_cb_id = self->m_signal.on_candidate([self, remoted, cands](cfgo::Signal::CandMsgPtr msg) -> bool {
                    if (remoted->load(std::memory_order::acquire))
                    {
                        self->add_candidate(std::move(msg));
                    } else {
                        cands->push_back(std::move(msg));
                    }
                    return true;
                });
                DEFER({
                    self->m_signal.off_candidate(cand_cb_id);
                });
                asiochan::unbounded_channel<cfgo::Signal::SdpMsgPtr> sdp_ch {};
                auto sdp_cb_id = self->m_signal.on_sdp([sdp_ch, sdp_id](cfgo::Signal::SdpMsgPtr msg) -> bool {
                    if (msg->mid == sdp_id && (msg->type == msg::SdpType::ANSWER || msg->type == msg::SdpType::PRANSWER))
                    {
                        chan_must_write(sdp_ch, msg);
                    }
                    return true;
                });
                DEFER({
                    self->m_signal.off_sdp(sdp_cb_id);
                });
                if (active)
                {
                    self->m_peer.setLocalDescription(rtc::Description::Type::Offer);
                    auto req_sdp = std::make_shared<msg::SdpMessage>();
                    auto local_desc = self->m_peer.localDescription().value();
                    req_sdp->mid = sdp_id;
                    req_sdp->sdp = local_desc;
                    req_sdp->type = local_desc.typeString();
                    self->m_signal.send_sdp(closer, std::move(req_sdp));
                    while (true)
                    {
                        auto sdp_msg = co_await chan_read_or_throw<cfgo::Signal::SdpMsgPtr>(sdp_ch, closer);
                        self->m_peer.setRemoteDescription(rtc::Description(sdp_msg->sdp, sdp_type_to_string(sdp_msg->type)));
                        remoted->store(true, std::memory_order::release);
                        for (auto m : *cands) {
                            self->add_candidate(std::move(m));
                        }
                        cands->clear();
                        if (sdp_msg->type == msg::SdpType::ANSWER)
                        {
                            break;
                        }
                    }
                } else {

                }
                
            }

        }
    } // namespace impl
    
} // namespace cfgo
