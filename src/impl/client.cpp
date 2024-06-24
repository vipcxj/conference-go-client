#include <assert.h>
#include <exception>
#include <cstdint>
#include "cfgo/track.hpp"
#include "cfgo/subscribation.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/asio.hpp"
#include "cfgo/async.hpp"
#include "cfgo/spd_helper.hpp"
#include "cfgo/rtc_helper.hpp"
#include "impl/client.hpp"
#include "impl/track.hpp"
#include "cpptrace/cpptrace.hpp"
#include "rtc/rtc.hpp"
#include "spdlog/spdlog.h"
#include "boost/lexical_cast.hpp"
#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"
#ifdef CFGO_SUPPORT_GSTREAMER
#include "gst/sdp/sdp.h"
#endif

namespace cfgo
{
    namespace impl
    {
        Client::Client(const Configuration &config, const IoCtxPtr & io_ctx, close_chan closer):
            m_config(config),
            m_io_ctx(io_ctx),
            m_strand(asio::make_strand(m_io_ctx->get_executor())),
            m_closer(closer.create_child()),
            m_signal(make_websocket_signal(m_closer, WebsocketSignalConfigure::from_conf(m_config))),
            m_webrtc(make_webrtc(m_closer, m_signal, m_config))
        {}

        Client::~Client()
        {}

        auto Client::strand() const noexcept -> const Strand &
        {
            return m_strand;
        }

        close_chan Client::get_closer() const noexcept
        {
            return m_closer;
        }

        SignalPtr Client::get_signal() const noexcept {
            return m_signal;
        }

        auto Client::subscribe(Pattern pattern, std::vector<std::string> req_types, const close_chan & close_chan) -> asio::awaitable<SubPtr> {
            return m_webrtc->subscribe(close_chan, std::move(pattern), std::move(req_types));
        }

        auto Client::unsubscribe(const std::string& sub_id, const close_chan & close_chan) -> asio::awaitable<void> {
            return m_webrtc->unsubscribe(close_chan, std::move(sub_id));
        }
    }
}
