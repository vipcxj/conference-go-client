#ifndef _CFGO_GST_CFGOSRC_HPP_
#define _CFGO_GST_CFGOSRC_HPP_

#include "cfgo/client.hpp"
#include "cfgo/track.hpp"
#include "cfgo/async.hpp"
#include "cfgo/log.hpp"
#include "cfgo/spd_helper.hpp"
#include "cfgo/gst/gstcfgosrc.h"
#include "cfgo/gst/utils.hpp"
#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include <vector>
#include <thread>

namespace cfgo
{
    namespace gst
    {
        class CfgoSrc;

        class CfgoSrc : public std::enable_shared_from_this<CfgoSrc>
        {
        public:
            struct Channel
            {
                GstPad * m_pad = nullptr;
                guint m_sessid = 0;
                guint m_ssrc = 0;
                guint m_pt = 0;
                gulong m_pad_added_handle = 0L;
                gulong m_pad_removed_handle = 0L;
                GstElement * m_processor = nullptr;
                std::vector<GstPad *> m_pads;

                ~Channel();
                void init(CfgoSrc * parent, GstCfgoSrc * owner, guint sessid, guint ssrc, guint pt, GstPad * pad);
                bool match(GstPad * pad) const;
                void install_ghost(CfgoSrc * parent, GstCfgoSrc * owner, GstPad * pad, const std::string & ghost_name);
                void uninstall_ghost(GstCfgoSrc * owner, GstPad * pad, bool remove = true);
            };
            using ChannelPtr = std::shared_ptr<Channel>;
            struct Session
            {
                guint m_id;
                TrackPtr m_track;
                GstPad * m_rtp_pad = nullptr;
                GstPad * m_rtcp_pad = nullptr;
                state_notifier m_rtp_data_notifier;
                std::atomic_bool m_rtp_need_data = true;
                state_notifier m_rtcp_data_notifier;
                std::atomic_bool m_rtcp_need_data = true;
                std::vector<ChannelPtr> m_channels;
                std::chrono::high_resolution_clock::duration m_rtp_push_cost_time {0};
                std::uint64_t m_rtp_pushed_num = 0;
                std::chrono::high_resolution_clock::duration m_rtcp_push_cost_time {0};
                std::uint64_t m_rtcp_pushed_num = 0;
                ~Session();
                ChannelPtr create_channel(CfgoSrc * parent, GstCfgoSrc * owner, guint ssrc, guint pt, GstPad * pad);
                void destroy_channel(CfgoSrc * parent, GstCfgoSrc * owner, Channel & channel, bool remove = true);
                ChannelPtr find_channel(GstPad * pad);
                ChannelPtr find_channel(guint ssrc, guint pt);
                void release_rtp_pad(GstElement * rtpbin);
                void release_rtcp_pad(GstElement * rtpbin);
            };
            using SessionPtr = std::shared_ptr<Session>;
            
            enum State
            {
                INITED,
                RUNNING,
                PAUSED,
                STOPED
            };
        private:
            Logger m_logger;
            State m_state {INITED};
            mutex m_state_mutex;
            GstCfgoSrcMode m_mode {GstCfgoSrcMode::GST_CFGO_SRC_MODE_RAW};
            Client::Ptr m_client;
            Pattern m_pattern;
            std::vector<std::string> m_req_types;
            close_chan m_close_ch;
            guint64 m_sub_timeout;
            TryOption m_sub_try_option;
            guint64 m_rtp_read_timeout;
            TryOption m_rtp_read_try_option;
            guint64 m_rtcp_read_timeout;
            TryOption m_rtcp_read_try_option;
            recursive_mutex m_mutex;
            GstCfgoSrc * m_owner = nullptr;
            bool m_detached = true;
            GstElement * m_rtp_bin = nullptr;
            gulong m_request_pt_map = 0;
            gulong m_pad_added_handler = 0;
            gulong m_pad_removed_handler = 0;
            std::vector<SessionPtr> m_sessions;
            GstCaps * m_decode_caps = nullptr;

