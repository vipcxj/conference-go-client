#ifndef _CFGO_CLIENT_HPP_
#define _CFGO_CLIENT_HPP_

#include "cfgo/asio.hpp"
#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/publication.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/signal.hpp"
#include "rtc/rtc.hpp"
namespace cfgo {
    namespace impl {
        struct Client;
    }

    class Client : ImplBy<impl::Client>
    {
    public:
        using Ptr = std::shared_ptr<Client>;

    public:
        Client(std::nullptr_t);
        Client(const Configuration& config, executor_factory_t executor_factory, close_chan closer = nullptr);
        executor_t executor() const noexcept;
        const strand_t & strand() const noexcept;
        close_chan get_closer() const noexcept;
        [[nodiscard]] auto connect(std::string socket_id, close_chan closer = nullptr) const -> asio::awaitable<void>;
        [[nodiscard]] auto subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan closer = nullptr) const -> asio::awaitable<SubPtr>;
        [[nodiscard]] auto unsubscribe(std::string sub_id, close_chan closer = nullptr) const -> asio::awaitable<void>;
        [[nodiscard]] auto publish(Publication pub, close_chan closer = nullptr) const -> asio::awaitable<void>;
        SignalPtr get_signal() const noexcept;
    };
}

#endif