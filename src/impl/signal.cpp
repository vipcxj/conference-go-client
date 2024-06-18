#include "impl/signal.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/websocket.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/url.hpp"
#include "cfgo/fmt.hpp"
#include "cfgo/error.hpp"
#include "impl/message.hpp"

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

        class WSMsg {
        private:
            std::uint64_t m_msg_id;
            std::string m_evt;
            nlohmann::json m_payload;
            bool m_ack;
            bool m_consumed {false};
            WSMsg(std::uint64_t msg_id, const std::string_view & evt, nlohmann::json && payload, bool ack):
                m_msg_id(msg_id),
                m_evt(evt),
                m_payload(std::move(payload)),
                m_ack(ack)
            {}
        public:
            static auto create(std::uint64_t msg_id, const std::string_view & evt, nlohmann::json && payload, bool ack) -> pro::proxy<spec::RawSigMsg> {
                struct make_unique_enabler : public WSMsg {
                    using WSMsg::WSMsg;
                };
                pro::proxy<spec::RawSigMsg> p = std::make_shared<make_unique_enabler>(msg_id, evt, std::move(payload), ack);
                return p;
            }
            std::uint64_t msg_id() const noexcept {
                return m_msg_id;
            }
            std::string_view evt() const noexcept {
                return m_evt;
            }
            const nlohmann::json & payload() const noexcept {
                return m_payload;
            }
            nlohmann::json && consume() noexcept {
                m_consumed = true;
                return std::move(m_payload);
            }
            bool ack() const noexcept {
                return m_ack;
            }
            bool is_consumed() const noexcept {
                return m_consumed;
            }
        };

        class WSAck {
        private:
            nlohmann::json m_payload;
            bool m_err;
            bool m_consumed {false};
        public:
            WSAck(nlohmann::json && payload, bool err): m_payload(std::move(payload)), m_err(err) {}
            const nlohmann::json & payload() const noexcept {
                return m_payload;
            }
            nlohmann::json && consume() noexcept {
                m_consumed = true;
                return std::move(m_payload);
            }
            bool err() const noexcept {
                return m_err;
            }
            bool is_consumed() const noexcept {
                return m_consumed;
            }
        };

        class WebsocketRawSignal : public std::enable_shared_from_this<WebsocketRawSignal> {
        private:
            using WSMsgChan = asiochan::channel<std::shared_ptr<WSMsg>, 1>;
            using WSMsgChans = std::vector<WSMsgChan>;
            using WSAckChan = unique_chan<WSAck>;

            WebsocketSignalConfigure m_config;
            const std::string m_id {boost::uuids::to_string(boost::uuids::random_generator()())};
            std::optional<Websocket> m_ws {std::nullopt};
            std::uint64_t m_next_msg_id {1};
            std::uint64_t m_next_msg_cb_id {0};
            std::unordered_map<std::uint64_t, spec::RawSigMsgCb> m_msg_cbs {};
            std::unordered_map<std::uint64_t, WSAckChan> m_ack_chs {};

            WebsocketRawSignal(const WebsocketSignalConfigure & conf):
                m_config(conf)
            {}
            auto make_sure_connect() -> asio::awaitable<void>;
        public:
            static auto create(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal> {
                struct make_unique_enabler : public WebsocketRawSignal {
                    using WebsocketRawSignal::WebsocketRawSignal;
                };
                pro::proxy<spec::RawSignal> p = std::make_shared<make_unique_enabler>(conf);
                return p;
            }
            auto run() -> asio::awaitable<void>;
            auto send(close_chan closer, bool ack, std::string evt, nlohmann::json payload) -> asio::awaitable<nlohmann::json>;
            std::uint64_t on_msg(spec::RawSigMsgCb cb);
            void off_msg(std::uint64_t id);
        };

        auto WebsocketRawSignal::make_sure_connect() -> asio::awaitable<void> {
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

        auto WebsocketRawSignal::run() -> asio::awaitable<void> {
            auto self = shared_from_this();
            co_await make_sure_connect();
            while (true) {
                beast::flat_buffer buffer;
                co_await m_ws->async_read(buffer);
                std::string_view evt;
                std::uint64_t msg_id;
                WsMsgFlag flag;
                std::string_view svPayload;
                decodeWsTextData(buffer, evt, msg_id, flag, svPayload);
                auto payload = nlohmann::json::parse(svPayload);
                if (flag == WS_MSG_FLAG_IS_ACK_ERR || flag == WS_MSG_FLAG_IS_ACK_NORMAL) {
                    WSAck msg(std::move(payload), flag == WS_MSG_FLAG_IS_ACK_ERR);
                    auto ch_iter = m_ack_chs.find(msg_id);
                    if (ch_iter != m_ack_chs.end()) {
                        chan_must_write(ch_iter->second, std::move(msg));
                        m_ack_chs.erase(ch_iter);
                    }
                } else {
                    auto msg = WSMsg::create(msg_id, evt, std::move(payload), flag == WS_MSG_FLAG_NEED_ACK);
                    for(auto it = m_msg_cbs.begin(); it != m_msg_cbs.end();) {
                        if (!it->second(msg)) {
                            it = m_msg_cbs.erase(it);
                        } else {
                            ++ it;
                        }
                        if (msg.is_consumed())
                        {
                            break;
                        }
                    }
                }
            }
        }

        auto WebsocketRawSignal::send(close_chan closer, bool ack, std::string evt, nlohmann::json payload) -> asio::awaitable<nlohmann::json> {
            auto self = shared_from_this();
            make_sure_connect();
            m_next_msg_id += 2;
            auto msg_id = m_next_msg_id;
            auto payload_str = payload.dump();
            auto buffer = encodeWsTextData(evt, msg_id, ack ? WS_MSG_FLAG_NEED_ACK : WS_MSG_FLAG_NO_ACK, payload_str);
            WSAckChan ch {};
            if (ack) {
                m_ack_chs.insert(std::make_pair(msg_id, ch));
            }
            co_await m_ws->async_write(buffer);
            if (ack) {
                auto && ack = co_await chan_read_or_throw<WSAck>(ch, closer);
                if (ack.err()) {
                    ServerErrorObject error {};
                    nlohmann::from_json(ack.consume(), error);
                    throw ServerError(std::move(error));
                } else {
                    co_return ack.consume();
                }
            } else {
                co_return nlohmann::json {nullptr};
            }
        }

        std::uint64_t WebsocketRawSignal::on_msg(spec::RawSigMsgCb cb) {
            auto cb_id = m_next_msg_cb_id;
            m_next_msg_cb_id ++;
            m_msg_cbs.insert(std::make_pair(cb_id, std::move(cb)));
        }

        void WebsocketRawSignal::off_msg(std::uint64_t id) {
            m_msg_cbs.erase(id);
        }

        class Signal : public std::enable_shared_from_this<Signal> {
        private:
            pro::proxy<spec::RawSignal> m_raw_signal;
            std::uint32_t m_next_custom_msg_id {0};
        public:
            auto send_message(close_chan closer, bool ack, std::string evt, nlohmann::json payload, std::string room, std::string to) -> asio::awaitable<nlohmann::json>;
        };

        auto Signal::send_message(close_chan closer, bool ack, std::string evt, nlohmann::json payload, std::string room, std::string to) -> asio::awaitable<nlohmann::json> {
            auto self = shared_from_this();
            auto msg_id = m_next_custom_msg_id;
            m_next_custom_msg_id ++;
            msg::CustomMessage msg {
                .router = msg::Router {.room = room, .userTo = to},
                .content = payload.dump(),
                .msgId = msg_id,
                .ack = ack,
            };
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, msg);
            unique_chan<nlohmann::json> ack_ch {};
            m_raw_signal.on_msg([msg_id, ack_ch](pro::proxy<spec::RawSigMsg> msg) -> bool {
                if (msg.evt() == "custom-ack" && msg.msg_id() == msg_id)
                {
                    chan_must_write(ack_ch, msg.consume());
                    return true;
                }
                return false;
            });
            co_await m_raw_signal.send(closer, false, "custom:" + evt, std::move(js_msg));
            if (ack) {
                co_return co_await chan_read_or_throw<nlohmann::json>(ack_ch, closer);
            } else {
                co_return nlohmann::json { nullptr };
            }
        }
    } // namespace impl

    auto make_websocket_raw_signal(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal> {
        return impl::WebsocketRawSignal::create(conf);
    }
    
} // namespace cfgo
