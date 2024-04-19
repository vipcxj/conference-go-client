#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/pbutils/pbutils.h"

#include "cpptrace/cpptrace.hpp"

#include "Poco/URI.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"

#include "spdlog/spdlog.h"

#include "cfgo/client.hpp"
#include "cfgo/track.hpp"
#include "cfgo/subscribation.hpp"
#include "cfgo/async.hpp"
#include "cfgo/defer.hpp"
#include "cfgo/gst/helper.h"
#include <fstream>

GST_DEBUG_CATEGORY_STATIC (appsrc_pipeline_debug);
#define GST_CAT_DEFAULT appsrc_pipeline_debug

#define CHECK_ELEMENT_CREATE(var_name) \
if(!var_name) { \
    g_printerr("The element %s can not be created.\n", #var_name); \
    m_valid = false; \
    return; \
}

struct App;

static GstCaps * request_pt_map(GstElement *src, guint session, guint pt, App *app);
static void pad_added_handler(GstElement *src, GstPad *new_pad, App *app);
static void pad_removed_handler(GstElement * src, GstPad * pad, App *app);
static void no_more_pads_handler(GstElement * src, App *app);
static void on_timeout_handler(GstElement * rtpbin, guint session, guint ssrc, App *app);
static void start_feed_rtp(GstElement *pipeline, guint size, App *app);
static void stop_feed_rtp(GstElement *pipeline, App *app);
static void start_feed_rtcp(GstElement *pipeline, guint size, App *app);
static void stop_feed_rtcp(GstElement *pipeline, App *app);
static gboolean on_bus_message(GstBus *bus, GstMessage *message, App *app);
static GstPadProbeReturn pad_probe(GstPad *pad, GstPadProbeInfo *info, App *app);

void invoke_on(GstElement * bin, const char * name, std::function<void(GstElement *, GstElement *)> cb)
{
    auto element = gst_bin_get_by_name(GST_BIN_CAST(bin), name);
    if (element)
    {
        cb(element, bin);
        gst_object_unref(element);
    }
    else
    {
        spdlog::debug("Unable to find element {} in {}", name, GST_ELEMENT_NAME(bin));
    }
}

void add_probe_to(GstElement * target, const char * pad_name, gpointer userdata)
{
    auto pad = gst_element_get_static_pad(target, pad_name);
    if (pad)
    {
        spdlog::debug("Add probe to pad {} of {}", pad_name, GST_ELEMENT_NAME(target));
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) pad_probe, userdata, NULL);
        gst_object_unref(pad);
    }
    else
    {
        spdlog::debug("Unable to find pad {} on {}", pad_name, GST_ELEMENT_NAME(target));
    }
}

struct App
{
    GstElement *m_pipeline;
    GstElement *m_rtp_src;
    GstElement *m_rtcp_src;
    GstElement *rtpbin;
    GstElement *decodebin;
    GstElement *videoconvert;
    GstElement *encodebin;
    // GstElement *mp4mux;
    GstElement *filesink;

    GstPad *rtp_sink;
    GstPad *rtcp_sink;
    GstPad *encode_video_sink;
    // GstPad *mp4_video_sink;

    GstBus *bus;

    GMainLoop *m_loop;
    guint m_rtp_sourceid;
    guint m_rtcp_sourceid;
    guint watchid;
    // GstClock *m_clock = nullptr;

    cfgo::Track::Ptr m_track;
    bool m_valid;

