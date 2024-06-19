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

        class WebsocketRawSignal;
        class WSAcker;
        class WSFakeAcker;

        asio::const_buffer encodeWsTextData(const std::string_view & evt, const std::uint64_t & msg_id, WsMsgFlag flag, const std::string_view & payload) {
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

        class WSMsg {
        private:
            std::uint64_t m_msg_id;
            std::string m_evt;
            nlohmann::json m_payload;
            bool m_ack;
        public:
            WSMsg(std::uint64_t msg_id, const std::string_view & evt, nlohmann::json && payload, bool ack):
                m_msg_id(msg_id),
                m_evt(evt),
                m_payload(std::move(payload)),
                m_ack(ack)
            {}
            static auto create(std::uint64_t msg_id, const std::string_view & evt, nlohmann::json && payload, bool ack) -> pro::proxy<spec::RawSigMsg> {
                pro::proxy<spec::RawSigMsg> p = std::make_shared<WSMsg>(msg_id, evt, std::move(payload), ack);
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
            bool ack() const noexcept {
                return m_ack;
            }
        };

        class WSAcker {
        private:
            std::weak_ptr<WebsocketRawSignal> m_signal;
            std::uint64_t m_msg_id;
        public:
            WSAcker(std::weak_ptr<WebsocketRawSignal> signal, std::uint64_t msg_id): m_signal(signal), m_msg_id(msg_id) {}
            static auto create(std::weak_ptr<WebsocketRawSignal> signal, std::uint64_t msg_id) {
                pro::proxy<spec::RawSigAcker> p = std::make_shared<WSAcker>(signal, msg_id);
                return p;
            }
            auto ack(nlohmann::json payload) -> asio::awaitable<void> {
                if (auto signal = m_signal.lock())
                {
                    auto payload_str = payload.dump();
                    auto buffer = encodeWsTextData("", m_msg_id, WS_MSG_FLAG_IS_ACK_NORMAL, payload_str);
                    co_await signal->m_ws->async_write(buffer);
                }
            }
            auto ack_err(std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> {
                if (auto signal = m_signal.lock())
                {
                    nlohmann::json payload {};
                    nlohmann::to_json(payload, *eo);
                    auto payload_str = payload.dump();
                    auto buffer = encodeWsTextData("", m_msg_id, WS_MSG_FLAG_IS_ACK_ERR, payload_str);
                    co_await signal->m_ws->async_write(buffer);
                }
            }
        };

        struct WSFakeAcker {
            auto ack(nlohmann::json payload) -> asio::awaitable<void> {
                co_return;
            }
            auto ack_err(std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> {
                co_return;
            }
        };

        auto make_acker(std::weak_ptr<WebsocketRawSignal> signal, bool ack, std::uint64_t msg_id) {
            if (ack) {
                return WSAcker::create(signal, msg_id);
            } else {
                return pro::make_proxy<spec::RawSigAcker>(WSFakeAcker{});
            }
        }

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
        public:
            WebsocketRawSignal(const WebsocketSignalConfigure & conf):
                m_config(conf)
            {}
            static auto create(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal> {
                pro::proxy<spec::RawSignal> p = std::make_shared<WebsocketRawSignal>(conf);
                return p;
            }
            auto create_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> pro::proxy<spec::RawSigMsg> {
                auto msg_id = m_next_msg_id;
                m_next_msg_id += 2;
                return WSMsg::create(msg_id, evt, std::move(payload), ack);
            }
            auto connect(close_chan closer) -> asio::awaitable<void>;
            auto run(close_chan closer) -> asio::awaitable<void>;
            auto send(close_chan closer, pro::proxy<spec::RawSigMsg> msg) -> asio::awaitable<nlohmann::json>;
            std::uint64_t on_msg(spec::RawSigMsgCb cb);
            void off_msg(std::uint64_t id);

            friend class WSAcker;
        };

        auto WebsocketRawSignal::connect(close_chan closer) -> asio::awaitable<void> {
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

        auto WebsocketRawSignal::run(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            closer = closer.create_child();
            asio::co_spawn(executor, fix_async_lambda([closer, weak_self = weak_from_this()]() -> asio::awaitable<void> {
                co_await closer.await();
                if (auto self = weak_self.lock())
                {
                    self->m_ws->next_layer().close();
                }
                co_return;
            }), asio::detached);
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
                    auto acker = make_acker(weak_from_this(), flag == WS_MSG_FLAG_NEED_ACK, msg_id);
                    for(auto it = m_msg_cbs.begin(); it != m_msg_cbs.end();) {
                        if (!it->second(msg, acker)) {
                            it = m_msg_cbs.erase(it);
                        } else {
                            ++ it;
                        }
                    }
                }
            }
        }

        auto WebsocketRawSignal::send(close_chan closer, pro::proxy<spec::RawSigMsg> msg) -> asio::awaitable<nlohmann::json> {
            auto self = shared_from_this();
            auto payload_str = msg.payload().dump();
            auto buffer = encodeWsTextData(msg.evt(), msg.msg_id(), msg.ack() ? WS_MSG_FLAG_NEED_ACK : WS_MSG_FLAG_NO_ACK, payload_str);
            WSAckChan ch {};
            if (msg.ack()) {
                m_ack_chs.insert(std::make_pair(msg.msg_id(), ch));
            }
            co_await m_ws->async_write(buffer);
            if (msg.ack()) {
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

        class SignalMsg {
            std::string m_evt;
            bool m_ack;
            std::string m_room;
            std::string m_user;
            std::string m_payload;
            std::uint32_t m_msg_id;
        public:
            SignalMsg(std::string_view evt, std::string_view room, std::string_view to, std::string && payload, std::uint32_t msg_id):
                m_evt(evt),
                m_room(room),
                m_user(to),
                m_payload(std::move(payload)),
                m_msg_id(msg_id)
            {}
            std::string_view evt() const noexcept {
                return m_evt;
            }
            bool ack() const noexcept {
                return m_ack;
            }
            std::string_view room() const noexcept {
                return m_room;
            }
            std::string_view user() const noexcept {
                return m_user;
            }
            std::string_view payload() const noexcept {
                return m_payload;
            }
            std::uint32_t msg_id() const noexcept {
                return m_msg_id;
            }
            std::string && consume() noexcept {
                return std::move(m_payload);
            }
        };

        class Signal;
        class SignalAcker : public std::enable_shared_from_this<SignalAcker> {
            std::weak_ptr<Signal> m_signal;
            SignalMsg m_msg;
        private:
            SignalAcker(std::weak_ptr<Signal> signal, SignalMsg msg):
                m_signal(std::move(signal)),
                m_msg(std::move(msg))
            {}
            auto ack(close_chan closer, std::string payload) const -> asio::awaitable<void> {
                auto self = shared_from_this();
                if (!self->m_msg.ack())
                {
                    co_return;
                }
                if (auto signal = self->m_signal.lock())
                {
                    msg::CustomAckMessage msg {
                        .router = msg::Router {
                            .room = std::string(m_msg.room()),
                            .userTo = std::string(m_msg.user()),
                        },
                        .msgId = m_msg.msg_id(),
                        .content = payload,
                        .err = false,
                    };
                    nlohmann::json raw_payload;
                    nlohmann::from_json(raw_payload, msg);
                    auto raw_msg = signal->m_raw_signal.create_msg("custom-ack", raw_payload, false);
                    co_await signal->m_raw_signal.send(closer, m_msg.evt(), std::move(raw_msg));
                }
            }
            auto ack_err(close_chan closer, std::unique_ptr<ServerErrorObject> err) const -> asio::awaitable<void> {
                auto self = shared_from_this();
                if (!self->m_msg.ack())
                {
                    co_return;
                }
                if (auto signal = m_signal.lock())
                {
                    nlohmann::json js_err;
                    nlohmann::to_json(js_err, *err);
                    msg::CustomAckMessage msg {
                        .router = msg::Router {
                            .room = std::string(m_msg.room()),
                            .userTo = std::string(m_msg.user()),
                        },
                        .msgId = m_msg.msg_id(),
                        .content = js_err.dump(),
                        .err = true,
                    };
                    nlohmann::json raw_payload;
                    nlohmann::from_json(raw_payload, msg);
                    auto raw_msg = signal->m_raw_signal.create_msg("custom-ack", raw_payload, false);
                    co_await signal->m_raw_signal.send(closer, m_msg.evt(), std::move(raw_msg));
                }
            }
        };

        struct CustomAckKey {
            std::uint64_t id;
            std::string room;
            std::string user;
        };

        class Signal : public std::enable_shared_from_this<Signal> {
        private:
            using CustomAckMessagePtr = std::unique_ptr<msg::CustomAckMessage>;
            using UserInfoPtr = std::unique_ptr<msg::UserInfoMessage>;
            pro::proxy<spec::RawSignal> m_raw_signal;
            UserInfoPtr m_user_info {nullptr};
            std::uint32_t m_next_custom_msg_id {0};
            std::uint64_t m_next_custom_cb_id {0};
            std::unordered_map<std::uint64_t, cfgo::SigMsgCb> m_custom_msg_cbs {};
            std::unordered_map<CustomAckKey, unique_chan<CustomAckMessagePtr>> m_custom_ack_chans {};
        public:
            auto connect(close_chan closer) -> asio::awaitable<void>;
            auto run(close_chan closer) -> asio::awaitable<void>;
            auto send_message(close_chan closer, bool ack, std::string evt, std::string payload, std::string room, std::string to) -> asio::awaitable<std::string>;
            std::uint64_t on_message(cfgo::SigMsgCb && cb) {
                auto cb_id = m_next_custom_cb_id;
                m_next_custom_cb_id ++;
                m_custom_msg_cbs.insert(std::make_pair(cb_id, std::move(cb)));
            }
            std::uint64_t on_message(const cfgo::SigMsgCb & cb) {
                auto cb_copy = cb;
                return on_message(std::move(cb));
            }
            void off_message(std::uint64_t id) {
                m_custom_msg_cbs.erase(id);
            }
            friend class SignalAcker;
        };

        auto Signal::connect(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
            m_raw_signal.on_msg([weak_self = weak_from_this()](pro::proxy<spec::RawSigMsg> msg, pro::proxy<spec::RawSigAcker> acker) -> bool {
                if (auto self = weak_self.lock()) {
                    auto evt = msg.evt();
                    if (evt == "custom-ack") {
                        auto cam = std::make_unique<msg::CustomAckMessage>();
                        nlohmann::from_json(msg.payload(), *cam);
                        CustomAckKey key {.id = cam->msgId, .room = cam->router.room, .user = cam->router.userFrom};
                        auto ch_iter = self->m_custom_ack_chans.find(key);
                        if (ch_iter != self->m_custom_ack_chans.end())
                        {
                            chan_must_write(ch_iter->second, std::move(cam));
                            self->m_custom_ack_chans.erase(ch_iter);
                        }
                    } else if (evt.starts_with("custom:")) {
                        auto cevt = evt.substr(7);
                        msg::CustomMessage cm;
                        nlohmann::from_json(msg.payload(), cm);
                        auto s_msg = cfgo::SignalMsg(cevt, cm.router.room, cm.router.userFrom, std::move(cm.content), cm.msgId);
                        auto s_acker = cfgo::SignalAcker(self, s_msg);
                        for (auto iter = self->m_custom_msg_cbs.begin(); iter != self->m_custom_msg_cbs.end();) {
                            if (!iter->second(s_msg, s_acker)) {
                                iter = self->m_custom_msg_cbs.erase(iter);
                            } else {
                                ++iter;
                            }
                        }
                    }
                }
                return true;
            });
            unique_chan<UserInfoPtr> ready_ch {};
            auto cb_id = m_raw_signal.on_msg([weak_self = weak_from_this(), ready_ch](pro::proxy<spec::RawSigMsg> msg, pro::proxy<spec::RawSigAcker> acker) -> bool {
                if (msg.evt() == "ready")
                {
                    auto ready_msg = std::make_unique<msg::UserInfoMessage>();
                    nlohmann::from_json(msg.payload(), *ready_msg);
                    chan_must_write(ready_ch, std::move(ready_msg));
                    return false;
                }
                return true;
            });
            co_await m_raw_signal.connect(closer);
            self->m_user_info = co_await chan_read_or_throw<UserInfoPtr>(ready_ch, closer);
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, fix_async_lambda([self, closer]() -> asio::awaitable<void> {
                co_await self->m_raw_signal.run(closer);
            }), asio::detached);
        }

        uint32_t get_custom_msg_id(const nlohmann::json & payload) {
            return payload.value("msgId", 0);
        }

        auto Signal::send_message(close_chan closer, bool ack, std::string evt, std::string payload, std::string room, std::string to) -> asio::awaitable<std::string> {
            auto self = shared_from_this();
            auto msg_id = m_next_custom_msg_id;
            m_next_custom_msg_id ++;
            msg::CustomMessage msg {
                .router = msg::Router {.room = room, .userTo = to},
                .content = payload,
                .msgId = msg_id,
                .ack = ack,
            };
            CustomAckKey key {
                .id = msg_id,
                .room = room,
                .user = to,
            };
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, msg);
            unique_chan<CustomAckMessagePtr> ack_ch {};
            if (ack)
            {
                m_custom_ack_chans.insert(std::make_pair(key, ack_ch));
            }
            co_await m_raw_signal.send(closer, m_raw_signal.create_msg("custom:" + evt, std::move(js_msg), false));
            if (ack) {
                auto ack_msg = co_await chan_read_or_throw<std::unique_ptr<msg::CustomAckMessage>>(ack_ch, closer);
                if (ack_msg->err)
                {
                    auto ack_js = nlohmann::json::parse(ack_msg->content);
                    ServerErrorObject eo {};
                    nlohmann::from_json(ack_js, eo);
                    throw ServerError(std::move(eo));
                } else {
                    co_return ack_msg->content;
                }
            } else {
                co_return "";
            }
        }
    } // namespace impl

    std::string_view SignalMsg::evt() const noexcept {
        return impl()->evt();
    }
    bool SignalMsg::ack() const noexcept {
        return impl()->ack();
    }
    std::string_view SignalMsg::room() const noexcept {
        return impl()->room();
    }
    std::string_view SignalMsg::user() const noexcept {
        return impl()->user();
    }
    std::string_view SignalMsg::payload() const noexcept {
        return impl()->payload();
    }
    std::uint32_t SignalMsg::msg_id() const noexcept {
        return impl()->msg_id();
    }
    std::string && SignalMsg::consume() const noexcept {
        return impl()->consume();
    }

    auto make_websocket_raw_signal(const WebsocketSignalConfigure & conf) -> pro::proxy<spec::RawSignal> {
        return impl::WebsocketRawSignal::create(conf);
    }

    auto Signal::connect(close_chan closer) -> asio::awaitable<void> {
        return impl()->connect(std::move(closer));
    }

    auto Signal::send_message(const close_chan & closer, SignalMsg && msg) -> asio::awaitable<std::string> {
        return impl()->send_message(closer, msg.ack(), std::string(msg.evt()), msg.consume(), std::string(msg.room()), std::string(msg.user()));
    }

    std::uint64_t Signal::on_message(const SigMsgCb & cb) {
        return impl()->on_message(cb);
    }

    void Signal::off_message(std::uint64_t id) {
        impl()->off_message(id);
    }
    
} // namespace cfgo
