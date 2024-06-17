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
#include "sio_client.h"
#include <mutex>
#include <optional>
#include <map>
#include <atomic>
#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

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
            using CtxPtr = std::shared_ptr<asio::execution_context>;
            struct Guard {
                Client* const m_client;
                Guard(Client* const client): m_client(client) {
                    m_client->lock();
                }
                ~Guard() {
                    m_client->unlock();
                }
            };

            struct MsgChanner {
                Client* const m_client;
                std::map<std::string, msg_chan> m_chan_map;
                MsgChanner(Client* const client);
                void prepare(const std::string &evt, msg_ptr const& ack = nullptr);
                void release(const std::string &evt);
                msg_chan &chan(const std::string &evt);
                const msg_chan &chan(const std::string &evt) const;
                ~MsgChanner();
            };
        private:
            Logger m_logger;
            Configuration m_config;
            std::unique_ptr<sio::client> m_client;
            std::shared_ptr<rtc::PeerConnection> m_peer;
            close_chan m_closer;
            const std::string m_id;
            #ifdef CFGO_SUPPORT_GSTREAMER
            friend class Track;
            GstSDPMessage * m_gst_sdp;
            #endif
        public:
            Client() = delete;
            Client(const Configuration& config, close_chan closer);
            Client(const Configuration& config, const CtxPtr& io_ctx, close_chan closer);
            Client(Client&&) = default;
            ~Client();
            Client(const Client&) = delete;
            Client& operator = (Client&) = delete;
            void init();
            [[nodiscard]] auto subscribe(Pattern pattern, std::vector<std::string> req_types, const close_chan & close_chan) -> asio::awaitable<SubPtr>;
            [[nodiscard]] auto unsubscribe(const std::string& sub_id, const close_chan & close_chan) -> asio::awaitable<cancelable<void>>;
            [[nodiscard]] auto send_custom_message_with_ack(const std::string & content, const std::string & to, const close_chan & close_chan) -> asio::awaitable<cancelable<void>>;
            void send_custom_message_no_ack(const std::string & content, const std::string & to);
            std::uint32_t on_custom_message(std::function<bool(const std::string &, const std::string &, const std::string &, std::function<void()>)> cb);
            void off_custom_message(std::uint32_t cb_id);

            void set_sio_logs_default();
            void set_sio_logs_verbose();
            void set_sio_logs_quiet();
            std::optional<rtc::Description> peer_local_desc() const;
            std::optional<rtc::Description> peer_remote_desc() const;
            CtxPtr execution_context() const noexcept;
            close_chan get_closer() const noexcept;
        private:
            cfgo::AsyncMutex m_a_mutex;
            void update_gst_sdp();

            [[nodiscard]] msg_ptr create_auth_message() const;

            template<typename T>
            void write_ch(asiochan::writable_channel_type<T> auto ch, const T& v) const {
                asio::co_spawn(asio::get_associated_executor(m_io_context), ch.write(v), asio::detached);
            };
            void write_ch(asiochan::channel<void>& ch) {
                asio::co_spawn(asio::get_associated_executor(m_io_context), ch.write(), asio::detached);
            };
            void setup_socket_close_callback(const close_chan & closer);
            void clean_socket_close_callback();
            void emit(const std::string& evt, msg_ptr msg);
            [[nodiscard]] asio::awaitable<cancelable<msg_ptr>> emit_with_ack(const std::string& evt, msg_ptr msg, close_chan& close_chan) const;
            [[nodiscard]] asio::awaitable<cancelable<msg_ptr>> wait_for_msg(const std::string& evt, MsgChanner& msg_channer, close_chan& close_chan, std::function<bool(msg_ptr)> cond);
            void add_candidate(const msg_ptr& msg);

            bool m_inited = false;
            mutex m_inited_mutex;

            void check_inited();

            CtxPtr m_io_context;
            const bool m_thread_safe;
            mutex m_mutex;

            mutex m_msg_cb_mutex;
            std::uint32_t m_msg_cb_next_id = 0;
            std::map<std::uint32_t, std::function<bool(sio::event & event)>> m_msg_cbs;
            std::uint32_t add_msg_cb(std::function<bool(sio::event & event)> cb);
            void remove_msg_cb(std::uint32_t cb_id);
            void process_msg_cbs(sio::event & event);

            std::atomic_uint32_t m_custom_msg_next_id;

            Client(const Configuration& config, const CtxPtr& io_ctx, close_chan closer, bool thread_safe);
            void lock();
            void unlock() noexcept;
        };
    }
}

#endif