    App(const cfgo::Track::Ptr & track, GMainLoop *loop): m_track(track), m_loop(loop) {
        m_rtp_sourceid = 0;
        m_rtcp_sourceid = 0;
        watchid = 0;

        m_pipeline = gst_pipeline_new("test-pipeline");
        CHECK_ELEMENT_CREATE(m_pipeline)
        // m_clock = gst_system_clock_obtain();
        // g_object_set(m_clock, "clock-type", GST_CLOCK_TYPE_MONOTONIC, NULL);
        // gst_pipeline_use_clock(GST_PIPELINE_CAST(m_pipeline), m_clock);
        m_rtp_src = gst_element_factory_make("appsrc", "rtpsrc");
        CHECK_ELEMENT_CREATE(m_rtp_src)
        g_object_set(
            m_rtp_src,
            "format", GST_FORMAT_TIME,
            "is-live", TRUE,
            "do-timestamp", TRUE,
            NULL
        );
        m_rtcp_src = gst_element_factory_make("appsrc", "rtcpsrc");
        CHECK_ELEMENT_CREATE(m_rtcp_src)
        g_object_set(
            m_rtcp_src,
            "format", GST_FORMAT_TIME,
            "is-live", TRUE,
            "do-timestamp", TRUE,
            NULL
        );
        rtpbin = gst_element_factory_make("rtpbin", "rtpbin");
        CHECK_ELEMENT_CREATE(rtpbin)
        g_object_set(rtpbin, "add-reference-timestamp-meta", TRUE, NULL);
        decodebin = gst_element_factory_make("decodebin3", "decodebin");
        CHECK_ELEMENT_CREATE(decodebin)
        videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
        CHECK_ELEMENT_CREATE(videoconvert)
        encodebin = gst_element_factory_make("encodebin", "encodebin");
        CHECK_ELEMENT_CREATE(encodebin)
        // mp4mux = gst_element_factory_make("mp4mux", "mp4mux");
        // CHECK_ELEMENT_CREATE(mp4mux)
        filesink = gst_element_factory_make("filesink", "filesink");
        CHECK_ELEMENT_CREATE(filesink)
        g_object_set(filesink, "location", (cfgo::getexedir() / "out.mp4").c_str(), NULL);
        
        gst_bin_add_many(GST_BIN(m_pipeline), m_rtp_src, m_rtcp_src, rtpbin, decodebin, videoconvert, encodebin/* , mp4mux */, filesink, NULL);

        // if (!gst_element_link_many(videoconvert, filesink, NULL))
        // {
        //     g_printerr("Elements could not be linked.\n");
        //     m_valid = false;
        //     return;
        // }

        bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
        if (!bus)
        {
            g_printerr("Unable to achieve the bus.");
            m_valid = false;
            return;
        }
        watchid = gst_bus_add_watch(bus, (GstBusFunc) on_bus_message, this);
        g_signal_connect(m_rtp_src, "need-data", G_CALLBACK(start_feed_rtp), this);
        g_signal_connect(m_rtp_src, "enough-data", G_CALLBACK(stop_feed_rtp), this);
        g_signal_connect(m_rtcp_src, "need-data", G_CALLBACK(start_feed_rtcp), this);
        g_signal_connect(m_rtcp_src, "enough-data", G_CALLBACK(stop_feed_rtcp), this);
        g_signal_connect(rtpbin, "request-pt-map", G_CALLBACK(request_pt_map), this);
        g_signal_connect(rtpbin, "pad-added", G_CALLBACK(pad_added_handler), this);
        g_signal_connect(rtpbin, "pad-removed", G_CALLBACK(pad_removed_handler), this);
        g_signal_connect(rtpbin, "no-more-pads", G_CALLBACK(no_more_pads_handler), this);
        g_signal_connect(decodebin, "pad-added", G_CALLBACK(pad_added_handler), this);
        g_signal_connect(rtpbin, "on-timeout", G_CALLBACK(on_timeout_handler), this);

        rtp_sink = gst_element_request_pad_simple(rtpbin, "recv_rtp_sink_0");
        if (!rtp_sink)
        {
            spdlog::error("Unable to request pad recv_rtp_sink_%u from rtpbin.");
            m_valid = false;
            return;
        }
        {
            auto pad_rtp_src = gst_element_get_static_pad(m_rtp_src, "src");
            if (!pad_rtp_src)
            {
                spdlog::error("Unable to find pad src on rtpsrc.");
                m_valid = false;
                return;
            }
            DEFER({
                gst_object_unref(pad_rtp_src);
            });
            if (GST_PAD_LINK_FAILED(gst_pad_link(pad_rtp_src, rtp_sink)))
            {
                spdlog::error("Unable to link the pad src of rtpsrc to the pad {} of rtpbin", gst_pad_get_name(rtp_sink));
                m_valid = false;
            }
        }
        spdlog::debug("Obtained request pad {} for rtp sender.", GST_PAD_NAME(rtp_sink));

        rtcp_sink = gst_element_request_pad_simple(rtpbin, "recv_rtcp_sink_0");
        if (!rtcp_sink)
        {
            spdlog::error("Unable to request pad recv_rtcp_sink_%u from rtpbin.");
            m_valid = false;
            return;
        }
        {
            auto pad_rtcp_src = gst_element_get_static_pad(m_rtcp_src, "src");
            if (!pad_rtcp_src)
            {
                spdlog::error("Unable to find pad src on rtcpsrc.");
                m_valid = false;
                return;
            }
            DEFER({
                gst_object_unref(pad_rtcp_src);
            });
            if (GST_PAD_LINK_FAILED(gst_pad_link(pad_rtcp_src, rtcp_sink)))
            {
                spdlog::error("Unable to link the pad src of rtcpsrc to the pad {} of rtpbin", GST_PAD_NAME(rtcp_sink));
                m_valid = false;
            }
        }
        spdlog::debug("Obtained request pad {} for rtcp sender.", GST_PAD_NAME(rtcp_sink));

        // mp4_video_sink = gst_element_request_pad_simple(mp4mux, "video_0");
        // if (!mp4_video_sink)
        // {
        //     spdlog::error("Unable to request pad video_0 from mp4mux.");
        //     m_valid = false;
        //     return;
        // }
        // {
        //     auto encodebin_src = gst_element_get_static_pad(encodebin, "src");
        //     if (!encodebin_src)
        //     {
        //         spdlog::error("Unable to find pad src on encodebin.");
        //         m_valid = false;
        //         return;
        //     }
        //     DEFER({
        //         gst_object_unref(encodebin_src);
        //     });
        //     if (GST_PAD_LINK_FAILED(gst_pad_link(encodebin_src, mp4_video_sink)))
        //     {
        //         spdlog::error("Unable to link the pad src of encodebin to the pad {} of mp4mux", GST_PAD_NAME(mp4_video_sink));
        //         m_valid = false;
        //     }
        // }
        // spdlog::debug("Obtained request pad {} for mp4mux.", GST_PAD_NAME(mp4_video_sink));

        {
            // auto caps = gst_pad_get_current_caps(mp4_video_sink);
            // if (!caps)
            // {
            //     caps = gst_pad_query_caps(mp4_video_sink, NULL);
            // }
            // DEFER({
            //     gst_caps_unref(caps);
            // });
            auto caps = gst_caps_new_simple ("video/quicktime", "variant", G_TYPE_STRING, "iso", NULL);
            DEFER({
                gst_caps_unref(caps);
            });
            auto profile = gst_encoding_container_profile_new("Mp4", "mp4", caps, NULL);
            DEFER({
                gst_encoding_profile_unref(profile);
            });
            auto video_caps = gst_caps_from_string("video/x-h264");
            DEFER({
                gst_caps_unref(video_caps);
            });
            gst_encoding_container_profile_add_profile(
                profile,
                GST_ENCODING_PROFILE(gst_encoding_video_profile_new(video_caps, NULL, NULL, 1))
            );
            // auto audio_caps = gst_caps_new_simple (
            //     "audio/mpeg", 
            //     "mpegversion", G_TYPE_INT, 1,
            //     "layer", G_TYPE_INT, 3, 
            //     "channels", G_TYPE_INT, 2, 
            //     "rate", G_TYPE_INT, 44100, 
            //     NULL
            // );
            // DEFER({
            //     gst_caps_unref(audio_caps);
            // });
            // gst_encoding_container_profile_add_profile(
            //     profile,
            //     GST_ENCODING_PROFILE(gst_encoding_audio_profile_new(audio_caps, NULL, NULL, 1))
            // );
            g_object_set(encodebin, "profile", profile, NULL);
        }

        encode_video_sink = gst_element_get_static_pad(encodebin, "video_0");
        if (!encode_video_sink)
        {
            spdlog::error("Unable to request pad video_0 from encodebin.");
            m_valid = false;
            return;
        }
        spdlog::debug("Obtained request pad {} for encodebin.", GST_PAD_NAME(encode_video_sink));
        
        if (!gst_element_link_many(encodebin, filesink, NULL))
        {
            g_printerr("Elements could not be linked.\n");
            m_valid = false;
            return;
        }
    }

