#ifndef _CFGO_CLIENT_HPP_
#define _CFGO_CLIENT_HPP_

#include "cfgo/asio.hpp"
#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/configuration.hpp"
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
        using CtxPtr = std::shared_ptr<asio::execution_context>;
        using IoCtxPtr = std::shared_ptr<asio::io_context>;
        using Strand = StandardStrand;

    public:
        Client(const Configuration& config, const Strand & strand, const close_chan & closer = nullptr);
        Client(const Configuration& config, const IoCtxPtr & io_context, const close_chan & closer = nullptr);
        const Strand & strand() const noexcept;
        close_chan get_closer() const noexcept;
        [[nodiscard]] auto connect(std::string socket_id, close_chan closer = nullptr) const -> asio::awaitable<void>;
        [[nodiscard]] auto subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan closer = nullptr) const -> asio::awaitable<SubPtr>;
        [[nodiscard]] auto unsubscribe(std::string sub_id, close_chan closer = nullptr) const -> asio::awaitable<void>;
        SignalPtr get_signal() const noexcept;
    };
}

#endif