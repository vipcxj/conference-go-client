#ifndef _CFGO_IMPL_SIGNAL_HPP_
#define _CFGO_IMPL_SIGNAL_HPP_

#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/configuration.hpp"

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
                PRO_DEF_MEMBER_DISPATCH(create_msg, pro::proxy<spec::RawSigMsg>(const std::string_view & evt, nlohmann::json && payload, bool ack));
            }
        }

        PRO_DEF_FACADE(RawSignal, PRO_MAKE_DISPATCH_PACK(
            signal::raw::connect,
            signal::raw::run,
            signal::raw::send, 
            signal::raw::on_msg,
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
        auto ack(close_chan closer, std::string payload) const -> asio::awaitable<void>;
        auto ack_err(close_chan closer, std::unique_ptr<ServerErrorObject> err) const -> asio::awaitable<void>;
    };

    using SigMsgCb = std::function<bool(SignalMsg, SignalAcker)>;

    class Signal : public ImplBy<impl::Signal> {
    public:
        auto connect(close_chan closer) -> asio::awaitable<void>;
        auto send_message(const close_chan & closer, SignalMsg && msg) -> asio::awaitable<std::string>;
        std::uint64_t on_message(const SigMsgCb & cb);
        std::uint64_t on_message(SigMsgCb && cb);
        void off_message(std::uint64_t id);
    };

} // namespace cfgo


#endif