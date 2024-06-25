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
    } // namespace impl
    
} // namespace cfgo

template<>
struct std::hash<cfgo::impl::CustomAckKey> {

    std::size_t operator()(const cfgo::impl::CustomAckKey& k) const {
        using std::hash;
        using std::string;
        return ((hash<std::int64_t>()(k.id) ^ (hash<string>()(k.room) << 1)) >> 1) ^ (hash<string>()(k.user) << 1);
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

        class WSMsg : public enable_shared<cfgo::RawSigMsg, WSMsg> {
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

        class WSAcker : public enable_shared<RawSigAcker, WSAcker> {
        private:
            derived_weak_ptr<cfgo::RawSignal, WebsocketRawSignal> m_signal;
            std::uint64_t m_msg_id;
        public:
            WSAcker(derived_weak_ptr<cfgo::RawSignal, WebsocketRawSignal> signal, std::uint64_t msg_id): m_signal(signal), m_msg_id(msg_id) {}
            ~WSAcker() {}
            static auto create(derived_weak_ptr<cfgo::RawSignal, WebsocketRawSignal> signal, std::uint64_t msg_id) -> RawSigAckerPtr {
                return std::make_shared<WSAcker>(signal, msg_id);
            }
            auto ack(nlohmann::json payload) -> asio::awaitable<void> override;
            auto ack(std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> override;
        };

        struct WSFakeAcker : public enable_shared<RawSigAcker, WSFakeAcker> {
            auto ack(nlohmann::json payload) -> asio::awaitable<void> override {
                co_return;
            }
            auto ack(std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> override {
                co_return;
            }
        };
        static WSFakeAcker WS_FAKE_ACKER {};
        static RawSigAckerPtr WS_FAKE_ACKER_PTR { std::shared_ptr<WSFakeAcker>{}, &WS_FAKE_ACKER };

        auto make_acker(derived_weak_ptr<cfgo::RawSignal, WebsocketRawSignal> signal, bool ack, std::uint64_t msg_id) {
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

        class WebsocketRawSignal : public enable_shared<cfgo::RawSignal, WebsocketRawSignal> {
        private:
            using WSAckChan = unique_chan<WSAck>;

            WebsocketSignalConfigure m_config;
            int ws_state = 0;
            Logger m_logger = Log::instance().create_logger(Log::Category::WEBSOCKET);
            const std::string m_id {boost::uuids::to_string(boost::uuids::random_generator()())};
            std::optional<Websocket> m_ws {std::nullopt};
            std::uint64_t m_next_msg_id {1};
            std::uint64_t m_next_msg_cb_id {0};
            close_chan m_closer;
            LzayRemoveMap<std::uint64_t, RawSigMsgCb> m_msg_cbs {};
            std::unordered_map<std::uint64_t, WSAckChan> m_ack_chs {};

            std::unordered_set<close_chan> m_listen_closers {};

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
        public:
            WebsocketRawSignal(close_chan closer, const WebsocketSignalConfigure & conf):
                m_closer(closer.create_child()),
                m_config(conf)
            {}
            ~WebsocketRawSignal() {}
            static auto create(close_chan closer, const WebsocketSignalConfigure & conf) -> RawSignalUPtr {
                return std::make_unique<WebsocketRawSignal>(std::move(closer), conf);
            }
            auto create_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> RawSigMsgUPtr override {
                auto msg_id = m_next_msg_id;
                m_next_msg_id += 2;
                return WSMsg::create(msg_id, evt, std::move(payload), ack);
            }
            auto connect(close_chan closer) -> asio::awaitable<void> override;
            auto send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> override;
            std::uint64_t on_msg(RawSigMsgCb cb) override;
            void off_msg(std::uint64_t id) override;
            close_chan get_notify_closer() override {
                return m_closer.create_child();
            }
            void close() override {
                m_closer.close_no_except();
            }

            friend class WSAcker;
        };

        auto WSAcker::ack(nlohmann::json payload) -> asio::awaitable<void> {
            if (auto signal = m_signal.lock())
            {
                auto payload_str = payload.dump();
                auto buffer = encodeWsTextData("", m_msg_id, WS_MSG_FLAG_IS_ACK_NORMAL, payload_str);
                co_await signal->m_ws->async_write(buffer);
            }
            co_return;
        }
        auto WSAcker::ack(std::unique_ptr<ServerErrorObject> eo) -> asio::awaitable<void> {
            if (auto signal = m_signal.lock())
            {
                nlohmann::json payload {};
                nlohmann::to_json(payload, *eo);
                auto payload_str = payload.dump();
                auto buffer = encodeWsTextData("", m_msg_id, WS_MSG_FLAG_IS_ACK_ERR, payload_str);
                co_await signal->m_ws->async_write(buffer);
            }
            co_return;
        }

        auto WebsocketRawSignal::connect(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
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

        auto WebsocketRawSignal::run() -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            asio::co_spawn(executor, fix_async_lambda([weak_self = weak_from_this()]() -> asio::awaitable<void> {
                if (auto self = weak_self.lock())
                {
                    std::string reason;
                    if (co_await self->m_closer.await())
                    {
                        reason = self->m_closer.get_close_reason();
                    }
                    else
                    {
                        reason = self->m_closer.get_timeout_reason();
                    }
                    if (reason.empty())
                    {
                        self->m_logger->debug("The raw signal closed.");
                    }
                    else
                    {
                        self->m_logger->debug("The raw signal closed, %s.", reason);
                    }
                    self->process_peer_closers(reason);
                    self->m_ws->next_layer().close();
                }
                co_return;
            }), asio::detached);
            asio::co_spawn(executor, fix_async_lambda([self]() -> asio::awaitable<void> {
                auto executor = co_await asio::this_coro::executor;
                try
                {
                    while (true) {
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
                            WSAck msg(std::move(payload), flag == WS_MSG_FLAG_IS_ACK_ERR);
                            auto ch_iter = self->m_ack_chs.find(msg_id);
                            if (ch_iter != self->m_ack_chs.end()) {
                                chan_must_write(ch_iter->second, std::move(msg));
                                self->m_ack_chs.erase(ch_iter);
                            }
                        } else {
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
                    }
                }
                catch(...)
                {
                    self->m_closer.close_no_except(what());
                }
            }), asio::detached);
        }

        auto WebsocketRawSignal::send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> {
            auto self = shared_from_this();
            closer = closer.create_child();
            register_listen_closer(closer);
            DEFER({
                unregister_peer_closer(closer);
            });
            auto payload_str = msg->payload().dump();
            auto buffer = encodeWsTextData(msg->evt(), msg->msg_id(), msg->ack() ? WS_MSG_FLAG_NEED_ACK : WS_MSG_FLAG_NO_ACK, payload_str);
            WSAckChan ch {};
            if (msg->ack()) {
                m_ack_chs.insert(std::make_pair(msg->msg_id(), ch));
            }
            co_await m_ws->async_write(buffer);
            if (msg->ack()) {
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
        class SignalMsg : public enable_shared<cfgo::SignalMsg, SignalMsg> {
            std::string m_evt;
            bool m_ack;
            std::string m_room;
            std::string m_user;
            std::string m_payload;
            std::uint32_t m_msg_id;
        public:
            SignalMsg(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload, std::uint32_t msg_id):
                m_evt(evt),
                m_ack(ack),
                m_room(room),
                m_user(to),
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
            std::string_view user() const noexcept override {
                return m_user;
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
        class SignalAcker : public enable_shared<cfgo::SignalAcker, SignalAcker> {
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

        class Signal : public enable_shared<cfgo::Signal, Signal> {
        private:
            using CustomAckMessagePtr = std::unique_ptr<msg::CustomAckMessage>;
            using UserInfoPtr = std::unique_ptr<msg::UserInfoMessage>;
            using CandMsgPtr = cfgo::Signal::CandMsgPtr;
            using CandCb = cfgo::Signal::CandCb;
            using SdpMsgPtr = cfgo::Signal::SdpMsgPtr;
            using SdpCb = cfgo::Signal::SdpCb;
            using SubscribeMsgPtr = cfgo::Signal::SubscribeMsgPtr;
            using SubscribeResultPtr = cfgo::Signal::SubscribeResultPtr;
            using SubscribedMsgPtr = cfgo::Signal::SubscribedMsgPtr;
            Logger m_logger = Log::instance().create_logger(Log::Category::SIGNAL);
            RawSignalUPtr m_raw_signal;
            UserInfoPtr m_user_info {nullptr};
            std::uint64_t m_next_cb_id {0};
            std::uint32_t m_next_custom_msg_id {0};
            std::unordered_map<std::uint64_t, CandCb> m_cand_cbs {};
            std::unordered_map<std::uint64_t, SdpCb> m_sdp_cbs {};
            LzayRemoveMap<std::uint64_t, cfgo::SigMsgCb> m_custom_msg_cbs {};
            std::unordered_map<CustomAckKey, unique_chan<CustomAckMessagePtr>> m_custom_ack_chans {};
            std::unordered_map<std::string, std::shared_ptr<LazyBox<SubscribedMsgPtr>>> m_subscribed_msgs {};
        public:
            Signal(RawSignalUPtr raw_signal): m_raw_signal(std::move(raw_signal)) {}
            ~Signal() {}
            auto connect(close_chan closer) -> asio::awaitable<void> override;
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
                        .userTo = std::string(m_msg->user()),
                    },
                    .msgId = m_msg->msg_id(),
                    .content = payload,
                    .err = false,
                };
                nlohmann::json raw_payload;
                nlohmann::from_json(raw_payload, msg);
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
                        .userTo = std::string(m_msg->user()),
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

        auto Signal::connect(close_chan closer) -> asio::awaitable<void> {
            auto self = shared_from_this();
            auto executor = co_await asio::this_coro::executor;
            auto on_msg = [weak_self = weak_from_this()](RawSigMsgPtr msg, RawSigAckerPtr acker) -> asio::awaitable<bool> {
                if (auto self = weak_self.lock()) {
                    auto evt = msg->evt();
                    if (evt == "candidate")
                    {
                        co_await acker->ack(nlohmann::json ("ack"));
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
                        co_await acker->ack(nlohmann::json ("ack"));
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
                        auto sm = std::make_unique<msg::CustomAckMessage>();
                        nlohmann::from_json(msg->payload(), *sm);
                        CustomAckKey key {.id = sm->msgId, .room = sm->router.room, .user = sm->router.userFrom};
                        auto ch_iter = self->m_custom_ack_chans.find(key);
                        if (ch_iter != self->m_custom_ack_chans.end())
                        {
                            chan_must_write(ch_iter->second, std::move(sm));
                            self->m_custom_ack_chans.erase(ch_iter);
                        }
                    } else if (evt.starts_with("custom:")) {
                        auto cevt = evt.substr(7);
                        msg::CustomMessage cm;
                        nlohmann::from_json(msg->payload(), cm);
                        SignalMsgPtr s_msg = SignalMsg::create(cevt, cm.ack, cm.router.room, cm.router.userFrom, std::move(cm.content), cm.msgId);
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
            m_raw_signal->on_msg(std::move(on_msg));
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
            co_await m_raw_signal->connect(closer);
            self->m_user_info = co_await chan_read_or_throw<UserInfoPtr>(ready_ch, closer);
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
            m_subscribed_msgs.emplace(std::make_pair(sub_res_msg->id, LazyBox<SubscribedMsgPtr>::create()));
            lazy_sub_id->init(sub_res_msg->id);
            co_return sub_res_msg;
        }

        auto Signal::wait_subscribed(close_chan closer, SubscribeResultPtr sub) -> asio::awaitable<SubscribedMsgPtr> {
            auto self = shared_from_this();
            auto lazy_sub_msg_iter = self->m_subscribed_msgs.find(sub->id);
            if (lazy_sub_msg_iter != self->m_subscribed_msgs.end()) {
                self->m_subscribed_msgs.erase(lazy_sub_msg_iter);
                co_return co_await lazy_sub_msg_iter->second->get(closer);
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
            auto msg_id = m_next_custom_msg_id;
            m_next_custom_msg_id ++;
            auto evt = msg->evt();
            msg::CustomMessage cm {
                .router = msg::Router {.room = std::string{msg->room()}, .userTo = std::string{msg->user()}},
                .content = msg->consume(),
                .msgId = msg->msg_id(),
                .ack = msg->ack(),
            };
            CustomAckKey key {
                .id = msg_id,
                .room = cm.router.room,
                .user = cm.router.userTo,
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
    } // namespace impl

    auto make_websocket_signal(close_chan closer, const WebsocketSignalConfigure & conf) -> SignalUPtr {
        auto raw_signal = impl::WebsocketRawSignal::create(std::move(closer), conf);
        return std::make_unique<impl::Signal>(std::move(raw_signal));
    }
    
} // namespace cfgo
