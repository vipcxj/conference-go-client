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

        private:
            Logger m_logger;
            Configuration m_config;
            cfgo::SignalPtr m_signal;
            cfgo::WebrtcPtr m_webrtc;
            close_chan m_closer;
            executor_factory_t m_executor_factory;
            strand_t m_strand;
        public:
            Client() = delete;
            Client(const Configuration& config, executor_factory_t executor_factory, close_chan closer);
            Client(Client&&) = default;
            ~Client();
            Client(const Client&) = delete;
            Client& operator = (Client&) = delete;
            [[nodiscard]] auto connect(std::string socket_id, close_chan closer) -> asio::awaitable<void>;
            [[nodiscard]] auto subscribe(Pattern pattern, std::vector<std::string> req_types, close_chan close_chan) -> asio::awaitable<SubPtr>;
            [[nodiscard]] auto unsubscribe(std::string sub_id, close_chan close_chan) -> asio::awaitable<void>;
            [[nodiscard]] auto publish(cfgo::Publication pub, close_chan closer) const -> asio::awaitable<void>;

            executor_t executor() const noexcept;
            const strand_t & strand() const noexcept;
            close_chan get_closer() const noexcept;
            SignalPtr get_signal() const noexcept;
        };
    }
}

#endif