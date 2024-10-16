#ifndef _CFGO_SIGNAL_HPP_
#define _CFGO_SIGNAL_HPP_

#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/configuration.hpp"
#include "cfgo/error.hpp"
#include "cfgo/message.hpp"
#include "cfgo/publication.hpp"
#include "cfgo/log.hpp"
#include "cfgo/allocate_tracer.hpp"

#include <memory>
#include <vector>
#include <unordered_set>

namespace cfgo
{
    struct RawSigMsg {
        virtual ~RawSigMsg() = 0;
        [[nodiscard]]
        virtual std::string_view evt() const noexcept = 0;
        [[nodiscard]]
        virtual const nlohmann::json & payload() const noexcept = 0;
        [[nodiscard]]
        virtual bool ack() const noexcept = 0;
        [[nodiscard]]
        virtual std::uint64_t msg_id() const noexcept = 0;
    };
    inline RawSigMsg::~RawSigMsg() {}
    using RawSigMsgPtr = std::shared_ptr<RawSigMsg>;
    using RawSigMsgUPtr = allocate_tracers::unique_ptr<RawSigMsg>;

    struct RawSigAcker {
        virtual ~RawSigAcker() = 0;
        [[nodiscard]]
        virtual auto ack(close_chan closer, nlohmann::json payload) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto ack(close_chan closer, allocate_tracers::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> = 0;
    };
    inline RawSigAcker::~RawSigAcker() {}
    using RawSigAckerPtr = std::shared_ptr<RawSigAcker>;
    using RawSigMsgCb = std::function<asio::awaitable<bool>(RawSigMsgPtr, RawSigAckerPtr acker)>;

    struct RawSignal {
        virtual ~RawSignal() = 0;
        [[nodiscard]]
        virtual auto id() const noexcept -> std::string = 0;
        [[nodiscard]]
        virtual auto connect(close_chan closer, std::string socket_id = "") -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto send_msg(close_chan closer, RawSigMsgUPtr msg) -> asio::awaitable<nlohmann::json> = 0;
        virtual std::uint64_t on_msg(RawSigMsgCb cb) = 0;
        virtual void off_msg(std::uint64_t) = 0;
        [[nodiscard]]
        virtual auto create_msg(const std::string_view & evt, nlohmann::json && payload, bool ack) -> RawSigMsgUPtr = 0;
        /**
         * used to get the close event, close this closer will not close the raw signal.
         */
        [[nodiscard]]
        virtual close_chan get_notify_closer() = 0;
        /**
         * close this closer will close the raw signal.
         */
        [[nodiscard]]
        virtual close_chan get_closer() noexcept = 0;
        /**
         * close the raw signal.
         */
        virtual void close() = 0;
    };
    inline RawSignal::~RawSignal() {}
    using RawSignalPtr = std::shared_ptr<RawSignal>;

    struct SignalMsg {
        virtual ~SignalMsg() = 0;
        [[nodiscard]]
        virtual std::string_view evt() const noexcept = 0;
        [[nodiscard]]
        virtual bool ack() const noexcept = 0;
        [[nodiscard]]
        virtual std::string_view room() const noexcept = 0;
        [[nodiscard]]
        virtual std::string_view socket_id() const noexcept = 0;
        [[nodiscard]]
        virtual std::string_view payload() const noexcept = 0;
        [[nodiscard]]
        virtual std::uint32_t msg_id() const noexcept = 0;
        [[nodiscard]]
        virtual std::string && consume() noexcept = 0;
    };
    inline SignalMsg::~SignalMsg() {}
    using SignalMsgPtr = std::shared_ptr<SignalMsg>;
    using SignalMsgUPtr = allocate_tracers::unique_ptr<SignalMsg>;

    struct SignalAcker {
        virtual ~SignalAcker() = 0;
        [[nodiscard]]
        virtual auto ack(close_chan closer, std::string payload) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto ack(close_chan closer, allocate_tracers::unique_ptr<ServerErrorObject> err) -> asio::awaitable<void> = 0;
    };
    inline SignalAcker::~SignalAcker() {}
    using SignalAckerPtr = std::shared_ptr<SignalAcker>;

    using SigMsgCb = std::function<asio::awaitable<bool>(SignalMsgPtr, SignalAckerPtr)>;

    struct KeepAliveContext {
        std::exception_ptr err {nullptr};
        int timeout_num {0};
        duration_t timeout_dur {0};
        bool warmup {true};
    };

    using KeepAliveCb = std::function<bool(const KeepAliveContext &)>;

    auto make_keep_alive_callback(close_chan signal, int timeout_num, duration_t timeout_dur, int timeout_num_when_warmup = -1, duration_t timeout_dur_when_warmup = duration_t{0}, bool term_when_err = true, Logger logger = nullptr) -> KeepAliveCb;

    inline auto makeKeepAliveCallback(close_chan signal, int timeout_num, int timeout_num_when_warmup = -1, bool term_when_err = true, Logger logger = nullptr) -> KeepAliveCb {
        return make_keep_alive_callback(std::move(signal), timeout_num, duration_t{0}, timeout_num_when_warmup, duration_t{0}, term_when_err, logger);
    }

    inline auto makeKeepAliveCallback(close_chan signal, duration_t timeout_dur, duration_t timeout_dur_when_warmup = duration_t{0}, bool term_when_err = true, Logger logger = nullptr) -> KeepAliveCb {
        return make_keep_alive_callback(std::move(signal), -1, timeout_dur, -1, timeout_dur_when_warmup, term_when_err, logger);
    }

