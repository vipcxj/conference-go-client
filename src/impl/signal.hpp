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

        namespace signal
        {
            namespace raw {
                namespace msg
                {
                    PRO_DEF_MEMBER_DISPATCH(msg_id, std::uint64_t() noexcept);
                    PRO_DEF_MEMBER_DISPATCH(evt, std::string_view() noexcept);
                    PRO_DEF_MEMBER_DISPATCH(payload, const nlohmann::json & () noexcept);
                    PRO_DEF_MEMBER_DISPATCH(consume, nlohmann::json && () noexcept);
                    PRO_DEF_MEMBER_DISPATCH(is_consumed, bool() noexcept);
                    PRO_DEF_MEMBER_DISPATCH(ack, bool() noexcept);
                } // namespace msg
            } // namespace raw
        } // namespace signal

        PRO_DEF_FACADE(RawSigMsg, PRO_MAKE_DISPATCH_PACK(
            signal::raw::msg::msg_id, 
            signal::raw::msg::evt, 
            signal::raw::msg::payload, 
            signal::raw::msg::consume, 
            signal::raw::msg::is_consumed, 
            signal::raw::msg::ack
        ), pro::copyable_ptr_constraints);

        using RawSigMsgCb = std::function<bool(pro::proxy<RawSigMsg> msg)>;

        namespace signal {
            namespace raw {
                PRO_DEF_MEMBER_DISPATCH(send, asio::awaitable<nlohmann::json>(const close_chan & closer, bool ack, const std::string & evt, nlohmann::json && payload));
                PRO_DEF_MEMBER_DISPATCH(on_msg, std::uint64_t(RawSigMsgCb cb));
            }
        }

        PRO_DEF_FACADE(RawSignal, PRO_MAKE_DISPATCH_PACK(
            signal::raw::send, 
            signal::raw::on_msg
        ), pro::copyable_ptr_constraints);

    } // namespace spec

    struct WebsocketSignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout;
    };

    auto make_websocket_raw_signal(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal>;

    

} // namespace cfgo


#endif