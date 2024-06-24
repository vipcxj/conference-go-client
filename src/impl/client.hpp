#ifndef _CFGO_IMPL_CLIENT_HPP_
#define _CFGO_IMPL_CLIENT_HPP_

#include "cfgo/config/configuration.h"
#include "cfgo/alias.hpp"
#include "cfgo/asio.hpp"
#include "cfgo/async.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/log.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/client.hpp"
#include "cfgo/signal.hpp"
#include "cfgo/webrtc.hpp"
#include <mutex>
#include <optional>
#include <map>
#include <atomic>

namespace rtc
{
    class PeerConnection;
} // namespace rtc


namespace cfgo {
    namespace impl {
        class Client : public std::enable_shared_from_this<Client>
        {
        public:
            using Ptr = std::shared_ptr<Client>;
            using IoCtxPtr = cfgo::Client::IoCtxPtr;
            using Strand = cfgo::Client::Strand;

        private:
            Logger m_logger;
            Configuration m_config;
            cfgo::SignalPtr m_signal;
            cfgo::WebrtcUPtr m_webrtc;
            close_chan m_closer;
            IoCtxPtr m_io_ctx;
            Strand m_strand;
        public:
            Client() = delete;
            Client(const Configuration& config, const IoCtxPtr & io_context, close_chan closer);
            Client(Client&&) = default;
            ~Client();
            Client(const Client&) = delete;
            Client& operator = (Client&) = delete;
            [[nodiscard]] auto subscribe(Pattern pattern, std::vector<std::string> req_types, const close_chan & close_chan) -> asio::awaitable<SubPtr>;
            [[nodiscard]] auto unsubscribe(const std::string& sub_id, const close_chan & close_chan) -> asio::awaitable<void>;

            const Strand & strand() const noexcept;
            close_chan get_closer() const noexcept;
            SignalPtr get_signal() const noexcept;
        };
    }
}

#endif