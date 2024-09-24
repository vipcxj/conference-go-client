#include <assert.h>
#include <exception>
#include "cfgo/client.hpp"
#include "impl/client.hpp"

namespace cfgo
{
    Client::Client(const Configuration& config, const Strand & strand, const close_chan & closer) : ImplBy<impl::Client>(config, strand, closer) {}
    Client::Client(const Configuration& config, const IoCtxPtr & io_context, const close_chan & closer) : ImplBy<impl::Client>(config, io_context, closer) {}

    // void Client::set_sio_logs_default() const {
    //     impl()->set_sio_logs_default();
    // }
    // void Client::set_sio_logs_verbose() const {
    //     impl()->set_sio_logs_verbose();
    // }
    // void Client::set_sio_logs_quiet() const {
    //     impl()->set_sio_logs_quiet();
    // }

    // std::optional<rtc::Description> Client::peer_local_desc() const
    // {
    //     return impl()->peer_local_desc();
    // }

    // std::optional<rtc::Description> Client::peer_remote_desc() const
    // {
    //     return impl()->peer_remote_desc();
    // }

    auto Client::connect(const std::string & socket_id, const close_chan & closer) const -> asio::awaitable<void> {
        return impl()->connect(socket_id, closer);
    }

    auto Client::subscribe(const Pattern &pattern, const std::vector<std::string> &req_types, const close_chan & closer) const -> asio::awaitable<SubPtr> {
        return impl()->subscribe(pattern, req_types, closer);
    }

    auto Client::unsubscribe(const std::string& sub_id, const close_chan & closer) const -> asio::awaitable<void>
    {
        return impl()->unsubscribe(sub_id, closer);
    }

    // auto Client::send_custom_message_with_ack(const std::string & content, const std::string & to, const close_chan & closer) const -> asio::awaitable<cancelable<void>>
    // {
    //     return impl()->send_custom_message_with_ack(content, to, closer);
    // }

    // void Client::send_custom_message_no_ack(const std::string & content, const std::string & to) const
    // {
    //     impl()->send_custom_message_no_ack(content, to);
    // }

    // std::uint32_t Client::on_custom_message(std::function<bool(const std::string &, const std::string &, const std::string &, std::function<void()>)> cb) const
    // {
    //     return impl()->on_custom_message(cb);
    // }

    // void Client::off_custom_message(std::uint32_t cb_id) const
    // {
    //     impl()->off_custom_message(cb_id);
    // }

    auto Client::strand() const noexcept -> const Strand &
    {
        return impl()->strand();
    }

    close_chan Client::get_closer() const noexcept
    {
        return impl()->get_closer();
    }

    SignalPtr Client::get_signal() const noexcept
    {
        return impl()->get_signal();
    }
}