            void _reset_sub_closer();
            void _reset_read_closer();
            void _create_rtp_bin(GstCfgoSrc * owner);
            SessionPtr _create_session(GstCfgoSrc * owner, TrackPtr track);
            void _create_processor(GstCfgoSrc * owner, Channel & channel, const std::string & type);
            void _destroy_processor(GstCfgoSrc * owner, Channel & channel);
            auto _loop() -> asio::awaitable<void>;
            auto _post_buffer(Session & session, Track::MsgType msg_type) -> asio::awaitable<void>;
            void _detach();
            void _install_pad(GstPad * pad);
            void _uninstall_pad(GstPad * pad);
            GstElementSPtr _safe_get_owner();
            template<typename T>
            cancelable<T> _safe_use_owner(std::function<T(GstCfgoSrc * owner)> func, bool lock = true)
            {
                if (m_detached)
                {
                    return make_canceled<T>();
                }
                if (lock)
                {
                    std::lock_guard lock(m_mutex);
                    if (m_detached)
                    {
                        return make_canceled<T>();
                    }
                    if constexpr (std::is_void_v<T>)
                    {
                        func(m_owner);
                        return make_resolved();
                    }
                    else
                    {
                        return make_resolved<T>(func(m_owner));
                    }
                }
                else
                {
                    if constexpr (std::is_void_v<T>)
                    {
                        func(m_owner);
                        return make_resolved();
                    }
                    else
                    {
                        return make_resolved<T>(func(m_owner));
                    }
                }
            }


        protected:
            CfgoSrc(int client_handle, const char * pattern_json, const char * req_types_str, guint64 sub_timeout, guint64 rtp_read_timeout, guint64 rtcp_read_timeout);
            CfgoSrc(const CfgoSrc &) = delete;
            CfgoSrc(CfgoSrc &&) = delete;
            CfgoSrc & operator= (const CfgoSrc &) = delete;
            CfgoSrc & operator= (CfgoSrc &&) = delete;
        public:
            ~CfgoSrc();

            using UPtr = std::unique_ptr<CfgoSrc>;
            using Ptr = std::shared_ptr<CfgoSrc>;
            static auto create(
                int client_handle, 
                const char * pattern_json, 
                const char * req_types_str, 
                guint64 sub_timeout = 0, 
                guint64 rtp_read_timeout = 0,
                guint64 rtcp_read_timeout = 0
            ) -> Ptr;
            void set_sub_timeout(guint64 timeout);
            void set_sub_try(gint32 tries, guint64 delay_init = 0, guint32 delay_step = 0, guint32 delay_level = 0);
            void set_rtp_read_timeout(guint64 timeout);
            void set_rtp_read_try(gint32 tries, guint64 delay_init = 0, guint32 delay_step = 0, guint32 delay_level = 0);
            void set_rtcp_read_timeout(guint64 timeout);
            void set_rtcp_read_try(gint32 tries, guint64 delay_init = 0, guint32 delay_step = 0, guint32 delay_level = 0);
            void attach(GstCfgoSrc * owner);
            void detach();
            void start();
            void pause();
            void resume();
            void stop();
            void switch_mode(GstCfgoSrcMode mode);
            void set_decode_caps(const GstCaps * caps);

            friend void rtpsrc_need_data(GstAppSrc * appsrc, guint length, gpointer user_data);
            friend void rtpsrc_enough_data(GstAppSrc * appsrc, gpointer user_data);
            friend void rtcpsrc_need_data(GstAppSrc * appsrc, guint length, gpointer user_data);
            friend void rtcpsrc_enough_data(GstAppSrc * appsrc, gpointer user_data);
            friend GstCaps * request_pt_map(GstElement *src, guint session_id, guint pt, gpointer user_data);
            friend void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data);
            friend void pad_removed_handler(GstElement * src, GstPad * pad, gpointer user_data);
            // friend GstPadProbeReturn block_buffer_probe(GstPad * pad, GstPadProbeInfo * info, CfgoSrc * input);
        };
    } // namespace gst    
} // namespace cfgo


#endif