    void run() {
        gst_element_set_state (m_pipeline, GST_STATE_PLAYING);
    }

    ~App() {
        // if (m_clock)
        // {
        //     gst_object_unref(m_clock);
        // }
        
        if (m_rtp_sourceid)
        {
            g_source_remove(m_rtp_sourceid);
        }
        if (m_rtcp_sourceid)
        {
            g_source_remove(m_rtcp_sourceid);
        }
        if (m_pipeline)
        {
            gst_element_set_state( m_pipeline, GST_STATE_NULL);
        }
        if (rtp_sink)
        {
            gst_object_unref(rtp_sink);
            gst_element_release_request_pad(rtpbin, rtp_sink);
        }
        if (rtcp_sink)
        {
            gst_object_unref(rtcp_sink);
            gst_element_release_request_pad(rtpbin, rtcp_sink);
        }
        if (encode_video_sink)
        {
            gst_object_unref(encode_video_sink);
            gst_element_release_request_pad(encodebin, encode_video_sink);
        }
        
        // if (mp4_video_sink)
        // {
        //     gst_object_unref(mp4_video_sink);
        //     gst_element_release_request_pad(mp4mux, mp4_video_sink);
        // }
        
        if (m_pipeline)
        {
            gst_object_unref(m_pipeline);
        }
    } 
};

