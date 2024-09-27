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

    auto Client::connect(std::string socket_id, close_chan closer) const -> asio::awaitable<void> {
        return impl()->connect(std::move(socket_id), std::move(closer));
    }

    auto Client::subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan closer) const -> asio::awaitable<SubPtr> {
        return impl()->subscribe(std::move(pattern), std::move(req_types), std::move(closer));
    }

    auto Client::unsubscribe(std::string sub_id, close_chan closer) const -> asio::awaitable<void>
    {
        return impl()->unsubscribe(std::move(sub_id), std::move(closer));
    }

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
