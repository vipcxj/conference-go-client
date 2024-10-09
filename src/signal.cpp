#include "cfgo/signal.hpp"
#include "cfgo/utils.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/websocket.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"
#include "boost/url.hpp"
#include "cfgo/fmt.hpp"
#include "cfgo/async.hpp"
#include "cfgo/defer.hpp"

#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace cfgo
{
    namespace impl
    {
        struct CustomAckKey {
            std::uint64_t id;
            std::string room;
            std::string user;

            bool operator==(const CustomAckKey &other) const = default;
        };

        struct PingKey {
            std::string socket_id;
            std::string room;

            bool operator==(const PingKey &other) const = default;
        };
    } // namespace impl
    
} // namespace cfgo

template<>
struct std::hash<cfgo::impl::CustomAckKey> {

    std::size_t operator()(const cfgo::impl::CustomAckKey & k) const {
        using std::hash;
        using std::string;
        return ((hash<std::int64_t>()(k.id) ^ (hash<string>()(k.room) << 1)) >> 1) ^ (hash<string>()(k.user) << 1);
    }
};

template<>
struct std::hash<cfgo::impl::PingKey> {

    std::size_t operator()(const cfgo::impl::PingKey & k) const {
        using std::hash;
        using std::string;
        return (hash<string>()(k.socket_id) ^ (hash<string>()(k.room) << 1)) >> 1;
    }
};

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

        std::string encodeWsTextData(const std::string_view & evt, const std::uint64_t & msg_id, WsMsgFlag flag, const std::string_view & payload) {
            return fmt::format("{};{};{};{}", evt, msg_id, (int)flag, payload);
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

        class WSMsg : public cfgo::RawSigMsg {
        private:
            std::uint64_t m_msg_id;
            std::string m_evt;
            nlohmann::json m_payload;
            bool m_ack;
        public:
            WSMsg(std::uint64_t msg_id, std::string_view evt, nlohmann::json && payload, bool ack):
                m_msg_id(msg_id),
                m_evt(evt),
                m_payload(std::move(payload)),
                m_ack(ack)
            {}
            ~WSMsg() {}
            static auto create(std::uint64_t msg_id, std::string_view evt, nlohmann::json && payload, bool ack) -> RawSigMsgUPtr {
                return std::make_unique<WSMsg>(msg_id, evt, std::move(payload), ack);
            }
            std::uint64_t msg_id() const noexcept override {
                return m_msg_id;
            }
            std::string_view evt() const noexcept override {
                return m_evt;
            }
            const nlohmann::json & payload() const noexcept override {
                return m_payload;
            }
            bool ack() const noexcept override {
                return m_ack;
            }
        };

        class WSAcker : public RawSigAcker {
        private:
            std::weak_ptr<WebsocketRawSignal> m_signal;
            std::uint64_t m_msg_id;
        public:
            WSAcker(std::weak_ptr<WebsocketRawSignal> signal, std::uint64_t msg_id): m_signal(signal), m_msg_id(msg_id) {}
            ~WSAcker() {}
            static auto create(std::weak_ptr<WebsocketRawSignal> signal, std::uint64_t msg_id) -> RawSigAckerPtr {
                return std::make_shared<WSAcker>(signal, msg_id);
            }
            auto ack(close_chan closer, nlohmann::json payload) -> asio::awaitable<void> override;
            auto ack(close_chan closer, std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> override;
        };

        struct WSFakeAcker : public RawSigAcker {
            auto ack(close_chan closer, nlohmann::json payload) -> asio::awaitable<void> override {
                co_return;
            }
            auto ack(close_chan closer, std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> override {
                co_return;
            }
        };
        static WSFakeAcker WS_FAKE_ACKER {};
        static RawSigAckerPtr WS_FAKE_ACKER_PTR { std::shared_ptr<WSFakeAcker>{}, &WS_FAKE_ACKER };

        auto make_acker(std::weak_ptr<WebsocketRawSignal> signal, bool ack, std::uint64_t msg_id) {
            if (ack) {
                return WSAcker::create(signal, msg_id);
            } else {
                return WS_FAKE_ACKER_PTR;
            }
        }

        class WSAck {
        private:
            nlohmann::json m_payload;
            bool m_err;
        public:
            WSAck(nlohmann::json && payload, bool err): m_payload(std::move(payload)), m_err(err) {}
            nlohmann::json & payload() & noexcept {
                return m_payload;
            }
            const nlohmann::json & payload() const & noexcept {
                return m_payload;
            }
            nlohmann::json && payload() && noexcept {
                return std::move(m_payload);
            }
            const nlohmann::json && payload() const && noexcept {
                return std::move(m_payload);
            }
            bool err() const noexcept {
                return m_err;
            }
        };

        struct WSAckPackage {
            std::uint64_t msg_id;
            std::string payload;
            bool err {false};
            unique_void_chan done_ch {};
        };

        using WSAckChan = unique_chan<WSAck>;
        using WSAckOrMsg = std::variant<RawSigMsgUPtr, WSAckPackage>;

        class WebsocketRawSignal : public cfgo::RawSignal, public std::enable_shared_from_this<WebsocketRawSignal> {
        private:
            close_chan m_closer;
            SignalConfigure m_config;
            int ws_state = 0;
            std::string m_id {boost::uuids::to_string(boost::uuids::random_generator()())};
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBSOCKET, Log::make_logger_name(Log::Category::WEBSOCKET, m_id));
            std::optional<Websocket> m_ws {std::nullopt};
            std::uint64_t m_next_msg_id {1};
            std::uint64_t m_next_msg_cb_id {0};
            LazyRemoveMap<std::uint64_t, RawSigMsgCb> m_msg_cbs {};
            asiochan::channel<WSAckOrMsg> m_outgoing_ch {};
            std::unordered_map<std::uint64_t, WSAckChan> m_incoming_ack_chs {};

            std::unordered_set<close_chan> m_listen_closers {};
            InitableBox<void, std::string> m_connect;

            void register_listen_closer(const close_chan & closer) {
                if (m_closer.is_closed())
                {
                    closer.close_no_except(m_closer.get_close_reason());
                }
                else
                {
                    m_listen_closers.insert(closer);
                }
            }

            void unregister_peer_closer(const close_chan & closer) {
                m_listen_closers.erase(closer);
            }

            void process_peer_closers(std::string_view reason) noexcept {
                for (auto && closer : m_listen_closers) {
                    std::string the_reason {reason};
                    closer.close_no_except(std::move(the_reason));
                }
            }

            auto run() -> asio::awaitable<void>;
            auto _connect(close_chan closer, std::string socket_id) -> asio::awaitable<void>;
            auto _wrap_background_task(std::function<asio::awaitable<void>()> && task) -> std::function<asio::awaitable<void>()>;
        public:
            WebsocketRawSignal(close_chan closer, const SignalConfigure & conf):
                m_closer(closer),
                m_config(conf),
                m_connect([this](auto closer, std::string socket_id) {
                    return _connect(std::move(closer), std::move(socket_id));
                }, false)
            {}
            ~WebsocketRawSignal() noexcept {}
            static auto create(close_chan closer, const SignalConfigure & conf) -> RawSignalPtr {
                return std::make_shared<WebsocketRawSignal>(std::move(closer), conf);
            }
            auto id() const noexcept -> std::string {
                return m_id;
            }
            auto create_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> RawSigMsgUPtr override {
                auto msg_id = m_next_msg_id;
                m_next_msg_id += 2;
                return WSMsg::create(msg_id, evt, std::move(payload), ack);
            }
            auto connect(close_chan closer, std::string socket_id) -> asio::awaitable<void> override {
                return m_connect(std::move(closer), std::move(socket_id));
            }
            auto send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> override;
            std::uint64_t on_msg(RawSigMsgCb cb) override;
            void off_msg(std::uint64_t id) override;
            close_chan get_notify_closer() override {
                return m_closer.create_child();
            }
            close_chan get_closer() noexcept override {
                return m_closer;
            }
            void close() override {
                m_closer.close_no_except();
            }

            friend class WSAcker;
        };

        auto WSAcker::ack(close_chan closer, nlohmann::json payload) -> asio::awaitable<void> {
            if (auto signal = m_signal.lock())
            {
                auto payload_str = payload.dump();
                WSAckPackage ack_pkg {
                    .msg_id = m_msg_id,
                    .payload = std::move(payload_str),
                };
                auto done_ch = ack_pkg.done_ch;
                CFGO_LOGGER_TRACE(signal->m_logger, "Normal raw ack sending... Id: {}; Content: {}.", m_msg_id, payload_str);
                co_await chan_write_or_throw<WSAckOrMsg>(signal->m_outgoing_ch, std::move(ack_pkg), closer);
                co_await chan_read_or_throw<void>(done_ch, closer);
                CFGO_LOGGER_TRACE(signal->m_logger, "Normal raw ack sended. Id: {}", m_msg_id);
            }
            co_return;
        }
        auto WSAcker::ack(close_chan closer, std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> {
            if (auto signal = m_signal.lock())
            {
                nlohmann::json payload_js {};
                nlohmann::to_json(payload_js, *eo);
                auto payload_str = payload_js.dump();
                WSAckPackage ack_pkg {
                    .msg_id = m_msg_id,
                    .payload = std::move(payload_str),
                    .err = true,
                };
                auto done_ch = ack_pkg.done_ch;
                CFGO_LOGGER_TRACE(signal->m_logger, "Error raw ack sending... Id: {}; Content: {}.", m_msg_id, payload_str);
                co_await chan_write_or_throw<WSAckOrMsg>(signal->m_outgoing_ch, std::move(ack_pkg), closer);
                co_await chan_read_or_throw<void>(done_ch, closer);
                CFGO_LOGGER_TRACE(signal->m_logger, "Error raw ack sended. Id: {}; What: {}.", m_msg_id, eo->msg);
            }
            co_return;
        }

        auto WebsocketRawSignal::_connect(close_chan closer, std::string socket_id) -> asio::awaitable<void> {
            auto self = shared_from_this();
            if (!socket_id.empty())
            {
                m_id = socket_id;
                m_logger = Log::instance().create_logger(Log::Category::WEBSOCKET, Log::make_logger_name(Log::Category::WEBSOCKET, m_id));
            }
            if (self->ws_state == 0)
            {
                self->ws_state = 1;
                m_ws.emplace(asio::use_awaitable.as_default_on(
                    Websocket(co_await asio::this_coro::executor)
                ));
            }
            else if (self->ws_state == 2)
            {
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
            co_await run();
            self->ws_state = 2;
        }

        auto WebsocketRawSignal::_wrap_background_task(std::function<asio::awaitable<void>()> && task) -> std::function<asio::awaitable<void>()> {
            auto self = shared_from_this();
            return fix_async_lambda([self, task = std::move(task)]() -> asio::awaitable<void> {
                try
                {
                    co_await task();
                }
                catch(const CancelError & e)
                {
                    self->m_closer.close_no_except(e.message());
                }
                catch(...)
                {
                    auto reason = what();
                    auto loc = std::source_location::current();
                    CFGO_SELF_ERROR("[{}:{}:{}]<{}> error found. {}", loc.file_name(), loc.line(), loc.column(), loc.function_name(), reason);
                    self->m_closer.close_no_except(std::move(reason));
                }
            });
        }

        auto WebsocketRawSignal::run() -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, self->_wrap_background_task([weak_self = weak_from_this()]() -> asio::awaitable<void> {
                if (auto self = weak_self.lock())
                {
                    co_await self->m_closer.await();
                    std::string reason = self->m_closer.get_close_reason();
                    if (reason.empty())
                    {
                        self->m_logger->debug("The raw signal closed.");
                    }
                    else
                    {
                        self->m_logger->debug("The raw signal closed, {}.", reason);
                    }
                    self->process_peer_closers(reason);
                    self->m_ws->next_layer().close();
                }
                co_return;
            }), asio::detached);
            asio::co_spawn(executor, self->_wrap_background_task([self]() -> asio::awaitable<void> {
                auto executor = co_await asio::this_coro::executor;
                do {
                    beast::flat_buffer buffer;
                    auto [ec, bytes] = co_await self->m_ws->async_read(buffer, asio::as_tuple(asio::use_awaitable));
                    if (ec != beast::errc::success)
                    {
                        self->m_closer.close_no_except(ec.message());
                        break;
                    }
                    std::string_view evt;
                    std::uint64_t msg_id;
                    WsMsgFlag flag;
                    std::string_view svPayload;
                    decodeWsTextData(buffer, evt, msg_id, flag, svPayload);
                    auto payload = nlohmann::json::parse(svPayload);
                    if (flag == WS_MSG_FLAG_IS_ACK_ERR || flag == WS_MSG_FLAG_IS_ACK_NORMAL) {
                        CFGO_SELF_TRACE("receive ack msg, err: {}, id: {}", flag == WS_MSG_FLAG_IS_ACK_ERR, msg_id);
                        WSAck msg(std::move(payload), flag == WS_MSG_FLAG_IS_ACK_ERR);
                        auto ch_iter = self->m_incoming_ack_chs.find(msg_id);
                        if (ch_iter != self->m_incoming_ack_chs.end()) {
                            chan_must_write(ch_iter->second, std::move(msg));
                        }
                    } else {
                        CFGO_SELF_TRACE("receive {} msg, id: {}, need ack: {}, content: {}", evt, msg_id, flag == WS_MSG_FLAG_NEED_ACK, svPayload);
                        RawSigMsgPtr msg = WSMsg::create(msg_id, evt, std::move(payload), flag == WS_MSG_FLAG_NEED_ACK);
                        auto acker = make_acker(self->weak_from_this(), flag == WS_MSG_FLAG_NEED_ACK, msg_id);
                        self->m_msg_cbs.start_loop();
                        for(auto it = self->m_msg_cbs->begin(); it != self->m_msg_cbs->end(); ++it) {
                            asio::co_spawn(executor, fix_async_lambda(log_error([self, cb_id = it->first, msg, acker]() -> asio::awaitable<void> {
                                auto cb = self->m_msg_cbs->at(cb_id);
                                if(!co_await cb(msg, acker)) {
                                    self->m_msg_cbs.lazy_remove(cb_id);
                                }
                            }, self->m_logger)), asio::detached);
                        }
                        self->m_msg_cbs.complete_loop();
                    }
                } while (true);
            }), asio::detached);
            asio::co_spawn(executor, self->_wrap_background_task([self]() -> asio::awaitable<void> {
                do
                {
                    auto ack_or_msg = co_await chan_read_or_throw<WSAckOrMsg>(self->m_outgoing_ch, self->m_closer);
                    if (std::holds_alternative<RawSigMsgUPtr>(ack_or_msg))
                    {
                        auto msg = std::get<RawSigMsgUPtr>(std::move(ack_or_msg));
                        auto payload_str = msg->payload().dump();
                        auto payload = encodeWsTextData(msg->evt(), msg->msg_id(), msg->ack() ? WS_MSG_FLAG_NEED_ACK : WS_MSG_FLAG_NO_ACK, payload_str);
                        co_await self->m_ws->async_write(asio::buffer(payload));
                        if (!msg->ack())
                        {
                            WSAck ack_msg(nullptr, false);
                            auto ch_iter = self->m_incoming_ack_chs.find(msg->msg_id());
                            if (ch_iter != self->m_incoming_ack_chs.end()) {
                                chan_must_write(ch_iter->second, std::move(ack_msg));
                            }
                        }
                    }
                    else
                    {
                        auto ack_pkg = std::get<WSAckPackage>(std::move(ack_or_msg));
                        auto payload = encodeWsTextData("", ack_pkg.msg_id, ack_pkg.err ? WS_MSG_FLAG_IS_ACK_ERR : WS_MSG_FLAG_IS_ACK_NORMAL, ack_pkg.payload);
                        co_await self->m_ws->async_write(asio::buffer(payload));
                        chan_must_write(ack_pkg.done_ch);
                    }
                } while (true);
            }), asio::detached);
        }

        auto WebsocketRawSignal::send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> {
            auto self = shared_from_this();
            CFGO_SELF_TRACE("sending {} msg with id {} and ack {}", msg->evt(), msg->msg_id(), msg->ack());
            closer = closer.create_child();
            register_listen_closer(closer);
            DEFER({
                unregister_peer_closer(closer);
            });
            std::string evt {msg->evt()};
            auto msg_id = msg->msg_id();
            auto ack = msg->ack();
            WSAckChan ch {};
            m_incoming_ack_chs.insert(std::make_pair(msg_id, ch));
            DEFER({
                m_incoming_ack_chs.erase(msg_id);
            });
            co_await chan_write_or_throw<WSAckOrMsg>(self->m_outgoing_ch, std::move(msg), closer);
            CFGO_SELF_TRACE("after async write {} msg with id {}", evt, msg_id);

            if (m_config.ack_timeout > duration_t{0})
            {
                closer.set_timeout(m_config.ack_timeout, std::format("ack of {} msg timeout after {} ms", evt, std::chrono::duration_cast<std::chrono::milliseconds>(m_config.ack_timeout).count()));
            }  
            auto && ack_msg = co_await chan_read_or_throw<WSAck>(ch, closer);
            if (ack_msg.err()) {
                ServerErrorObject error {};
                nlohmann::from_json(ack_msg.payload(), error);
                CFGO_SELF_TRACE("send {} msg failed with id {} and ack {}", evt, msg_id, ack);
                throw ServerError(std::move(error), false);
            } else {
                CFGO_SELF_TRACE("send {} msg succeed with id {} and ack {}", evt, msg_id, ack);
                co_return std::move(ack_msg).payload();
            }
        }

        std::uint64_t WebsocketRawSignal::on_msg(RawSigMsgCb cb) {
            auto cb_id = m_next_msg_cb_id;
            m_next_msg_cb_id ++;
            m_msg_cbs->insert(std::make_pair(cb_id, std::move(cb)));
            return cb_id;
        }

        void WebsocketRawSignal::off_msg(std::uint64_t id) {
            m_msg_cbs.lazy_remove(id);
        }
    }

    namespace impl {
        class SignalMsg : public cfgo::SignalMsg {
            std::string m_evt;
            bool m_ack;
            std::string m_room;
            std::string m_socket;
            std::string m_payload;
            std::uint32_t m_msg_id;
        public:
            SignalMsg(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload, std::uint32_t msg_id):
                m_evt(evt),
                m_ack(ack),
                m_room(room),
                m_socket(to),
                m_payload(std::move(payload)),
                m_msg_id(msg_id)
            {}
            ~SignalMsg() {}
            static auto create(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload, std::uint32_t msg_id) -> cfgo::SignalMsgUPtr {
                return std::make_unique<SignalMsg>(evt, ack, room, to, std::move(payload), msg_id);
            }
            std::string_view evt() const noexcept override {
                return m_evt;
            }
            bool ack() const noexcept override {
                return m_ack;
            }
            std::string_view room() const noexcept override {
                return m_room;
            }
            std::string_view socket_id() const noexcept override {
                return m_socket;
            }
            std::string_view payload() const noexcept override {
                return m_payload;
            }
            std::uint32_t msg_id() const noexcept override {
                return m_msg_id;
            }
            std::string && consume() noexcept override {
                return std::move(m_payload);
            }
        };

        class Signal;
        class SignalAcker : public cfgo::SignalAcker, public std::enable_shared_from_this<SignalAcker> {
            std::weak_ptr<Signal> m_signal;
            SignalMsgPtr m_msg;
        public:
            SignalAcker(std::weak_ptr<Signal> signal, SignalMsgPtr msg):
                m_signal(std::move(signal)),
                m_msg(std::move(msg))
            {}
            ~SignalAcker() {}
            static SignalAckerPtr create(std::weak_ptr<Signal> signal, SignalMsgPtr msg) {
                return std::make_shared<SignalAcker>(std::move(signal), std::move(msg));
            }
            auto ack(close_chan closer, std::string payload) -> asio::awaitable<void> override;
            auto ack(close_chan closer, std::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> override;
        };

        class Signal : public cfgo::Signal, public std::enable_shared_from_this<Signal> {
        private:
            using CustomAckMessagePtr = std::unique_ptr<msg::CustomAckMessage>;
            using UserInfoPtr = std::unique_ptr<msg::UserInfoMessage>;
            using PingMsgPtr = std::shared_ptr<msg::PingMessage>;
            using PingCb = std::function<bool(PingMsgPtr)>;
            using PongMsgPtr = std::shared_ptr<msg::PongMessage>;
            using PongCb = std::function<bool(PongMsgPtr)>;
            using CandMsgPtr = cfgo::Signal::CandMsgPtr;
            using CandCb = cfgo::Signal::CandCb;
            using SdpMsgPtr = cfgo::Signal::SdpMsgPtr;
            using SdpCb = cfgo::Signal::SdpCb;
            using SubscribeMsgPtr = cfgo::Signal::SubscribeMsgPtr;
            using SubscribeResultPtr = cfgo::Signal::SubscribeResultPtr;
            using SubscribedMsgPtr = cfgo::Signal::SubscribedMsgPtr;
            using PingCh = asiochan::unbounded_channel<PingMsgPtr>;
            RawSignalPtr m_raw_signal;
            Logger m_logger;
            UserInfoPtr m_user_info {nullptr};
            std::uint64_t m_next_cb_id {0};
            std::uint32_t m_next_custom_msg_id {0};
            std::uint32_t m_next_ping_msg_id {0};
            std::unordered_map<std::uint64_t, PingCb> m_ping_cbs {};
            std::unordered_map<std::uint64_t, PongCb> m_pong_cbs {};
            std::unordered_map<std::uint64_t, CandCb> m_cand_cbs {};
            std::unordered_map<std::uint64_t, SdpCb> m_sdp_cbs {};
            LazyRemoveMap<std::uint64_t, cfgo::SigMsgCb> m_custom_msg_cbs {};
            std::unordered_map<CustomAckKey, unique_chan<CustomAckMessagePtr>> m_custom_ack_chans {};
            std::unordered_map<std::string, std::shared_ptr<LazyBox<SubscribedMsgPtr>>> m_subscribed_msgs {};
            std::unordered_set<std::string> m_rooms {};
            InitableBox<void, std::string> m_connect;

            auto _connect(close_chan closer, std::string socket_id) -> asio::awaitable<void>;

            auto _send_ping(close_chan closer, std::uint32_t msg_id, std::string room, std::string socket_id) -> asio::awaitable<void> {
                auto self = shared_from_this();
                msg::PingMessage pm {
                    .router = msg::Router {
                        .room = std::move(room),
                        .socketTo = std::move(socket_id),
                    },
                    .msgId = msg_id,
                };
                nlohmann::json js_msg;
                nlohmann::to_json(js_msg, pm);
                co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("ping", std::move(js_msg), false));
            }
            std::uint64_t _on_ping(PingCb && cb) {
                auto cb_id = m_next_cb_id;
                m_next_cb_id ++;
                m_ping_cbs.insert(std::make_pair(cb_id, std::move(cb)));
                return cb_id;
            }
            void _off_ping(std::uint64_t id) {
                m_ping_cbs.erase(id);
            }

            auto _send_pong(close_chan closer, std::uint32_t msg_id, std::string room, std::string socket_id) -> asio::awaitable<void> {
                auto self = shared_from_this();
                msg::PongMessage pm {
                    .router = msg::Router {
                        .room = std::move(room),
                        .socketTo = std::move(socket_id),
                    },
                    .msgId = msg_id,
                };
                nlohmann::json js_msg;
                nlohmann::to_json(js_msg, pm);
                co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("pong", std::move(js_msg), false));
            }
            std::uint64_t _on_pong(PongCb && cb) {
                auto cb_id = m_next_cb_id;
                m_next_cb_id ++;
                m_pong_cbs.insert(std::make_pair(cb_id, std::move(cb)));
                return cb_id;
            }
            void _off_pong(std::uint64_t id) {
                m_pong_cbs.erase(id);
            }
        public:
            Signal(RawSignalPtr raw_signal):
                m_raw_signal(std::move(raw_signal)),
                m_logger(Log::instance().create_logger(Log::Category::SIGNAL, Log::make_logger_name(Log::Category::SIGNAL, m_raw_signal->id()))),
                m_connect([this](auto closer, std::string socket_id) {
                    return _connect(std::move(closer), std::move(socket_id));
                }, false)
            {}
            ~Signal() noexcept {}
            auto connect(close_chan closer, std::string socket_id = "") -> asio::awaitable<void> override {
                return m_connect(std::move(closer), std::move(socket_id));
            }
            auto id() const noexcept -> std::string {
                return m_raw_signal->id();
            }
            close_chan get_notify_closer() override {
                return m_raw_signal->get_notify_closer();
            }
            close_chan get_closer() noexcept override {
                return m_raw_signal->get_closer();
            }
            void close() override {
                m_raw_signal->close();
            }
            auto id(close_chan closer) -> asio::awaitable<std::string> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                co_return self->m_user_info->socketId;
            }
            auto user_id(close_chan closer) -> asio::awaitable<std::string> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                co_return self->m_user_info->userId;
            }
            auto user_name(close_chan closer) -> asio::awaitable<std::string> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                co_return self->m_user_info->userName;
            }
            auto role(close_chan closer) -> asio::awaitable<std::string> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                co_return self->m_user_info->role;
            }
            auto join(close_chan closer, std::vector<std::string> rooms) -> asio::awaitable<void> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                msg::JoinMessage msg {std::move(rooms)};
                nlohmann::json js_msg;
                nlohmann::to_json(js_msg, msg);
                co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("join", std::move(js_msg), true));
                m_rooms.insert(std::make_move_iterator(msg.rooms.begin()), std::make_move_iterator(msg.rooms.end()));
            }
            auto join(close_chan closer, std::string room) -> asio::awaitable<void> override {
                return join(std::move(closer), std::vector<std::string>{room});
            }
            auto leave(close_chan closer, std::vector<std::string> rooms) -> asio::awaitable<void> override {
                auto self = shared_from_this();
                co_await self->connect(closer);
                msg::LeaveMessage msg {std::move(rooms)};
                nlohmann::json js_msg;
                nlohmann::to_json(js_msg, msg);
                co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("leave", std::move(js_msg), true));
                for (auto && room : msg.rooms) {
                    m_rooms.erase(room);
                }
            }
            auto leave(close_chan closer, std::string room) -> asio::awaitable<void> override {
                return leave(std::move(closer), std::vector<std::string>{room});
            }
            auto rooms() const -> const std::unordered_set<std::string> & {
                return m_rooms;
            }
            auto send_candidate(close_chan closer, CandMsgPtr msg) -> asio::awaitable<void>;
            std::uint64_t on_candidate(CandCb cb) override {
                auto cb_id = m_next_cb_id;
                m_next_cb_id ++;
                m_cand_cbs.insert(std::make_pair(cb_id, std::move(cb)));
                return cb_id;
            }
            void off_candidate(std::uint64_t id) override {
                m_cand_cbs.erase(id);
            }
            auto send_sdp(close_chan closer, SdpMsgPtr msg) -> asio::awaitable<void> override;
            std::uint64_t on_sdp(SdpCb cb) override{
                auto cb_id = m_next_cb_id;
                m_next_cb_id ++;
                m_sdp_cbs.insert(std::make_pair(cb_id, std::move(cb)));
                return cb_id;
            }
            void off_sdp(std::uint64_t id) override {
                m_sdp_cbs.erase(id);
            }
            auto subsrcibe(close_chan closer, SubscribeMsgPtr msg) -> asio::awaitable<SubscribeResultPtr> override;
            auto wait_subscribed(close_chan closer, SubscribeResultPtr sub) -> asio::awaitable<SubscribedMsgPtr> override;
            auto create_message(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload) -> cfgo::SignalMsgUPtr override;
            auto send_message(close_chan closer, SignalMsgUPtr msg) -> asio::awaitable<std::string> override;
            std::uint64_t on_message(cfgo::SigMsgCb && cb) override {
                auto cb_id = m_next_cb_id;
                m_next_cb_id ++;
                m_custom_msg_cbs->insert(std::make_pair(cb_id, std::move(cb)));
                return cb_id;
            }
            std::uint64_t on_message(const cfgo::SigMsgCb & cb) override {
                auto cb_copy = cb;
                return on_message(std::move(cb));
            }
            void off_message(std::uint64_t id) override {
                m_custom_msg_cbs.lazy_remove(id);
            }
            auto keep_alive(close_chan closer, std::string room, std::string socket_id, bool active, duration_t timeout, KeepAliveCb cb) -> asio::awaitable<void> override;
            friend class SignalAcker;
        };

        auto SignalAcker::ack(close_chan closer, std::string payload) -> asio::awaitable<void> {
            auto self = shared_from_this();
            if (!self->m_msg->ack())
            {
                co_return;
            }
            if (auto signal = self->m_signal.lock())
            {
                msg::CustomAckMessage msg {
                    .router = msg::Router {
                        .room = std::string(m_msg->room()),
                        .socketTo = std::string(m_msg->socket_id()),
                    },
                    .msgId = m_msg->msg_id(),
                    .content = payload,
                    .err = false,
                };
                nlohmann::json raw_payload;
                nlohmann::to_json(raw_payload, msg);
                auto raw_msg = signal->m_raw_signal->create_msg("custom-ack", std::move(raw_payload), false);
                co_await signal->m_raw_signal->send_msg(closer, std::move(raw_msg));
            }
        }
        auto SignalAcker::ack(close_chan closer, std::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> {
            auto self = shared_from_this();
            if (!self->m_msg->ack())
            {
                co_return;
            }
            if (auto signal = m_signal.lock())
            {
                nlohmann::json js_err;
                nlohmann::to_json(js_err, *err);
                msg::CustomAckMessage msg {
                    .router = msg::Router {
                        .room = std::string(m_msg->room()),
                        .socketTo = std::string(m_msg->socket_id()),
                    },
                    .msgId = m_msg->msg_id(),
                    .content = js_err.dump(),
                    .err = true,
                };
                nlohmann::json raw_payload;
                nlohmann::from_json(raw_payload, msg);
                auto raw_msg = signal->m_raw_signal->create_msg("custom-ack", std::move(raw_payload), false);
                co_await signal->m_raw_signal->send_msg(closer, std::move(raw_msg));
            }
        }

        auto Signal::_connect(close_chan closer, std::string socket_id) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            auto signal_closer = self->m_raw_signal->get_notify_closer();
            auto on_msg = [weak_self = weak_from_this(), closer = signal_closer](RawSigMsgPtr msg, RawSigAckerPtr acker) -> asio::awaitable<bool> {
                if (auto self = weak_self.lock()) {
                    auto evt = msg->evt();
                    if (evt == "ping")
                    {
                        assert(!msg->ack());
                        auto pm = std::make_shared<msg::PingMessage>();
                        nlohmann::from_json(msg->payload(), *pm);
                        co_await self->_send_pong(closer, pm->msgId, pm->router.room, pm->router.socketFrom);
                        for (auto iter = self->m_ping_cbs.begin(); iter != self->m_ping_cbs.end();)
                        {
                            if (!iter->second(pm))
                            {
                                iter = self->m_ping_cbs.erase(iter);
                            }
                            else
                            {
                                ++iter;
                            }
                        }
                    }
                    else if (evt == "pong")
                    {
                        assert(!msg->ack());
                        auto pm = std::make_shared<msg::PongMessage>();
                        nlohmann::from_json(msg->payload(), *pm);
                        for (auto iter = self->m_pong_cbs.begin(); iter != self->m_pong_cbs.end();)
                        {
                            if (!iter->second(pm))
                            {
                                iter = self->m_pong_cbs.erase(iter);
                            }
                            else
                            {
                                ++iter;
                            }
                        }
                    }
                    else if (evt == "candidate")
                    {
                        assert(msg->ack());
                        co_await acker->ack(closer, nlohmann::json ("ack"));
                        auto sm = std::make_shared<msg::CandidateMessage>();
                        nlohmann::from_json(msg->payload(), *sm);
                        for (auto iter = self->m_cand_cbs.begin(); iter != self->m_cand_cbs.end();)
                        {
                            if (!iter->second(sm))
                            {
                                iter = self->m_cand_cbs.erase(iter);
                            } else {
                                ++iter;
                            }
                        }
                    } else if (evt == "sdp") {
                        assert(msg->ack());
                        co_await acker->ack(closer, nlohmann::json ("ack"));
                        auto sm = std::make_shared<msg::SdpMessage>();
                        nlohmann::from_json(msg->payload(), *sm);
                        for (auto iter = self->m_sdp_cbs.begin(); iter != self->m_sdp_cbs.end();)
                        {
                            if (!iter->second(sm))
                            {
                                iter = self->m_sdp_cbs.erase(iter);
                            } else {
                                ++iter;
                            }
                        }
                    } else if (evt == "custom-ack") {
                        assert(!msg->ack());
                        auto sm = std::make_unique<msg::CustomAckMessage>();
                        nlohmann::from_json(msg->payload(), *sm);
                        CustomAckKey key {.id = sm->msgId, .room = sm->router.room, .user = sm->router.socketFrom};
                        auto ch_iter = self->m_custom_ack_chans.find(key);
                        if (ch_iter != self->m_custom_ack_chans.end())
                        {
                            chan_must_write(ch_iter->second, std::move(sm));
                            self->m_custom_ack_chans.erase(ch_iter);
                        }
                    } else if (evt.starts_with("custom:")) {
                        assert(!msg->ack());
                        auto cevt = evt.substr(7);
                        msg::CustomMessage cm;
                        nlohmann::from_json(msg->payload(), cm);
                        SignalMsgPtr s_msg = SignalMsg::create(cevt, cm.ack, cm.router.room, cm.router.socketFrom, std::move(cm.content), cm.msgId);
                        auto s_acker = SignalAcker::create(self, s_msg);
                        auto executor = co_await asio::this_coro::executor;
                        self->m_custom_msg_cbs.start_loop();
                        for (auto iter = self->m_custom_msg_cbs->begin(); iter != self->m_custom_msg_cbs->end(); ++iter) {
                            asio::co_spawn(executor, fix_async_lambda(log_error([self, cb_id = iter->first, s_msg, s_acker]() -> asio::awaitable<void> {
                                auto cb = self->m_custom_msg_cbs->at(cb_id);
                                if (!co_await cb(s_msg, s_acker))
                                {
                                    self->m_custom_msg_cbs.lazy_remove(cb_id);
                                }
                            }, self->m_logger)), asio::detached);
                        }
                        self->m_custom_msg_cbs.complete_loop();
                    }
                }
                co_return true;
            };
            self->m_raw_signal->on_msg(std::move(on_msg));
            unique_chan<UserInfoPtr> ready_ch {};
            auto cb_id = m_raw_signal->on_msg([weak_self = weak_from_this(), ready_ch](RawSigMsgPtr msg, RawSigAckerPtr acker) -> asio::awaitable<bool> {
                if (msg->evt() == "ready")
                {
                    auto ready_msg = std::make_unique<msg::UserInfoMessage>();
                    nlohmann::from_json(msg->payload(), *ready_msg);
                    chan_must_write(ready_ch, std::move(ready_msg));
                    co_return false;
                }
                co_return true;
            });
            auto id = self->id();
            co_await m_raw_signal->connect(closer, std::move(socket_id));
            auto new_id = self->id();
            if (new_id != id)
            {
                self->m_logger = Log::instance().create_logger(Log::Category::SIGNAL, Log::make_logger_name(Log::Category::SIGNAL, new_id));
            }
            self->m_user_info = co_await chan_read_or_throw<UserInfoPtr>(ready_ch, closer);
            self->m_rooms = std::unordered_set<std::string>(self->m_user_info->rooms.begin(), self->m_user_info->rooms.end());
            self->m_user_info->rooms.clear();
            // asio::co_spawn(executor, fix_async_lambda(log_error([]() -> asio::awaitable<void> {

            // }, self->m_logger)), asio::detached);
        }

        auto Signal::send_candidate(close_chan closer, CandMsgPtr msg) -> asio::awaitable<void> {
            auto self = shared_from_this();
            co_await self->connect(closer);
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, *msg);
            co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("candidate", std::move(js_msg), true));
        }

        auto Signal::send_sdp(close_chan closer, SdpMsgPtr msg) -> asio::awaitable<void> {
            auto self = shared_from_this();
            co_await self->connect(closer);
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, *msg);
            co_await self->m_raw_signal->send_msg(closer, self->m_raw_signal->create_msg("sdp", std::move(js_msg), true));
        }

        auto Signal::subsrcibe(close_chan closer, SubscribeMsgPtr msg) -> asio::awaitable<SubscribeResultPtr> {
            auto self = shared_from_this();
            co_await self->connect(closer);
            std::shared_ptr<LazyBox<std::string>> lazy_sub_id = LazyBox<std::string>::create();
            auto executor = co_await asio::this_coro::executor;
            self->m_raw_signal->on_msg([self, lazy_sub_id, closer, executor = std::move(executor)](RawSigMsgPtr msg, RawSigAckerPtr acker) -> asio::awaitable<bool> {
                if (msg->evt() == "subscribed")
                {
                    co_await acker->ack(closer, "ack");
                    auto sub_id = co_await lazy_sub_id->get(closer);
                    if (msg->payload().value("subId", "") == sub_id)
                    {
                        auto sub_msg_iter = self->m_subscribed_msgs.find(sub_id);
                        if (sub_msg_iter != self->m_subscribed_msgs.end())
                        {
                            auto sm = std::make_unique<msg::SubscribedMessage>();
                            nlohmann::from_json(msg->payload(), *sm);
                            sub_msg_iter->second->init(std::move(sm));
                        }
                        co_return false;
                    }
                }
                co_return true;
            });
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, *msg);
            auto res = co_await m_raw_signal->send_msg(closer, m_raw_signal->create_msg("subscribe", std::move(js_msg), true));
            auto sub_res_msg = std::make_unique<msg::SubscribeResultMessage>();
            nlohmann::from_json(res, *sub_res_msg);
            m_subscribed_msgs.emplace(sub_res_msg->id, LazyBox<SubscribedMsgPtr>::create());
            lazy_sub_id->init(sub_res_msg->id);
            co_return sub_res_msg;
        }

        auto Signal::wait_subscribed(close_chan closer, SubscribeResultPtr sub) -> asio::awaitable<SubscribedMsgPtr> {
            auto self = shared_from_this();
            auto lazy_sub_msg_iter = self->m_subscribed_msgs.find(sub->id);
            if (lazy_sub_msg_iter != self->m_subscribed_msgs.end()) {
                auto res_msg = co_await lazy_sub_msg_iter->second->move(closer);
                self->m_subscribed_msgs.erase(lazy_sub_msg_iter);
                co_return res_msg;
            } else {
                throw cpptrace::runtime_error("call subsrcibe at first. ");
            }
        }

        auto Signal::create_message(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload) -> cfgo::SignalMsgUPtr {
            auto msg_id = m_next_custom_msg_id ++;
            return SignalMsg::create(evt, ack, room, to, std::move(payload), msg_id);
        }
        
        auto Signal::send_message(close_chan closer, SignalMsgUPtr msg) -> asio::awaitable<std::string> {
            auto self = shared_from_this();
            co_await self->connect(closer);
            auto evt = msg->evt();
            msg::CustomMessage cm {
                .router = msg::Router {.room = std::string{msg->room()}, .socketTo = std::string{msg->socket_id()}},
                .content = msg->consume(),
                .msgId = msg->msg_id(),
                .ack = msg->ack(),
            };
            CustomAckKey key {
                .id = msg->msg_id(),
                .room = cm.router.room,
                .user = cm.router.socketTo,
            };
            nlohmann::json js_msg;
            nlohmann::to_json(js_msg, cm);
            unique_chan<CustomAckMessagePtr> ack_ch {};
            if (cm.ack)
            {
                m_custom_ack_chans.insert(std::make_pair(key, ack_ch));
            }
            co_await m_raw_signal->send_msg(closer, m_raw_signal->create_msg(fmt::format("custom:{}", evt), std::move(js_msg), false));
            if (cm.ack) {
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

        auto Signal::keep_alive(close_chan closer, std::string room, std::string socket_id, bool active, duration_t timeout, KeepAliveCb cb) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto signal_closer = self->m_raw_signal->get_notify_closer();
            co_await closer.depend_on(signal_closer, "The underneath signal is closed.");
            co_await self->connect(closer);

            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, fix_async_lambda(log_error([
                self = std::move(self),
                closer = std::move(closer),
                room = std::move(room),
                socket_id = std::move(socket_id),
                active,
                timeout,
                cb = std::move(cb)
            ]() -> asio::awaitable<void> {
                KeepAliveContext kaCtx{};
                if (active)
                {
                    do
                    {
                        try
                        {
                            unique_void_chan pong_ch {};
                            auto start_pt = std::chrono::high_resolution_clock::now();
                            auto msg_id = self->m_next_ping_msg_id ++;
                            auto timeout_closer = closer.create_child();
                            timeout_closer.set_timeout(timeout);
                            auto cb_id = self->_on_pong([pong_ch, msg_id, room, socket_id](PongMsgPtr pong) -> bool {
                                if (pong->msgId == msg_id && pong->router.socketFrom == socket_id && pong->router.room == room)
                                {
                                    chan_must_write(pong_ch);
                                    return false;
                                }
                                return true;
                            });
                            DEFER({
                                self->_off_pong(cb_id);
                            });
                            bool timeout = false;
                            try
                            {
                                co_await self->_send_ping(timeout_closer, msg_id, room, socket_id);
                            }
                            catch(const cfgo::CancelError & e)
                            {
                                if (e.is_timeout())
                                {
                                    timeout = true;
                                }
                                else
                                {
                                    assert(timeout_closer.is_closed());
                                    co_return;
                                }
                            }                          
                            if (timeout || !co_await chan_read<void>(pong_ch, timeout_closer))
                            {
                                if (timeout_closer.is_timeout())
                                {
                                    kaCtx.timeout_num ++;
                                    kaCtx.timeout_dur += std::chrono::high_resolution_clock::now() - start_pt;
                                    if (cb(kaCtx))
                                    {
                                        co_return;
                                    }
                                }
                                else
                                {
                                    assert(timeout_closer.is_closed());
                                    co_return;
                                }
                            }
                            else
                            {
                                kaCtx.timeout_num = 0;
                                kaCtx.timeout_dur = duration_t{0};
                                kaCtx.warmup = false;
                                if (co_await timeout_closer.await())
                                {
                                    co_return;
                                }
                            }
                        }
                        catch(...)
                        {
                            kaCtx.err = std::current_exception();
                            if (cb(kaCtx))
                            {
                                co_return;
                            }
                            else
                            {
                                kaCtx.err = nullptr;
                            }
                        }
                    } while (true);
                }
                else
                {
                    asiochan::unbounded_channel<PingMsgPtr> ping_ch {};
                    auto cb_id = self->_on_ping([ping_ch, room, socket_id](PingMsgPtr ping) -> bool {
                        if (ping->router.room == room && ping->router.socketFrom == socket_id)
                        {
                            chan_must_write(ping_ch, ping);
                        }
                        return true;
                    });
                    DEFER({
                        self->_off_ping(cb_id);
                    });
                    do
                    {
                        try
                        {
                            auto start_pt = std::chrono::high_resolution_clock::now();
                            auto timeout_closer = closer.create_child();
                            timeout_closer.set_timeout(timeout);
                            auto ping_res = co_await chan_read<PingMsgPtr>(ping_ch, timeout_closer);
                            if (ping_res)
                            {
                                kaCtx.timeout_num = 0;
                                kaCtx.timeout_dur = duration_t{0};
                                kaCtx.warmup = false;
                            }
                            else
                            {
                                if (timeout_closer.is_timeout())
                                {
                                    kaCtx.timeout_num ++;
                                    kaCtx.timeout_dur += std::chrono::high_resolution_clock::now() - start_pt;
                                    if (cb(kaCtx))
                                    {
                                        co_return;
                                    }
                                }
                                else
                                {
                                    assert(timeout_closer.is_closed());
                                    co_return;
                                }
                            }
                        }
                        catch(...)
                        {
                            kaCtx.err = std::current_exception();
                            if (cb(kaCtx))
                            {
                                co_return;
                            }
                            else
                            {
                                kaCtx.err = nullptr;
                            }
                        }
                    } while (true);
                }
            })), asio::detached);
        }
    } // namespace impl

    auto make_keep_alive_callback(close_chan closer, int timeout_num, duration_t timeout_dur, int timeout_num_when_warmup, duration_t timeout_dur_when_warmup, bool term_when_err, Logger logger) -> KeepAliveCb {
        return [=](const KeepAliveContext & ctx) -> bool {
            if (ctx.err && term_when_err)
            {
                auto reason = what(ctx.err);
                if (logger)
                {
                    logger->error("Keep alive failed because of err: {}", reason);
                }
                closer.close(std::move(reason));
                return true;
            }
            if (ctx.warmup)
            {
                if (timeout_num_when_warmup >= 0 && ctx.timeout_num > timeout_num_when_warmup)
                {
                    closer.close("Keep alive timeout.");
                    return true;
                }
                if (timeout_dur_when_warmup > duration_t{0} && ctx.timeout_dur > timeout_dur_when_warmup)
                {
                    closer.close("Keep alive timeout.");
                    return true;
                }
            }
            else
            {
                if (timeout_num >= 0 && ctx.timeout_num > timeout_num)
                {
                    closer.close("Keep alive timeout.");
                    return true;
                }
                if (timeout_dur > duration_t{0} && ctx.timeout_dur > timeout_dur)
                {
                    closer.close("Keep alive timeout.");
                    return true;
                }
            }
            return false;
        };
    }

    auto make_websocket_signal(close_chan closer, const SignalConfigure & conf) -> SignalPtr {
        auto raw_signal = impl::WebsocketRawSignal::create(std::move(closer), conf);
        return std::make_shared<impl::Signal>(std::move(raw_signal));
    }
    
} // namespace cfgo
