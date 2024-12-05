#include <assert.h>
#include <exception>
#include "cfgo/client.hpp"
#include "impl/client.hpp"

namespace cfgo
{
    Client::Client(std::nullptr_t): ImplBy<impl::Client>(nullptr) {}
    Client::Client(const Configuration& config, executor_factory_t executor_factory, close_chan closer) : ImplBy<impl::Client>(config, std::move(executor_factory), std::move(closer)) {}

    // std::optional<rtc::Description> Client::peer_local_desc() const
    // {
    //     return impl()->peer_local_desc();
    // }

    // std::optional<rtc::Description> Client::peer_remote_desc() const
    // {
    //     return impl()->peer_remote_desc();
    // }

    auto Client::connect(std::string socket_id, close_chan closer) const -> asio::awaitable<void> {
        return make_sure_impl()->connect(std::move(socket_id), std::move(closer));
    }

    auto Client::subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan closer) const -> asio::awaitable<SubPtr> {
        return make_sure_impl()->subscribe(std::move(pattern), std::move(req_types), std::move(closer));
    }

    auto Client::unsubscribe(std::string sub_id, close_chan closer) const -> asio::awaitable<void>
    {
        return make_sure_impl()->unsubscribe(std::move(sub_id), std::move(closer));
    }

    auto Client::publish(Publication pub, close_chan closer) const -> asio::awaitable<void>
    {
        return make_sure_impl()->publish(std::move(pub), std::move(closer));
    }

    auto Client::executor() const noexcept -> executor_t
    {
        return make_sure_impl()->executor();
    }

    auto Client::strand() const noexcept -> const strand_t &
    {
        return make_sure_impl()->strand();
    }

    close_chan Client::get_closer() const noexcept
    {
        return make_sure_impl()->get_closer();
    }

    SignalPtr Client::get_signal() const noexcept
    {
        return make_sure_impl()->get_signal();
    }
}
