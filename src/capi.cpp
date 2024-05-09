#include "cfgo/capi.h"
#include "cfgo/cbridge.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/async.hpp"
#include "cpptrace.hpp"
#include "spdlog/spdlog.h"
#include "asio/io_context.hpp"
#include "asio/thread_pool.hpp"
#include "asio/co_spawn.hpp"
#include "asio/associated_executor.hpp"
#include "asio/detached.hpp"
#include "boost/algorithm/string.hpp"

#include <exception>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>

#define CFGO_HANDLE_NOT_EXIST(who) "The " who " handle does not exist"

#define CFGO_DEFINE_CPP_GET_REF(NAME, TYPE, MSG) \
    TYPE get_ ## NAME(int handle) \
    { \
        std::lock_guard lock(c_mutex); \
        if (auto it = NAME ## _map.find(handle); it != NAME ## _map.end()) \
        { \
            return it->second; \
        } \
        else \
        { \
            throw cpptrace::invalid_argument(CFGO_HANDLE_NOT_EXIST(MSG)); \
        } \
    }

#define CFGO_DEFINE_CPP_WRAP(NAME, TYPE) \
    int wrap_ ## NAME(TYPE ptr) \
    { \
        std::lock_guard lock(c_mutex); \
        int handle = ++last_handle; \
        NAME ## _map.emplace(std::make_pair(handle, ptr)); \
        NAME ## _ref_map.emplace(std::make_pair(handle, 1)); \
        return handle; \
    }

#define CFGO_DEFINE_CPP_REF(NAME, MSG) \
    void ref_ ## NAME(int handle) \
    { \
        std::lock_guard lock(c_mutex); \
        if (auto it = NAME ## _ref_map.find(handle); it != NAME ## _ref_map.end()) \
        { \
            it->second ++; \
        } \
        else \
        { \
            throw cpptrace::invalid_argument(CFGO_HANDLE_NOT_EXIST(MSG)); \
        } \
    }

#define CFGO_DEFINE_CPP_UNREF(NAME, MSG) \
    void unref_ ## NAME(int handle) \
    { \
        std::lock_guard lock(c_mutex); \
        if (auto it = NAME ## _ref_map.find(handle); it != NAME ## _ref_map.end()) \
        { \
            it->second --; \
            if (it->second == 0) \
            { \
                if (NAME ## _map.erase(handle) == 0) \
                { \
                    throw cpptrace::logic_error("Something bug happened."); \
                } \
            } \
        } \
        else \
        { \
            throw cpptrace::invalid_argument(CFGO_HANDLE_NOT_EXIST(MSG)); \
        } \
    }

namespace cfgo
{

    std::unordered_map<int, cfgo::Client::CtxPtr> execution_context_map;
    std::unordered_map<int, int> execution_context_ref_map;
    std::unordered_map<int, cfgo::close_chan_ptr> close_chan_map;
    std::unordered_map<int, int> close_chan_ref_map;
    std::unordered_map<int, cfgo::Client::Ptr> client_map;
    std::unordered_map<int, int> client_ref_map;
    std::unordered_map<int, cfgo::Subscribation::Ptr> subscribation_map;
    std::unordered_map<int, int> subscribation_ref_map;
    std::unordered_map<int, cfgo::Track::Ptr> track_map;
    std::unordered_map<int, int> track_ref_map;

    mutex c_mutex;
    int last_handle = 0;

    CFGO_DEFINE_CPP_GET_REF(execution_context, Client::CtxPtr, "execution context")
    CFGO_DEFINE_CPP_WRAP(execution_context, Client::CtxPtr)
    CFGO_DEFINE_CPP_REF(execution_context, "execution context")
    CFGO_DEFINE_CPP_UNREF(execution_context, "execution context")

    CFGO_DEFINE_CPP_GET_REF(close_chan, close_chan_ptr, "close chan")
    CFGO_DEFINE_CPP_WRAP(close_chan, close_chan_ptr)
    CFGO_DEFINE_CPP_REF(close_chan, "close chan")
    CFGO_DEFINE_CPP_UNREF(close_chan, "close chan")

    CFGO_DEFINE_CPP_GET_REF(client, Client::Ptr, "client")
    CFGO_DEFINE_CPP_WRAP(client, Client::Ptr)
    CFGO_DEFINE_CPP_REF(client, "client")
    CFGO_DEFINE_CPP_UNREF(client, "client")

    CFGO_DEFINE_CPP_GET_REF(subscribation, Subscribation::Ptr, "subscribation")
    CFGO_DEFINE_CPP_WRAP(subscribation, Subscribation::Ptr)
    CFGO_DEFINE_CPP_REF(subscribation, "subscribation")
    CFGO_DEFINE_CPP_UNREF(subscribation, "subscribation")

    CFGO_DEFINE_CPP_GET_REF(track, Track::Ptr, "track")
    CFGO_DEFINE_CPP_WRAP(track, Track::Ptr)
    CFGO_DEFINE_CPP_REF(track, "track")
    CFGO_DEFINE_CPP_UNREF(track, "track")

    rtc::Configuration rtc_config_to_cpp(const rtcConfiguration * config)
    {
        using namespace rtc;
		rtc::Configuration c;
        if (config != nullptr)
        {
            for (int i = 0; i < config->iceServersCount; ++i)
                c.iceServers.emplace_back(config->iceServers[i]);

            if (config->proxyServer)
                c.proxyServer.emplace(config->proxyServer);

            if (config->bindAddress)
                c.bindAddress = config->bindAddress;

            if (config->portRangeBegin > 0 || config->portRangeEnd > 0) {
                c.portRangeBegin = config->portRangeBegin;
                c.portRangeEnd = config->portRangeEnd;
            }

            c.certificateType = static_cast<CertificateType>(config->certificateType);
            c.iceTransportPolicy = static_cast<TransportPolicy>(config->iceTransportPolicy);
            c.enableIceTcp = config->enableIceTcp;
            c.enableIceUdpMux = config->enableIceUdpMux;
            c.disableAutoNegotiation = config->disableAutoNegotiation;
            c.forceMediaTransport = config->forceMediaTransport;

            if (config->mtu > 0)
                c.mtu = size_t(config->mtu);

            if (config->maxMessageSize)
                c.maxMessageSize = size_t(config->maxMessageSize);
        }
        return c;
    }

    std::string cfgo_str_to_cpp(const char * str)
    {
        return str;
    }

    cfgo::Configuration cfgo_config_to_cpp(const cfgoConfiguration * conf)
    {
        if (conf->signal_url == nullptr)
        {
            throw cpptrace::invalid_argument("The signal url is required.");
        }
        if (conf->token == nullptr)
        {
            throw cpptrace::invalid_argument("The token is required.");
        }
        if (conf->rtc_config)
        {
            return {conf->signal_url, conf->token, rtc_config_to_cpp(conf->rtc_config), conf->thread_safe};
        }
        else
        {
            return {conf->signal_url, conf->token, conf->thread_safe};
        }
    }

    template<typename I, typename T>
    std::vector<T> array_field_to_vector(const std::string & field_name, const std::string & len_field_name, const I * array, int array_len, std::function<T(I)> converter)
    {
        std::vector<T> vec;
        if (array)
        {
            if (array_len <= 0)
            {
                throw cpptrace::invalid_argument("Invalid " + len_field_name + ": " + std::to_string(array_len) + " with a valid " + field_name + " parameter.");
            }
            for (size_t i = 0; i < array_len; i++)
            {
                vec.push_back(converter(array[0]));
            }
        }
        else if (array_len > 0)
        {
            throw cpptrace::invalid_argument("Invalid " + len_field_name + ": " + std::to_string(array_len) + " with a null " + field_name + " parameter.");
        }
        return vec;
    }

    void cfgo_pattern_parse(const char * pattern_json, cfgo::Pattern & pattern)
    {
        CPPTRACE_WRAP_BLOCK(
            cfgo::from_json(nlohmann::json::parse(pattern_json), pattern);
        );
    }

    void cfgo_req_types_parse(const char * req_types_str, std::vector<std::string> & req_types)
    {
        req_types.clear();
        if (!req_types_str)
        {
            return;
        }
        boost::split(req_types, std::string_view(req_types_str), boost::is_any_of(" ,\t\r\n"));
        for (auto &&req_type : req_types)
        {
            boost::trim(req_type);
        }
        req_types.erase(
            std::remove_if(req_types.begin(), req_types.end(), [](auto && v) { return v.empty(); }),
            req_types.end()
        );
    }

    template<typename F>
    int c_wrap(F func)
    {
        try
        {
            return int(func());
        }
        catch(...)
        {
            spdlog::error(cfgo::what(std::current_exception()));
            return CFGO_ERR_FAILURE;
        }
    }

    template<typename F, typename R>
    R c_wrap_ret(F func, R err)
    {
        try
        {
            return R(func());
        }
        catch(...)
        {
            spdlog::error(cfgo::what(std::current_exception()));
            return err;
        }
    }

    template<typename F>
    const char * c_wrap_ret_str(F func)
    {
        return c_wrap_ret<F, const char *>(func, nullptr);
    }

    template<typename F>
    void * c_wrap_ret_void_ptr(F func)
    {
        return c_wrap_ret<F, void *>(func, nullptr);
    }
    
    template<typename F>
    const unsigned char * c_wrap_ret_bytes(F func)
    {
        return c_wrap_ret<F, const unsigned char *>(func, nullptr);
    }

} // namespace name

CFGO_API int cfgo_execution_context_create_io_context()
{
    return cfgo::c_wrap([]() {
        return cfgo::wrap_execution_context(std::make_shared<asio::io_context>());
    });
}

CFGO_API int cfgo_execution_context_create_thread_pool(int n)
{
    return cfgo::c_wrap([n]() {
        return cfgo::wrap_execution_context(std::make_shared<asio::thread_pool>(n));
    });
}

CFGO_API int cfgo_execution_context_create_thread_pool_auto()
{
    return cfgo::c_wrap([]() {
        return cfgo::wrap_execution_context(std::make_shared<asio::thread_pool>());
    });
}

CFGO_API int cfgo_execution_context_ref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::ref_execution_context(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_execution_context_unref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::unref_execution_context(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_close_chan_create()
{
    return cfgo::c_wrap([]() {
        return cfgo::wrap_close_chan(std::make_shared<cfgo::close_chan>());
    });
}

CFGO_API int cfgo_close_chan_close(int handle)
{
    return cfgo::c_wrap([handle]() {
        auto chan = cfgo::get_close_chan(handle);
        chan->close();
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_close_chan_ref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::ref_close_chan(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_close_chan_unref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::unref_close_chan(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_client_create(const cfgoConfiguration * config)
{
    return cfgo::c_wrap([config]() {
        return cfgo::wrap_client(std::make_shared<cfgo::Client>(cfgo::cfgo_config_to_cpp(config)));
    });
}

CFGO_API int cfgo_client_ref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::ref_client(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_client_unref(int handle)
{
    return cfgo::c_wrap([handle]() {
        cfgo::unref_client(handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_client_subscribe(
    int client_handle, 
    const char * pattern, 
    const char * req_types, 
    int close_chan_handle,
    cfgoOnSubCallback on_sub_callback, void * user_data
)
{
    return cfgo::c_wrap([=]() {
        auto client = cfgo::get_client(client_handle);
        auto close_chan = close_chan_handle > 0 ? cfgo::get_close_chan(close_chan_handle) : nullptr;
        asio::co_spawn(
            asio::get_associated_executor(client->execution_context()),
            cfgo::fix_async_lambda([=]() -> asio::awaitable<void> {
                std::vector<std::string> arg_req_types;
                cfgo::cfgo_req_types_parse(req_types, arg_req_types);
                cfgo::Pattern p;
                cfgo::cfgo_pattern_parse(pattern, p);
                try
                {
                    cfgo::SubPtr sub;
                    if (close_chan)
                    {
                        sub = co_await client->subscribe(std::move(p), std::move(arg_req_types), *close_chan);
                    }
                    else
                    {
                        sub = co_await client->subscribe(std::move(p), std::move(arg_req_types));
                    }
                    if (on_sub_callback)
                    {
                        if (sub)
                        {
                            on_sub_callback(cfgo::wrap_subscribation(sub), user_data);
                        }
                        else
                        {
                            on_sub_callback(CFGO_ERR_TIMEOUT, user_data);
                        }
                    }
                }
                catch(...)
                {
                    spdlog::error(cfgo::what(std::current_exception()));
                    if (on_sub_callback)
                    {
                        on_sub_callback(CFGO_ERR_FAILURE, user_data);
                    }
                }
            }), 
            asio::detached
        );
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API const char * cfgo_subscribation_get_sub_id(int sub_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto sub = cfgo::get_subscribation(sub_handle);
        return sub->sub_id().c_str();
    });
}

CFGO_API const char * cfgo_subscribation_get_pub_id(int sub_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto sub = cfgo::get_subscribation(sub_handle);
        return sub->pub_id().c_str();
    });
}

CFGO_API int cfgo_subscribation_get_track_count(int sub_handle)
{
    return cfgo::c_wrap([=]() {
        auto sub = cfgo::get_subscribation(sub_handle);
        return sub->tracks().size();
    });
}

CFGO_API int cfgo_subscribation_get_track_at(int sub_handle, int index)
{
    return cfgo::c_wrap([=]() {
        auto sub = cfgo::get_subscribation(sub_handle);
        CPPTRACE_WRAP_BLOCK(
            return cfgo::wrap_track(sub->tracks()[index]);
        );
    });
}

CFGO_API int cfgo_subscribation_ref(int sub_handle)
{
    return cfgo::c_wrap([=]() {
        cfgo::ref_subscribation(sub_handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_subscribation_unref(int sub_handle)
{
    return cfgo::c_wrap([=]() {
        cfgo::unref_subscribation(sub_handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API const char * cfgo_track_get_type(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->type().c_str();
    });
}

CFGO_API const char * cfgo_track_get_pub_id(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->pub_id().c_str();
    });
}

CFGO_API const char * cfgo_track_get_global_id(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->global_id().c_str();
    });
}

CFGO_API const char * cfgo_track_get_bind_id(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->bind_id().c_str();
    });
}

CFGO_API const char * cfgo_track_get_rid(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->rid().c_str();
    });
}

CFGO_API const char * cfgo_track_get_stream_id(int track_handle)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->stream_id().c_str();
    });
}

CFGO_API int cfgo_track_get_label_count(int track_handle)
{
    return cfgo::c_wrap([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->labels().size();
    });
}

CFGO_API const char * cfgo_track_get_label_at(int track_handle, const char * name)
{
    return cfgo::c_wrap_ret_str([=]() {
        auto track = cfgo::get_track(track_handle);
        CPPTRACE_WRAP_BLOCK(
            return track->labels()[name].c_str();
        );
    });
}

CFGO_API void * cfgo_track_get_gst_caps(int track_handle, int payload_type)
{
    return cfgo::c_wrap_ret_void_ptr([=]() {
        auto track = cfgo::get_track(track_handle);
        return track->get_gst_caps(payload_type);
    });
}

CFGO_API const unsigned char * cfgo_track_receive_msg(int track_handle, cfgoMsgType msg_type)
{
    return cfgo::c_wrap_ret_bytes([=]() {
        auto track = cfgo::get_track(track_handle);
        auto msg_ptr = track->receive_msg((cfgo::Track::MsgType) msg_type);
        return msg_ptr ? msg_ptr->data() : nullptr;
    });
}

CFGO_API int cfgo_track_ref(int track_handle)
{
    return cfgo::c_wrap([=]() {
        cfgo::ref_track(track_handle);
        return CFGO_ERR_SUCCESS;
    });
}

CFGO_API int cfgo_track_unref(int track_handle)
{
    return cfgo::c_wrap([=]() {
        cfgo::unref_track(track_handle);
        return CFGO_ERR_SUCCESS;
    });
}