static GstPadProbeReturn pad_probe(GstPad *pad, GstPadProbeInfo *info, App *app)
{
    auto element = gst_pad_get_parent_element(pad);
    if (element)
    {
        spdlog::debug("[{}/{}] data pass", GST_ELEMENT_NAME(element), GST_PAD_NAME(pad));
        gst_object_unref(element);
    }
    return GST_PAD_PROBE_OK;
}

GstCaps * request_pt_map(GstElement *src, guint session, guint pt, App *app)
{
    spdlog::debug("[session {}] reqiest pt {}", session, pt);
    return (GstCaps *) app->m_track->get_gst_caps(pt);
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, App *app)
{
    spdlog::debug("[{}] add pad {}", GST_ELEMENT_NAME(src), GST_PAD_NAME(new_pad));
    cfgo_gst_print_pad_capabilities(src, GST_PAD_NAME(new_pad));
    GstElement *target;
    GstPad *sink_pad;
    if (src == app->decodebin)
    {
        if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "video_"))
        {
            return;
        }
        target = app->encodebin;
        sink_pad = app->encode_video_sink;
    }
    else if (src == app->rtpbin)
    {
        if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_"))
        {
            return;
        }
        target = app->decodebin;
        sink_pad = gst_element_get_static_pad(target, "sink");
    }
    else
    {
        spdlog::error("Unknown source of pad link: {}", GST_ELEMENT_NAME(src));
        return;
    }
    DEFER({
        gst_object_unref(sink_pad);
    });
    
    spdlog::debug("received new pad {} from {}", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));
    if (gst_pad_is_linked(sink_pad))
    {
        spdlog::debug("the {} pad of {} already linked, ignoring", GST_PAD_NAME(sink_pad), GST_ELEMENT_NAME(target));
        return;
    }
    auto new_pad_caps = gst_pad_get_current_caps(new_pad);
    if (!new_pad_caps)
    {
        spdlog::debug("unable to get the cap of the pad {} of {}",  GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));
    }
    else
    {
        DEFER({
            gst_caps_unref(new_pad_caps);
        });
        auto new_pad_struct  = gst_caps_get_structure(new_pad_caps, 0);
        auto new_pad_type = gst_structure_get_name(new_pad_struct);
        spdlog::debug("the pad type of {} is {}", GST_PAD_NAME(new_pad), new_pad_type);
    }
    if (GST_PAD_LINK_FAILED(gst_pad_link(new_pad, sink_pad)))
    {
        spdlog::debug("Link failed.");
    }
    else
    {
        spdlog::debug("Link succeeded. {}.{} -> {}.{}", GST_ELEMENT_NAME(src), GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(target), GST_PAD_NAME(sink_pad));
    }
}

