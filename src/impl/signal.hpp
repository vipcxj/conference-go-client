#ifndef _CFGO_IMPL_SIGNAL_HPP_
#define _CFGO_IMPL_SIGNAL_HPP_

#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/error.hpp"
#include "impl/message.hpp"

#include "proxy/proxy.h"
#include <memory>
#include <vector>

namespace cfgo
{
    namespace spec
    {

        namespace signal
        {
            namespace raw {
                namespace msg
                {
                    PRO_DEF_MEMBER_DISPATCH(evt, std::string_view() noexcept);
                    PRO_DEF_MEMBER_DISPATCH(payload, const nlohmann::json & () noexcept);
                    PRO_DEF_MEMBER_DISPATCH(ack, bool() noexcept);
                    PRO_DEF_MEMBER_DISPATCH(msg_id, std::uint64_t() noexcept);
                } // namespace msg

                namespace ack
                {
                    PRO_DEF_MEMBER_DISPATCH(ack, asio::awaitable<void>(nlohmann::json));
                    PRO_DEF_MEMBER_DISPATCH(ack_err, asio::awaitable<void>(std::unique_ptr<ServerErrorObject>));
                } // namespace msg
            } // namespace raw
        } // namespace signal

        PRO_DEF_FACADE(RawSigMsg, PRO_MAKE_DISPATCH_PACK(
            signal::raw::msg::evt, 
            signal::raw::msg::payload,
            signal::raw::msg::ack,
            signal::raw::msg::msg_id
        ), pro::copyable_ptr_constraints);

        PRO_DEF_FACADE(RawSigAcker, PRO_MAKE_DISPATCH_PACK(
            signal::raw::ack::ack,
            signal::raw::ack::ack_err
        ), pro::copyable_ptr_constraints);

        using RawSigMsgCb = std::function<bool(pro::proxy<RawSigMsg> msg, pro::proxy<RawSigAcker> acker)>;

        namespace signal {
            namespace raw {
                PRO_DEF_MEMBER_DISPATCH(connect, asio::awaitable<void>(close_chan closer));
                PRO_DEF_MEMBER_DISPATCH(run, asio::awaitable<void>(close_chan closer));
                PRO_DEF_MEMBER_DISPATCH(send, asio::awaitable<nlohmann::json>(const close_chan & closer, pro::proxy<spec::RawSigMsg> msg));
                PRO_DEF_MEMBER_DISPATCH(on_msg, std::uint64_t(RawSigMsgCb cb));
                PRO_DEF_MEMBER_DISPATCH(off_msg, void(std::uint64_t));
                PRO_DEF_MEMBER_DISPATCH(create_msg, pro::proxy<spec::RawSigMsg>(const std::string_view & evt, nlohmann::json && payload, bool ack));
            }
        }

        PRO_DEF_FACADE(RawSignal, PRO_MAKE_DISPATCH_PACK(
            signal::raw::connect,
            signal::raw::run,
            signal::raw::send, 
            signal::raw::on_msg,
            signal::raw::off_msg,
            signal::raw::create_msg
        ), pro::copyable_ptr_constraints);

    } // namespace spec

    struct WebsocketSignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout;
    };

    auto make_websocket_raw_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> pro::proxy<spec::RawSigMsg>;

    auto make_websocket_raw_signal(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal>;

    namespace impl
    {
        class Signal;
        class SignalMsg;
        class SignalAcker;
    } // namespace impl

    struct SignalMsg : public ImplBy<impl::SignalMsg> {
        using ImplBy::ImplBy;
        std::string_view evt() const noexcept;
        bool ack() const noexcept;
        std::string_view room() const noexcept;
        std::string_view user() const noexcept;
        std::string_view payload() const noexcept;
        std::uint32_t msg_id() const noexcept;
        std::string && consume() const noexcept;
    };

    struct SignalAcker : public ImplBy<impl::SignalAcker> {
        using ImplBy::ImplBy;
        auto async_ack(close_chan closer, std::string payload) const -> asio::awaitable<void>;
        auto async_ack_err(close_chan closer, std::unique_ptr<ServerErrorObject> err) const -> asio::awaitable<void>;
        template<typename Executor>
        void ack(Executor executor, close_chan closer, std::string payload) const {
            impl()->ack(std::move(executor), std::move(closer), std::move(payload));
        }
        template<typename Executor>
        void ack_err(Executor executor, close_chan closer, std::unique_ptr<ServerErrorObject> err) const {
            impl()->ack_err(std::move(executor), std::move(closer), std::move(err));
        }
    };

    using SigMsgCb = std::function<bool(SignalMsg, SignalAcker)>;

    class Signal : public ImplBy<impl::Signal> {
    public:
        using CandMsgPtr = std::shared_ptr<msg::CandidateMessage>;
        using CandCb = std::function<bool(CandMsgPtr)>;
        using SdpMsgPtr = std::shared_ptr<msg::SdpMessage>;
        using SdpCb = std::function<bool(SdpMsgPtr)>;
        using SubscribeMsgPtr = std::unique_ptr<msg::SubscribeMessage>;
        using SubscribedMsgPtr = std::unique_ptr<msg::SubscribedMessage>;
        using SubscribeResultPtr = std::unique_ptr<msg::SubscribeResultMessage>;


        auto connect(close_chan closer) -> asio::awaitable<void>;
        auto send_candidate(close_chan closer, CandMsgPtr msg) -> asio::awaitable<void>;
        std::uint64_t on_candidate(CandCb cb);
        void off_candidate(std::uint64_t id);
        std::uint64_t on_sdp(SdpCb cb);
        void off_sdp(std::uint64_t id);
        auto send_sdp(const close_chan & closer, SdpMsgPtr msg) -> asio::awaitable<void>;
        auto subsrcibe(const close_chan & closer, SubscribeMsgPtr msg) -> asio::awaitable<SubscribeResultPtr>;
        auto wait_subscribed(const close_chan & closer, SubscribeResultPtr sub) -> asio::awaitable<SubscribedMsgPtr>;
        auto send_message(const close_chan & closer, SignalMsg && msg) -> asio::awaitable<std::string>;
        std::uint64_t on_message(const SigMsgCb & cb);
        std::uint64_t on_message(SigMsgCb && cb);
        void off_message(std::uint64_t id);
    };

} // namespace cfgo


#endif