    struct Signal {
        using CandMsgPtr = std::shared_ptr<msg::CandidateMessage>;
        using CandCb = std::function<bool(CandMsgPtr)>;
        using SdpMsgPtr = std::shared_ptr<msg::SdpMessage>;
        using SdpCb = std::function<bool(SdpMsgPtr)>;
        using SubscribeMsgPtr = allocate_tracers::unique_ptr<msg::SubscribeMessage>;
        using SubscribedMsgPtr = allocate_tracers::unique_ptr<msg::SubscribedMessage>;
        using SubscribeResultPtr = allocate_tracers::unique_ptr<msg::SubscribeResultMessage>;
        using PublishAddMsgPtr = allocate_tracers::unique_ptr<msg::PublishAddMessage>;
        using PublishRemoveMsgPtr = allocate_tracers::unique_ptr<msg::PublishRemoveMessage>;
        using PublishResultMsgPtr = allocate_tracers::unique_ptr<msg::PublishResultMessage>;
        using PublishedMsgPtr = allocate_tracers::unique_ptr<msg::PublishedMessage>;

        struct publish_handle
        {
            std::uint64_t cb_id {0};
            asiochan::unbounded_channel<PublishedMsgPtr> ch {};
            RawSignalPtr raw_signal {};

            publish_handle(std::uint64_t cb_id, asiochan::unbounded_channel<PublishedMsgPtr> ch, RawSignalPtr raw_signal) noexcept
            : cb_id(cb_id), ch(ch), raw_signal(raw_signal) {}

            publish_handle(const publish_handle &) = delete;
            publish_handle(publish_handle && other) noexcept: cb_id(other.cb_id), ch(other.ch), raw_signal(other.raw_signal)
            {
                other.raw_signal.reset();
            }
            publish_handle & operator= (const publish_handle &) = delete;
            publish_handle & operator= (publish_handle && other) noexcept
            {
                cb_id = other.cb_id;
                ch = other.ch;
                raw_signal = std::move(other.raw_signal);
                other.raw_signal.reset();
                return *this;
            }

            ~publish_handle()
            {
                if (raw_signal)
                {
                    raw_signal->off_msg(cb_id);
                }
            }
        };

        virtual ~Signal() = 0;
        [[nodiscard]]
        virtual auto id() const noexcept -> std::string = 0;
        [[nodiscard]]
        virtual auto connect(close_chan closer, std::string socket_id = "") -> asio::awaitable<void> = 0;
        /**
         * used to get the close event, close this closer will not close the signal.
         */
        [[nodiscard]]
        virtual close_chan get_notify_closer() = 0;
        /**
         * close this closer will close the signal.
         */
        [[nodiscard]]
        virtual close_chan get_closer() noexcept = 0;
        virtual void close() = 0;
        [[nodiscard]]
        virtual auto id(close_chan closer) -> asio::awaitable<std::string> = 0;
        [[nodiscard]]
        virtual auto user_id(close_chan closer) -> asio::awaitable<std::string> = 0;
        [[nodiscard]]
        virtual auto user_name(close_chan closer) -> asio::awaitable<std::string> = 0;
        [[nodiscard]]
        virtual auto role(close_chan closer) -> asio::awaitable<std::string> = 0;
        [[nodiscard]]
        virtual auto join(close_chan closer, std::string room) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto join(close_chan closer, std::vector<std::string> rooms) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto leave(close_chan closer, std::string room) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto leave(close_chan closer, std::vector<std::string> rooms) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto rooms() const -> const std::unordered_set<std::string> & = 0;
        [[nodiscard]]
        virtual auto send_candidate(close_chan closer, CandMsgPtr msg) -> asio::awaitable<void> = 0;
        virtual std::uint64_t on_candidate(CandCb cb) = 0;
        virtual void off_candidate(std::uint64_t id) = 0;
        [[nodiscard]]
        virtual auto send_sdp(close_chan closer, SdpMsgPtr msg) -> asio::awaitable<void> = 0;
        virtual std::uint64_t on_sdp(SdpCb cb) = 0;
        virtual void off_sdp(std::uint64_t id) = 0;
        [[nodiscard]]
        virtual auto subsrcibe(close_chan closer, SubscribeMsgPtr msg) -> asio::awaitable<SubscribedMsgPtr> = 0;
        [[nodiscard]]
        virtual auto publish(close_chan closer, Publication pub) -> asio::awaitable<publish_handle> = 0;
        [[nodiscard]]
        virtual auto wait_published(close_chan closer, publish_handle pub_handle) -> asio::awaitable<void> = 0;
        [[nodiscard]]
        virtual auto create_message(std::string_view evt, bool ack, std::string_view room, std::string_view to, std::string && payload) -> cfgo::SignalMsgUPtr = 0;
        [[nodiscard]]
        virtual auto send_message(close_chan closer, SignalMsgUPtr msg) -> asio::awaitable<std::string> = 0;
        virtual std::uint64_t on_message(const SigMsgCb & cb) = 0;
        virtual std::uint64_t on_message(SigMsgCb && cb) = 0;
        virtual void off_message(std::uint64_t id) = 0;
        [[nodiscard]]
        virtual auto keep_alive(close_chan closer, std::string room, std::string socket_id, bool active, duration_t timeout, KeepAliveCb cb) -> asio::awaitable<void> = 0;
    };
    inline Signal::~Signal() {}
    using SignalPtr = std::shared_ptr<Signal>;
    using Rooms = std::vector<std::string>;
    using RoomSet = std::unordered_set<std::string>;

    auto make_websocket_signal(close_chan closer, const SignalConfigure & conf) -> SignalPtr;

} // namespace cfgo


#endif