static void pad_removed_handler(GstElement * src, GstPad * pad, App *app)
{
    spdlog::debug("[{}] pad {} removed.", GST_ELEMENT_NAME(src), GST_PAD_NAME(pad));
}

static void no_more_pads_handler(GstElement * src, App *app)
{
    spdlog::debug("[{}] no more pads.", GST_ELEMENT_NAME(src));
}

static void on_timeout_handler(GstElement * rtpbin, guint session, guint ssrc, App *app)
{
    spdlog::debug("[{}/{}] timeout.", session, ssrc);
}

static gboolean read_data(App *app, cfgo::Track::MsgType msg_type)
{
    if (auto msg_ptr = app->m_track->receive_msg(msg_type))
    {
        GstBuffer *buffer;
        
        buffer = gst_buffer_new_and_alloc(msg_ptr->size());
        auto clock = gst_pipeline_get_clock(GST_PIPELINE_CAST(app->m_pipeline));
        DEFER({
            gst_object_unref(clock);
        });
        if (clock)
        {
            auto time_now = gst_clock_get_time(clock);
            GST_BUFFER_PTS(buffer) = GST_BUFFER_DTS(buffer) = time_now - gst_element_get_base_time(app->m_pipeline);
        }
        else
        {
            GST_BUFFER_PTS(buffer) = GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
        }
        GstMapInfo info = GST_MAP_INFO_INIT;
        if (!gst_buffer_map(buffer, &info, GST_MAP_READWRITE))
        {
            return FALSE;
        }
        DEFER({
            gst_buffer_unmap(buffer, &info);
        });
        memcpy(info.data, msg_ptr->data(), msg_ptr->size());
        auto size = gst_buffer_get_size(buffer);
        GstFlowReturn ret;
        if (msg_type == cfgo::Track::MsgType::RTP)
        {
            ret = gst_app_src_push_buffer((GstAppSrc*) app->m_rtp_src, buffer);
        }
        else if (msg_type == cfgo::Track::MsgType::RTCP)
        {
            ret = gst_app_src_push_buffer((GstAppSrc*) app->m_rtcp_src, buffer);
        }
        else
        {
            throw cpptrace::logic_error("only rtp or rtcp is accepted.");
        }
        
        if (ret != GST_FLOW_OK)
        {
            spdlog::debug("not writed and return {}", (int) ret);
        }
        
        // gst_buffer_unref(buffer);
        return ret== GST_FLOW_OK;
    }
    
    return TRUE;
}

static gboolean read_rtp_data(App *app)
{
    return read_data(app, cfgo::Track::MsgType::RTP);
}

static gboolean read_rtcp_data(App *app)
{
    return read_data(app, cfgo::Track::MsgType::RTCP);
}

static void start_feed_rtp(GstElement *pipeline, guint size, App *app) 
{
    if (app->m_rtp_sourceid == 0)
    {
        spdlog::debug("start rtp feeding");
        app->m_rtp_sourceid = g_idle_add((GSourceFunc) read_rtp_data, app);
    }
    
}

static void stop_feed_rtp(GstElement *pipeline, App *app) 
{
    if (app->m_rtp_sourceid != 0)
    {
        spdlog::debug("stop rtp feeding");
        g_source_remove(app->m_rtp_sourceid);
        app->m_rtp_sourceid = 0;
    }
    
}

static void start_feed_rtcp(GstElement *pipeline, guint size, App *app) 
{
    if (app->m_rtcp_sourceid == 0)
    {
        spdlog::debug("start rtcp feeding");
        app->m_rtcp_sourceid = g_idle_add((GSourceFunc) read_rtcp_data, app);
    }
    
}

static void stop_feed_rtcp(GstElement *pipeline, App *app) 
{
    if (app->m_rtcp_sourceid != 0)
    {
        spdlog::debug("stop rtcp feeding");
        g_source_remove(app->m_rtcp_sourceid);
        app->m_rtcp_sourceid = 0;
    }
    
}

static gboolean on_bus_message(GstBus *bus, GstMessage *message, App *app) 
{
    return TRUE;
}

