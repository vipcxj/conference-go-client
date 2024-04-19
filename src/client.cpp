#include <assert.h>
#include <exception>
#include "cfgo/client.hpp"
#include "impl/client.hpp"

namespace cfgo
{
    Client::Client(const Configuration& config, close_chan closer) : ImplBy<impl::Client>(config, closer) {}
    Client::Client(const Configuration& config, const CtxPtr& io_ctx, close_chan closer) : ImplBy<impl::Client>(config, io_ctx, closer) {}

    void Client::set_sio_logs_default() {
        impl()->set_sio_logs_default();
    }
    void Client::set_sio_logs_verbose() {
        impl()->set_sio_logs_verbose();
    }
    void Client::set_sio_logs_quiet() {
        impl()->set_sio_logs_quiet();
    }

    std::optional<rtc::Description> Client::peer_local_desc() const
    {
        return impl()->peer_local_desc();
    }

    std::optional<rtc::Description> Client::peer_remote_desc() const
    {
        return impl()->peer_remote_desc();
    }

    auto Client::subscribe(const Pattern &pattern, const std::vector<std::string> &req_types, close_chan &close_chan) -> asio::awaitable<SubPtr> {
        return impl()->subscribe(pattern, req_types, close_chan);
    }

    Client::CtxPtr Client::execution_context() const noexcept
    {
        return impl()->execution_context();
    }

    close_chan Client::get_closer() const noexcept
    {
        return impl()->get_closer();
    }
}
