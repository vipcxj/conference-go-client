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
    using SignalRawMsg = std::vector<std::byte>;
    namespace spec
    {
        struct RoomedSignal;
        namespace signal
        {
            PRO_DEF_MEMBER_DISPATCH(roomed, pro::proxy<RoomedSignal>(const std::string & room));
            PRO_DEF_MEMBER_DISPATCH(send, asio::awaitable<std::optional<SignalRawMsg>>(const close_chan & closer, bool ack, const std::string & evt, const SignalRawMsg & payload));
            PRO_DEF_MEMBER_DISPATCH(sendCustom, asio::awaitable<void>(const close_chan & closer, bool ack, const std::string & evt, const std::string & payload, const std::string & room, const std::string & to));
        } // namespace signal

        namespace rsignal
        {
            PRO_DEF_MEMBER_DISPATCH(sendCustom, asio::awaitable<void>(const close_chan & closer, bool ack, const std::string & evt, const std::string & payload, const std::string & to));
        } // namespace rsignal

        PRO_DEF_FACADE(Signal, PRO_MAKE_DISPATCH_PACK(signal::roomed, signal::sendCustom));

        PRO_DEF_FACADE(RoomedSignal, PRO_MAKE_DISPATCH_PACK(rsignal::sendCustom));

    } // namespace spec

    namespace impl
    {
        class WebsocketSignal;
    } // namespace impl

    struct WebsocketSignalConfigure {
        std::string url;
        std::string token;
        duration_t ready_timeout;
    };

    class RoomedWebsocketSignal;
    class WebsocketSignal : public ImplBy<impl::WebsocketSignal> {
    public:
        WebsocketSignal(const WebsocketSignalConfigure & conf);
        inline auto roomed(const std::string & room) -> pro::proxy<spec::RoomedSignal> {
            return pro::make_proxy<spec::RoomedSignal>(RoomedWebsocketSignal { room, *this });
        }
        auto send(const close_chan & closer, bool ack, const std::string & evt, const SignalRawMsg & payload) -> asio::awaitable<std::optional<SignalRawMsg>>;
        auto sendCustom(const close_chan & closer, bool ack, const std::string & evt, const std::string & payload, const std::string & room, const std::string & to) -> asio::awaitable<void>;
    };

    class RoomedWebsocketSignal {
    private:
        std::string m_room;
        WebsocketSignal m_signal;
    public:
        RoomedWebsocketSignal(const std::string & room, const WebsocketSignal & signal): m_room(room), m_signal(signal) {}
        inline auto sendCustom(const close_chan & closer, bool ack, const std::string & evt, const std::string & payload, const std::string & room, const std::string & to) -> asio::awaitable<void> {
            return this->m_signal.sendCustom(closer, ack, evt, payload, this->m_room, to);
        }
    };

} // namespace cfgo


#endif