auto get_token() -> std::string {
    using namespace Poco::Net;
    Poco::URI uri("http://localhost:3100/token?key=10000&uid=10000&uname=user10000&role=parent&room=room0&nonce=12345&autojoin=true");
    HTTPClientSession session(uri.getHost(), uri.getPort());
    HTTPRequest request(HTTPRequest::HTTP_GET, uri.getPathAndQuery(), HTTPMessage::HTTP_1_1);
    HTTPResponse response;
    session.sendRequest(request);
    auto&& rs = session.receiveResponse(response);
    if (response.getStatus() != HTTPResponse::HTTP_OK)
    {
        throw cpptrace::runtime_error(fmt::format("unable to get the token. status code: {}.", (int) response.getStatus()));
    }
    return std::string{ std::istreambuf_iterator<char>(rs), std::istreambuf_iterator<char>() };
}

auto main_task(const std::string & token, const cfgo::Client::CtxPtr & io_ctx, GMainLoop *loop) -> asio::awaitable<void> {
    std::cout << "start main task." << std::endl;
    cfgo::Configuration conf { "http://localhost:8080", token };
    cfgo::Client client(conf, io_ctx);
    // client.set_sio_logs_verbose();
    std::cout << "client created." << std::endl;
    cfgo::Pattern pattern {
        cfgo::Pattern::Op::ALL,
        {},
        {
            cfgo::Pattern {
                cfgo::Pattern::Op::TRACK_TYPE,
                { "video" },
                {}
            },
            cfgo::Pattern {
                cfgo::Pattern::Op::TRACK_LABEL_ALL_MATCH,
                { "uid", "0" },
                {}
            }
        }
    };
    auto timeout = cfgo::make_timeout(std::chrono::seconds{30});
    std::cout << "subscribing..." << std::endl;
    auto sub = co_await client.subscribe(pattern, {}, timeout);
    if (!sub)
    {
        throw cpptrace::runtime_error("sub timeout");
    }
    std::cout << "subscribed successfully with sub id: " << sub->sub_id() << ", pub id: " << sub->pub_id() << std::endl;
    App app {sub->tracks()[0], loop};
    app.run();
    co_await cfgo::wait_timeout(std::chrono::seconds{3});
    invoke_on(app.rtpbin, "rtpjitterbuffer0", [&app](GstElement *target, GstElement *bin) {
        // add_probe_to(target, "sink", &app);
        // add_probe_to(target, "src", &app);
    });
    auto graph = cfgo::getexedir() / "pipeline.dot";
    spdlog::debug("writing pipeline graph to {}", graph.c_str());
    auto dot_data = gst_debug_bin_to_dot_data(GST_BIN_CAST(app.m_pipeline), GST_DEBUG_GRAPH_SHOW_VERBOSE);
    {
        std::ofstream dot_file(graph);
        dot_file << dot_data;
        spdlog::debug("writed");
    }
    g_free(dot_data);
    co_await cfgo::wait_timeout(std::chrono::seconds{120});
}

int main(int argc, char **argv) {

    gst_init(&argc, &argv);
    GST_DEBUG_CATEGORY_INIT (appsrc_pipeline_debug, "appsrc-pipeline", 1, "appsrc pipeline example");
    spdlog::set_level(spdlog::level::debug);

    //gst_registry_scan_path(gst_registry_get(), (cfgo::getexedir() / "plugins/gstreamer").c_str());

    auto plugins = gst_registry_get_plugin_list(gst_registry_get());
    DEFER({
        gst_plugin_list_free(plugins);
    });
    int plugins_num = 0;
    while (plugins)
    {
        auto plugin = (GstPlugin *) (plugins->data);
        plugins = g_list_next(plugins);
        g_print("plugin: %s\n", gst_plugin_get_name(plugin));
        ++plugins_num;
    }
    g_print("found %d plugins\n", plugins_num);

    auto token = get_token();
    std::cout << "got token: " << token << std::endl;
    auto pool = std::make_shared<asio::thread_pool>();
    GMainLoop *loop = g_main_loop_new(NULL, TRUE);
    asio::co_spawn(*pool, main_task(token, pool, loop), asio::detached);
    g_main_loop_run(loop);
    GST_DEBUG("stopping");
    g_main_loop_unref(loop);
}