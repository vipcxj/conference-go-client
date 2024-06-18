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
    class WSMsg;
    class WSAck;
    using SignalRawMsg = std::vector<std::byte>;
    using WSMsgCb = std::function<bool(WSMsg&)>;
    using WSAckCb = std::function<void(WSAck&)>;
    namespace spec
    {

        namespace msg
        {
            PRO_DEF_MEMBER_DISPATCH(msg_id, std::uint64_t() noexcept);
            PRO_DEF_MEMBER_DISPATCH(evt, std::string_view() noexcept);
            PRO_DEF_MEMBER_DISPATCH(payload, const nlohmann::json & () noexcept);
            PRO_DEF_MEMBER_DISPATCH(consume, nlohmann::json && () noexcept);
            PRO_DEF_MEMBER_DISPATCH(is_consumed, bool() noexcept);
            PRO_DEF_MEMBER_DISPATCH(ack, bool() noexcept);
        } // namespace msg

        PRO_DEF_FACADE(SigMsg, PRO_MAKE_DISPATCH_PACK(msg::msg_id, msg::evt, msg::payload, msg::consume, msg::is_consumed, msg::ack));

        using SigMsgCb = std::function<bool(pro::proxy<SigMsg> msg)>;

        namespace signal
        {   
            PRO_DEF_MEMBER_DISPATCH(send, asio::awaitable<nlohmann::json>(const close_chan & closer, bool ack, const std::string & evt, nlohmann::json && payload));
            PRO_DEF_MEMBER_DISPATCH(on_msg, std::uint64_t(SigMsgCb cb));
        } // namespace signal

        PRO_DEF_FACADE(Signal, PRO_MAKE_DISPATCH_PACK(signal::send, signal::on_msg));

    } // namespace spec

    namespace impl
    {
        class WSMsg;
        class WSAck;
        class WebsocketSignal;
    } // namespace impl

    struct WebsocketSignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout;
    };

    auto make_websocket_signal() -> pro::proxy<spec::Signal>;

    class WSMsg : public ImplBy<impl::WSMsg> {
    public:
        using ImplBy::ImplBy;
        std::uint64_t msg_id() const noexcept;
        std::string_view evt() const noexcept;
        const nlohmann::json & payload() const noexcept;
        nlohmann::json && consume() const noexcept;
        bool ack() const noexcept;
        bool is_consumed() const noexcept;
    };

    class WSAck : public ImplBy<impl::WSAck> {
    public:
        using ImplBy::ImplBy;
        const nlohmann::json & payload() const noexcept;
        nlohmann::json && consume() const noexcept;
        bool err() const noexcept;
        bool is_consumed() const noexcept;
    };

    class RoomedWebsocketSignal;
    class WebsocketSignal : public ImplBy<impl::WebsocketSignal> {
    private:
        WebsocketSignal(const WebsocketSignalConfigure & conf);
    public:
        WebsocketSignal(std::nullptr_t): ImplBy(std::shared_ptr<impl::WebsocketSignal>(nullptr)) {};
        static auto create(const WebsocketSignalConfigure & conf) {
            return pro::make_proxy<spec::Signal>(WebsocketSignal(conf));
        }
        auto send(const close_chan & closer, bool ack, const std::string & evt, nlohmann::json && payload) -> asio::awaitable<nlohmann::json>;
        std::uint64_t on_msg(WSMsgCb && cb);
    };

} // namespace cfgo


#endif