#ifndef _CFGO_CLIENT_HPP_
#define _CFGO_CLIENT_HPP_

#include "cfgo/asio.hpp"
#include "cfgo/alias.hpp"
#include "cfgo/async.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/utils.hpp"
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

    public:
        Client(const Configuration& config, const close_chan & closer = nullptr);
        Client(const Configuration& config, const CtxPtr& io_ctx, const close_chan & closer = nullptr);
        void set_sio_logs_default() const;
        void set_sio_logs_verbose() const;
        void set_sio_logs_quiet() const;
        std::optional<rtc::Description> peer_local_desc() const;
        std::optional<rtc::Description> peer_remote_desc() const;
        CtxPtr execution_context() const noexcept;
        close_chan get_closer() const noexcept;
        void init() const;
        [[nodiscard]] auto subscribe(const Pattern& pattern, const std::vector<std::string>& req_types, const close_chan & closer = nullptr) const -> asio::awaitable<SubPtr>;
        [[nodiscard]] auto unsubscribe(const std::string& sub_id, const close_chan & closer = nullptr) const -> asio::awaitable<cancelable<void>>;
        [[nodiscard]] auto send_custom_message_with_ack(const std::string & content, const std::string & to, const close_chan & close_chan) const -> asio::awaitable<cancelable<void>>;
        void send_custom_message_no_ack(const std::string & content, const std::string & to) const;
        std::uint32_t on_custom_message(std::function<bool(const std::string &, const std::string &, const std::string &, std::function<void()>)> cb) const;
        void off_custom_message(std::uint32_t cb_id) const;
    };
}

#endif