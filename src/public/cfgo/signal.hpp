#ifndef _CFGO_IMPL_SIGNAL_HPP_
#define _CFGO_IMPL_SIGNAL_HPP_

#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/error.hpp"
#include "cfgo/message.hpp"

#include <memory>
#include <vector>

namespace cfgo
{
    struct RawSigMsg {
        virtual ~RawSigMsg() = 0;
        virtual std::string_view evt() const noexcept = 0;
        virtual const nlohmann::json & payload() const noexcept = 0;
        virtual bool ack() const noexcept = 0;
        virtual std::uint64_t msg_id() const noexcept = 0;
    };
    using RawSigMsgPtr = std::shared_ptr<RawSigMsg>;
    using RawSigMsgUPtr = std::unique_ptr<RawSigMsg>;

    struct RawSigAcker {
        virtual ~RawSigAcker() = 0;
        virtual auto ack(nlohmann::json payload) -> asio::awaitable<void> = 0;
        virtual auto ack(std::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> = 0;
    };
    using RawSigAckerPtr = std::shared_ptr<RawSigAcker>;
    using RawSigMsgCb = std::function<asio::awaitable<bool>(RawSigMsgPtr, RawSigAckerPtr acker)>;

    struct RawSignal {
        virtual ~RawSignal() = 0;
        virtual auto connect(close_chan closer) -> asio::awaitable<void> = 0;
        virtual auto send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> = 0;
        virtual std::uint64_t on_msg(RawSigMsgCb cb) = 0;
        virtual void off_msg(std::uint64_t) = 0;
        virtual auto create_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> RawSigMsgUPtr = 0;
        /**
         * used to get the close event, close this closer will not close the raw signal.
         */
        virtual close_chan get_notify_closer() = 0;
        /**
         * close the raw signal.
         */
        virtual void close() = 0;
    };
    using RawSignalPtr = std::shared_ptr<RawSignal>;
    using RawSignalUPtr = std::unique_ptr<RawSignal>;

    struct SignalMsg {
        virtual ~SignalMsg() = 0;
        virtual std::string_view evt() const noexcept = 0;
        virtual bool ack() const noexcept = 0;
        virtual std::string_view room() const noexcept = 0;
        virtual std::string_view user() const noexcept = 0;
        virtual std::string_view payload() const noexcept = 0;
        virtual std::uint32_t msg_id() const noexcept = 0;
        virtual std::string && consume() noexcept = 0;
    };
    using SignalMsgPtr = std::shared_ptr<SignalMsg>;
    using SignalMsgUPtr = std::unique_ptr<SignalMsg>;

    struct SignalAcker {
        virtual ~SignalAcker() = 0;
        virtual auto ack(close_chan closer, std::string payload) -> asio::awaitable<void> = 0;
        virtual auto ack(close_chan closer, std::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> = 0;
    };
    using SignalAckerPtr = std::shared_ptr<SignalAcker>;

    using SigMsgCb = std::function<asio::awaitable<bool>(SignalMsgPtr, SignalAckerPtr)>;

    struct Signal {
        using CandMsgPtr = std::shared_ptr<msg::CandidateMessage>;
        using CandCb = std::function<bool(CandMsgPtr)>;
        using SdpMsgPtr = std::shared_ptr<msg::SdpMessage>;
        using SdpCb = std::function<bool(SdpMsgPtr)>;
        using SubscribeMsgPtr = std::unique_ptr<msg::SubscribeMessage>;
        using SubscribedMsgPtr = std::unique_ptr<msg::SubscribedMessage>;
        using SubscribeResultPtr = std::unique_ptr<msg::SubscribeResultMessage>;

        virtual ~Signal() = 0;
        virtual auto connect(close_chan closer) -> asio::awaitable<void> = 0;
        virtual auto send_candidate(close_chan closer, CandMsgPtr msg) -> asio::awaitable<void> = 0;
        virtual std::uint64_t on_candidate(CandCb cb) = 0;
        virtual void off_candidate(std::uint64_t id) = 0;
        virtual std::uint64_t on_sdp(SdpCb cb) = 0;
        virtual void off_sdp(std::uint64_t id) = 0;
        virtual auto send_sdp(close_chan closer, SdpMsgPtr msg) -> asio::awaitable<void> = 0;
        virtual auto subsrcibe(close_chan closer, SubscribeMsgPtr msg) -> asio::awaitable<SubscribeResultPtr> = 0;
        virtual auto wait_subscribed(close_chan closer, SubscribeResultPtr sub) -> asio::awaitable<SubscribedMsgPtr> = 0;
        virtual auto create_message(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload) -> cfgo::SignalMsgUPtr = 0;
        virtual auto send_message(close_chan closer, SignalMsgUPtr msg) -> asio::awaitable<std::string> = 0;
        virtual std::uint64_t on_message(const SigMsgCb & cb) = 0;
        virtual std::uint64_t on_message(SigMsgCb && cb) = 0;
        virtual void off_message(std::uint64_t id) = 0;
    };
    using SignalPtr = std::shared_ptr<Signal>;
    using SignalUPtr = std::unique_ptr<Signal>;

    struct WebsocketSignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout;

        static WebsocketSignalConfigure from_conf(const Configuration & conf) {
            return WebsocketSignalConfigure{
                .url = conf.m_signal_url,
                .token = conf.m_token,
                .ready_timeout = conf.m_ready_timeout,
            };
        }
    };

    auto make_websocket_signal(close_chan closer, const WebsocketSignalConfigure & conf) -> SignalUPtr;

} // namespace cfgo


#endif