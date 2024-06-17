#include "impl/signal.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/websocket.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/url.hpp"
#include "cfgo/fmt.hpp"

#include <deque>

namespace cfgo
{
    namespace impl
    {
        namespace beast = boost::beast;
        namespace http = beast::http;
        namespace websocket = beast::websocket;
        namespace urls = boost::urls;
        using tcp = boost::asio::ip::tcp;
        using Websocket = decltype(asio::use_awaitable.as_default_on(websocket::stream<beast::tcp_stream>()));

        enum WsMsgFlag {
            WS_MSG_FLAG_IS_ACK_NORMAL = 0,
            WS_MSG_FLAG_IS_ACK_ERR = 1,
            WS_MSG_FLAG_NEED_ACK = 2,
            WS_MSG_FLAG_NO_ACK = 4
        };

        struct WSMessage {
            std::uint64_t msg_id;
            std::string payload;
            bool ack;
            bool consumed;
            std::chrono::steady_clock::time_point timestamp;
        };

        struct WSAck {
            std::string payload;
            bool err;
            bool consumed;
            std::chrono::steady_clock::time_point timestamp;
        };

        class WebsocketSignal : public std::enable_shared_from_this<WebsocketSignal> {
        private:
            WebsocketSignalConfigure m_config;
            const std::string m_id;
            std::optional<Websocket> m_ws;
            std::uint64_t m_next_msg_id;
            std::unordered_map<std::string, std::deque<WSMessage>> m_buffered_msgs;
            std::unordered_map<std::uint64_t, std::deque<WSAck>> m_buffered_acks;

            auto make_sure_connect() -> asio::awaitable<void>;
        public:
            WebsocketSignal(const WebsocketSignalConfigure & conf):
                m_config(conf), m_ws(std::nullopt),
                m_id(boost::uuids::to_string(boost::uuids::random_generator()())),
                m_next_msg_id(1)
            {}
            auto run() -> asio::awaitable<void>;
            auto send(close_chan closer, bool ack, std::string evt, std::string payload) -> asio::awaitable<std::optional<std::string>>;
            auto sendCustom(close_chan closer, bool ack, std::string evt, std::string payload, std::string room, std::string to) -> asio::awaitable<void>;
        };

        auto WebsocketSignal::make_sure_connect() -> asio::awaitable<void> {
            auto self = shared_from_this();
            if (!m_ws || !m_ws->is_open())
            {
                m_ws.emplace(asio::use_awaitable.as_default_on(
                    Websocket(co_await asio::this_coro::executor)
                ));
            } else {
                co_return;
            }
            auto & ws = *m_ws;
            auto resolver = asio::use_awaitable.as_default_on(
                tcp::resolver(co_await asio::this_coro::executor)
            );
            urls::url parsed_url = urls::parse_uri(m_config.url).value();
            std::string host, port;
            host = parsed_url.host();
            port = parsed_url.port();
            if (host.empty())
            {
                host = "localhost";
                parsed_url.set_host(host);
            }
            if (port.empty())
            {
                auto scheme = parsed_url.scheme();
                if (scheme == "wss" || scheme == "https") {
                    port = "443";
                } else {
                    port = "80";
                }
                parsed_url.set_port(port);
            }
            auto const results = co_await resolver.async_resolve(host, port);
            beast::get_lowest_layer(ws).expires_after(std::chrono::seconds{30});
            auto ep = co_await beast::get_lowest_layer(ws).async_connect(results);
            // Update the host_ string. This will provide the value of the
            // Host HTTP header during the WebSocket handshake.
            // See https://tools.ietf.org/html/rfc7230#section-5.4
            parsed_url.set_port_number(ep.port());
            // Turn off the timeout on the tcp_stream, because
            // the websocket stream has its own timeout system.
            beast::get_lowest_layer(ws).expires_never();
            // Set suggested timeout settings for the websocket
            ws.set_option(
                websocket::stream_base::timeout::suggested(
                    beast::role_type::client
                )
            );
            // Set a decorator to change the User-Agent of the handshake
            ws.set_option(websocket::stream_base::decorator(
                [this](websocket::request_type& req)
                {
                    req.set(http::field::user_agent,
                        std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-client-coro");
                    req.set(http::field::authorization, m_config.token);
                    req.set("Signal-Id", m_id);
                }));
            co_await ws.async_handshake(parsed_url.encoded_host_and_port(), parsed_url.path());
        }

        auto WebsocketSignal::run() -> asio::awaitable<void> {
            auto self = shared_from_this();
            co_await make_sure_connect();
            while (true) {
                beast::flat_buffer buffer;
                co_await m_ws->async_read(buffer);
                std::string_view evt;
                std::uint64_t msg_id;
                WsMsgFlag flag;
                std::string_view payload;
                decodeWsTextData(buffer, evt, msg_id, flag, payload);
                if (flag == WS_MSG_FLAG_IS_ACK_ERR || flag == WS_MSG_FLAG_IS_ACK_NORMAL) {
                    auto [iter, _] = m_buffered_acks.try_emplace(msg_id);
                    iter->second.emplace_back(std::string(payload), flag == WS_MSG_FLAG_IS_ACK_ERR, false, std::chrono::steady_clock::now());
                } else {
                    auto [iter, _] = m_buffered_msgs.try_emplace(std::string(evt));
                    iter->second.emplace_back(msg_id, std::string(payload), flag == WS_MSG_FLAG_NEED_ACK, false, std::chrono::steady_clock::now());
                }
            }
        }

        asio::const_buffer encodeWsTextData(const std::string & evt, const std::uint64_t & msg_id, WsMsgFlag flag, const std::string_view & payload) {
            return asio::buffer(fmt::format("{};{};{};{}", evt, msg_id, (int)flag, payload));
        }

        void decodeWsTextData(const beast::flat_buffer & buffer, std::string_view & evt, std::uint64_t & msg_id, WsMsgFlag & flag, std::string_view & payload) {
             const char * data = (const char *) buffer.data().data();
             // init -> evt -> msg_id -> flag -> payload
             int stage = 0;
             int pos = 0;
             for(int i = 0; i < buffer.size(); ++i) {
                if (data[i] == ';')
                {
                    switch (stage)
                    {
                    case 0:
                        evt = std::string_view(data + pos, i);
                        pos = i + 1;
                        stage = 1;
                        break;
                    case 1:
                        msg_id = std::stoull(std::string(data + pos, i));
                        pos = i + 1;
                        stage = 2;
                        break;
                    case 2:
                        flag = (WsMsgFlag) std::stoi(std::string(data + pos, i));
                        pos = i + 1;
                        stage = 3;
                        break;
                    }
                }
                if (stage == 3)
                {
                    break;
                }
             }
             if (pos < buffer.size())
             {
                payload = std::string_view(data + pos, buffer.size() - pos);
             }
        }

        auto WebsocketSignal::send(close_chan closer, bool ack, std::string evt, std::string payload) -> asio::awaitable<std::optional<std::string>> {
            auto self = shared_from_this();
            make_sure_connect();
            m_next_msg_id += 2;
            auto msg_id = m_next_msg_id;
            auto buffer = encodeWsTextData(evt, msg_id, ack ? WS_MSG_FLAG_NEED_ACK : WS_MSG_FLAG_NO_ACK, payload);
            co_await m_ws->async_write(buffer);
            if (ack)
            {
                m_ws->async_read()
            }
            
        }
    } // namespace impl
    
} // namespace cfgo
