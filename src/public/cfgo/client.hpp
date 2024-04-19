#ifndef _CFGO_CLIENT_HPP_
#define _CFGO_CLIENT_HPP_

#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/utils.hpp"
#include "asio.hpp"
#include "rtc/rtc.hpp"
#include "asiochan/asiochan.hpp"
namespace cfgo {
    namespace impl {
        struct Client;
    }

    class Client : ImplBy<impl::Client>
    {
    public:
        using Ptr = std::shared_ptr<Client>;
        using CtxPtr = std::shared_ptr<asio::execution_context>;

    public:
        Client(const Configuration& config, close_chan closer = INVALID_CLOSE_CHAN);
        Client(const Configuration& config, const CtxPtr& io_ctx, close_chan closer = INVALID_CLOSE_CHAN);
        void set_sio_logs_default();
        void set_sio_logs_verbose();
        void set_sio_logs_quiet();
        std::optional<rtc::Description> peer_local_desc() const;
        std::optional<rtc::Description> peer_remote_desc() const;
        CtxPtr execution_context() const noexcept;
        close_chan get_closer() const noexcept;
        [[nodiscard]] asio::awaitable<SubPtr> subscribe(const Pattern& pattern, const std::vector<std::string>& req_types, close_chan& close_chan = INVALID_CLOSE_CHAN);
    };
}

#endif