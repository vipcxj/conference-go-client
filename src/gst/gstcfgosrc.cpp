/**
 * SECTION:element-gstcfgosrc
 *
 * The cfgosrc element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v fakesrc ! cfgosrc ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "cfgo/gst/gstcfgosrc.h"
#include "cfgo/gst/gstcfgosrc_private_api.hpp"
#include "cfgo/gst/cfgosrc.hpp"
#include "cfgo/gst/error.hpp"
#include "cfgo/gst/utils.hpp"
#include "cfgo/async.hpp"
#include "cfgo/fmt.hpp"
#include "cfgo/capi.h"
#include "cfgo/cbridge.hpp"
#include "cfgo/client.hpp"
#include "cfgo/defer.hpp"
#include "asio.hpp"

GST_DEBUG_CATEGORY_STATIC(gst_cfgosrc_debug_category);
#define GST_CAT_DEFAULT gst_cfgosrc_debug_category

/* prototypes */

namespace cfgo
{
    namespace gst
    {
        struct GstCfgoSrcPrivateState
        {
            std::mutex mutex;
            bool running;
            cfgo::gst::CfgoSrc::Ptr task;

            GstCfgoSrcPrivateState()
            {
                spdlog::debug("new GstCfgoSrcPrivateState.");
            }

            ~GstCfgoSrcPrivateState()
            {
                spdlog::debug("delete GstCfgoSrcPrivateState.");
            }
        };
    } // namespace gst
    
} // namespace cfgo

struct _GstCfgoSrcPrivate
{
    cfgo::gst::GstCfgoSrcPrivateState * state;
    GstElement * rtpsrc;
    GstElement * rtcpsrc;
    GstElement * parsebin;
    GstElement * decodebin;
};

#define GST_CFGOSRC_PVS(cfgosrc) cfgosrc->priv->state

#define GST_CFGOSRC_ACTUALLY_JOIN(x, y) x##y
#define GST_CFGOSRC_JOIN(x, y) GST_CFGOSRC_ACTUALLY_JOIN(x, y)
#ifdef __COUNTER__
#define GST_CFGOSRC_LOCK_GUARD_VARNAME(x) GST_CFGOSRC_JOIN(x, __COUNTER__)
#else
#define GST_CFGOSRC_LOCK_GUARD_VARNAME(x) GST_CFGOSRC_JOIN(x, __LINE__)
#endif
#define GST_CFGOSRC_LOCK_GUARD(cfgosrc) std::lock_guard GST_CFGOSRC_LOCK_GUARD_VARNAME(guard)(GST_CFGOSRC_PVS(cfgosrc)->mutex)

static void gst_cfgosrc_set_property(GObject *object,
                                     guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_cfgosrc_get_property(GObject *object,
                                     guint property_id, GValue *value, GParamSpec *pspec);
static void gst_cfgosrc_dispose(GObject *object);
static void gst_cfgosrc_finalize(GObject *object);

static GstPad *gst_cfgosrc_request_new_pad(GstElement *element,
                                           GstPadTemplate *templ, const gchar *name);
static void gst_cfgosrc_release_pad(GstElement *element, GstPad *pad);
static GstStateChangeReturn
gst_cfgosrc_change_state(GstElement *element, GstStateChange transition);

#define DEFAULT_GST_CFGO_SRC_MODE GST_CFGO_SRC_MODE_RAW

enum
{
    PROP_0,
    PROP_CLIENT,
    PROP_PATTERN,
    PROP_REQ_TYPES,
    PROP_SUB_TIMEOUT,
    PROP_SUB_TRIES,
    PROP_SUB_TRY_DELAY_INIT,
    PROP_SUB_TRY_DELAY_STEP,
    PROP_SUB_TRY_DELAY_LEVEL,
    PROP_READ_TIMEOUT,
    PROP_READ_TRIES,
    PROP_READ_TRY_DELAY_INIT,
    PROP_READ_TRY_DELAY_STEP,
    PROP_READ_TRY_DELAY_LEVEL,
    PROP_MODE,
    PROP_DECODE_CAPS
};

#define GST_CFGO_SRC_MODE_TYPE (gst_cfgo_src_mode_get_type())
static GType
gst_cfgo_src_mode_get_type (void)
{
  static GType mode_type = 0;
  static const GEnumValue mode_types[] = {
    {GST_CFGO_SRC_MODE_RAW, "raw", "raw"},
    {GST_CFGO_SRC_MODE_PARSE, "parse", "parse"},
    {GST_CFGO_SRC_MODE_DECODE, "decode", "decode"},
    {0, NULL, NULL},
  };

  if (!mode_type) {
    mode_type = g_enum_register_static ("GstCfgoSrcMode", mode_types);
  }
  return mode_type;
}

/* pad templates */

static GstStaticPadTemplate gst_cfgosrc_rtp_src_template =
    GST_STATIC_PAD_TEMPLATE("rtp_src_%u_%u_%u",
                            GST_PAD_SRC,
                            GST_PAD_SOMETIMES,
                            GST_STATIC_CAPS("application/x-rtp"));

static GstStaticPadTemplate gst_cfgosrc_parse_src_template =
    GST_STATIC_PAD_TEMPLATE("parse_src_%u_%u_%u_%u",
                            GST_PAD_SRC,
                            GST_PAD_SOMETIMES,
                            GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_cfgosrc_decode_src_template =
    GST_STATIC_PAD_TEMPLATE("decode_src_%u_%u_%u_%u",
                            GST_PAD_SRC,
                            GST_PAD_SOMETIMES,
                            GST_STATIC_CAPS_ANY);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstCfgoSrc, gst_cfgosrc, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT(
        gst_cfgosrc_debug_category, "cfgosrc", 0,
        "debug category for cfgosrc element"
    );
    G_ADD_PRIVATE(GstCfgoSrc)
);

namespace cfgo
{
    namespace gst
    {
        void rtp_src_set_callbacks(GstCfgoSrc * parent, GstAppSrcCallbacks & callbacks, gpointer user_data, GDestroyNotify notify)
        {
            gst_app_src_set_callbacks(GST_APP_SRC(parent->priv->rtpsrc), &callbacks, user_data, notify);
        }

        void rtp_src_remove_callbacks(GstCfgoSrc * parent)
        {
            GstAppSrcCallbacks cbs {};
            gst_app_src_set_callbacks(GST_APP_SRC(parent->priv->rtpsrc), &cbs, nullptr, nullptr);
        }

        void rtcp_src_set_callbacks(GstCfgoSrc * parent, GstAppSrcCallbacks & callbacks, gpointer user_data, GDestroyNotify notify)
        {
            gst_app_src_set_callbacks(GST_APP_SRC(parent->priv->rtcpsrc), &callbacks, user_data, notify);
        }

        void rtcp_src_remove_callbacks(GstCfgoSrc * parent)
        {
            GstAppSrcCallbacks cbs {};
            gst_app_src_set_callbacks(GST_APP_SRC(parent->priv->rtcpsrc), &cbs, nullptr, nullptr);
        }

        void link_rtp_src(GstCfgoSrc * parent, GstPad * pad)
        {
            auto src_pad = gst_element_get_static_pad(parent->priv->rtpsrc, "src");
            if (!src_pad)
            {
                throw cpptrace::range_error("Unable to get the pad src from rtpsrc.");
            }
            DEFER({
                gst_object_unref(src_pad);
            });
            if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, pad)))
            {
                throw cpptrace::range_error("Unable to link the rtp src pad.");
            }
        }

        void link_rtcp_src(GstCfgoSrc * parent, GstPad * pad)
        {
            auto src_pad = gst_element_get_static_pad(parent->priv->rtcpsrc, "src");
            if (!src_pad)
            {
                throw cpptrace::range_error("Unable to get the pad src from rtcpsrc.");
            }
            DEFER({
                gst_object_unref(src_pad);
            });
            if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, pad)))
            {
                throw cpptrace::range_error("Unable to link the rtcp src pad.");
            }
        }

        GstFlowReturn push_rtp_buffer(GstCfgoSrc * parent, GstBuffer * buffer)
        {
            return gst_app_src_push_buffer(GST_APP_SRC(parent->priv->rtpsrc), buffer);
        }

        GstFlowReturn push_rtcp_buffer(GstCfgoSrc * parent, GstBuffer * buffer)
        {
            return gst_app_src_push_buffer(GST_APP_SRC(parent->priv->rtcpsrc), buffer);
        }
    } // namespace gst
    
} // namespace cfgo

static void
gst_cfgosrc_class_init(GstCfgoSrcClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_static_pad_template(element_class,
                                              &gst_cfgosrc_rtp_src_template);
    gst_element_class_add_static_pad_template(element_class,
                                              &gst_cfgosrc_parse_src_template);
    gst_element_class_add_static_pad_template(element_class,
                                              &gst_cfgosrc_decode_src_template);

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                          "FIXME Long name", "Generic", "FIXME Description",
                                          "FIXME <fixme@example.com>");

    gobject_class->set_property = gst_cfgosrc_set_property;
    gobject_class->get_property = gst_cfgosrc_get_property;
    gobject_class->dispose = gst_cfgosrc_dispose;
    gobject_class->finalize = gst_cfgosrc_finalize;
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_cfgosrc_change_state);

    g_object_class_install_property(
        gobject_class, PROP_CLIENT,
        g_param_spec_int(
            "client", "client", "The cfgo client",
            0, G_MAXINT, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_PATTERN,
        g_param_spec_string(
            "pattern", "pattern", "The pattern for subscribing",
            NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_REQ_TYPES,
        g_param_spec_string(
            "req-types", "req-types", "The reqire types for subscribing, \"video\", \"audio\" or \"video,audio\"",
            NULL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SUB_TIMEOUT,
        g_param_spec_uint64(
            "sub-timeout", "sub-timeout", "timeout miliseconds of subscribing",
            0, G_MAXUINT64, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SUB_TRIES,
        g_param_spec_int(
            "sub-tries", "sub-tries", "max tries of subscribing, -1 means forever",
            -1, G_MAXINT32, -1,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SUB_TRY_DELAY_INIT,
        g_param_spec_uint64(
            "sub-try-delay-init", "sub-try-delay-init", "the init delay time in milisecond before next subscribing try",
            0, G_MAXUINT64, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SUB_TRY_DELAY_STEP,
        g_param_spec_uint(
            "sub-try-delay-step", "sub-try-delay-step", "the increase step of delay time in milisecond before next subscribing try.",
            0, G_MAXINT32, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_SUB_TRY_DELAY_LEVEL,
        g_param_spec_uint(
            "sub-try-delay-level", "sub-try-delay-level", "the max increase level of delay time before next subscribing try.",
            0, 16, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_READ_TIMEOUT,
        g_param_spec_uint64(
            "read-timeout", "read-timeout", "timeout miliseconds of reading data",
            0, G_MAXUINT64, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_READ_TRIES,
        g_param_spec_int(
            "read-tries", "read-tries", "max tries of reading data, -1 means forever",
            -1, G_MAXINT32, -1,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_READ_TRY_DELAY_INIT,
        g_param_spec_uint64(
            "read-try-delay-init", "read-try-delay-init", "the init delay time in milisecond before next reading data try",
            0, G_MAXUINT64, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_READ_TRY_DELAY_STEP,
        g_param_spec_uint(
            "read-try-delay-step", "read-try-delay-step", "the increase step of delay time in milisecond before next reading data try.",
            0, G_MAXINT32, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_READ_TRY_DELAY_LEVEL,
        g_param_spec_uint(
            "read-try-delay-level", "read-try-delay-level", "the max increase level of delay time before next reading data try.",
            0, 16, 0,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_MODE,
        g_param_spec_enum (
            "mode", "mode", "Control the exposed pad type", 
            GST_CFGO_SRC_MODE_TYPE, DEFAULT_GST_CFGO_SRC_MODE, 
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_DECODE_CAPS,
        g_param_spec_boxed ("decode-caps", "Decode Caps",
            "The caps on which to stop decoding. (NULL = default)",
            GST_TYPE_CAPS,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}


static void
gst_cfgosrc_init(GstCfgoSrc *cfgosrc)
{
    cfgosrc->priv = (GstCfgoSrcPrivate *)gst_cfgosrc_get_instance_private(cfgosrc);
    GST_CFGOSRC_PVS(cfgosrc) = new cfgo::gst::GstCfgoSrcPrivateState();
    auto rtpsrc = gst_element_factory_make("appsrc", "rtpsrc");
    if (!rtpsrc)
    {
        throw cpptrace::runtime_error("Unable to create element rtpsrc for the parent cfgosrc.");
    }
    g_object_set(
        rtpsrc,
        "format", GST_FORMAT_TIME,
        "is-live", TRUE,
        "do-timestamp", TRUE,
        NULL
    );
    gst_bin_add(GST_BIN(cfgosrc), rtpsrc);
    cfgosrc->priv->rtpsrc = rtpsrc;
    auto rtcpsrc = gst_element_factory_make("appsrc", "rtcpsrc");
    if (!rtcpsrc)
    {
        throw cpptrace::runtime_error("Unable to create element rtcpsrc for the parent cfgosrc.");
    }
    g_object_set(
        rtcpsrc,
        "format", GST_FORMAT_TIME,
        "is-live", TRUE,
        "do-timestamp", TRUE,
        NULL
    );
    gst_bin_add(GST_BIN(cfgosrc), rtcpsrc);
    cfgosrc->priv->rtcpsrc = rtcpsrc;
}

void _gst_cfgosrc_prepare(GstCfgoSrc *cfgosrc, bool reset_task)
{
    GST_DEBUG_OBJECT(cfgosrc, "%s", "_gst_cfgosrc_prepare");
    if (GST_CFGOSRC_PVS(cfgosrc)->running)
    {
        if (cfgosrc->client_handle > 0 
            && cfgosrc->pattern && strlen(cfgosrc->pattern) > 0)
        {
            if (!GST_CFGOSRC_PVS(cfgosrc)->task || reset_task)
            {
                GST_DEBUG_OBJECT(cfgosrc, "%s", "Creating the task.");
                spdlog::debug("cfgosrc created with sub timeout {} and read timeout {}.", cfgosrc->sub_timeout, cfgosrc->read_timeout);
                GST_CFGOSRC_PVS(cfgosrc)->task = cfgo::gst::CfgoSrc::create(
                    cfgosrc->client_handle, 
                    cfgosrc->pattern, cfgosrc->req_types, 
                    cfgosrc->sub_timeout, cfgosrc->read_timeout
                );
                GST_DEBUG_OBJECT(cfgosrc, "%s", "Attaching the task.");
                GST_CFGOSRC_PVS(cfgosrc)->task->attach(cfgosrc);
                if (cfgosrc->decode_caps)
                {
                    GST_CFGOSRC_PVS(cfgosrc)->task->set_decode_caps(cfgosrc->decode_caps);
                }
            }
            else
            {
                GST_DEBUG_OBJECT(cfgosrc, "%s", "The task has created and started, updating timeout properties.");
                GST_CFGOSRC_PVS(cfgosrc)->task->set_sub_timeout(cfgosrc->sub_timeout);
                GST_CFGOSRC_PVS(cfgosrc)->task->set_sub_timeout(cfgosrc->read_timeout);
            }
            GST_DEBUG_OBJECT(cfgosrc, "%s", "Updating the try options of the task.");
            GST_CFGOSRC_PVS(cfgosrc)->task->set_sub_try(
                cfgosrc->sub_tries,
                cfgosrc->sub_try_delay_init,
                cfgosrc->sub_try_delay_step,
                cfgosrc->sub_try_delay_level
            );
            GST_CFGOSRC_PVS(cfgosrc)->task->set_read_try(
                cfgosrc->read_tries,
                cfgosrc->read_try_delay_init,
                cfgosrc->read_try_delay_step,
                cfgosrc->read_try_delay_level
            );
        }
    }
}

void gst_cfgosrc_start(GstCfgoSrc *cfgosrc)
{
    GST_DEBUG_OBJECT(cfgosrc, "%s", "gst_cfgosrc_start");
    GST_CFGOSRC_LOCK_GUARD(cfgosrc);
    GST_CFGOSRC_PVS(cfgosrc)->running = true;
    _gst_cfgosrc_prepare(cfgosrc, false);
    if (GST_CFGOSRC_PVS(cfgosrc)->task)
    {
        GST_DEBUG_OBJECT(cfgosrc, "%s", "start task, if already started, no side effect");
        GST_CFGOSRC_PVS(cfgosrc)->task->start();
        GST_DEBUG_OBJECT(cfgosrc, "%s", "resume task");
        GST_CFGOSRC_PVS(cfgosrc)->task->resume();
    }
}

void gst_cfgosrc_stop(GstCfgoSrc *cfgosrc)
{
    GST_DEBUG_OBJECT(cfgosrc, "%s", "gst_cfgosrc_stop");
    GST_CFGOSRC_LOCK_GUARD(cfgosrc);
    GST_CFGOSRC_PVS(cfgosrc)->running = false;
    if (GST_CFGOSRC_PVS(cfgosrc)->task)
    {
        GST_CFGOSRC_PVS(cfgosrc)->task->pause();
    }
}

bool gst_cfgosrc_set_string_property(const GValue *value, gchar ** target)
{
    const gchar * src = g_value_get_string(value);
    if (src == *target)
    {
        return false;
    }
    else if (src == nullptr)
    {
        g_free(*target);
        *target = nullptr;
        return true;
    }
    else if (*target == nullptr)
    {
        *target = g_strdup(src);
        return true;
    }
    else if (g_str_equal(src, *target))
    {
        return false;
    }
    else
    {
        g_free(*target);
        *target = g_strdup(src);
        return true;
    }
}

bool gst_cfgosrc_set_int32_property(const GValue * value, gint32 * target)
{
    auto src = g_value_get_int(value);
    if (src == *target)
    {
        return false;
    }
    else
    {
        *target = src;
        return true;
    }
}

bool gst_cfgosrc_set_uint32_property(const GValue * value, guint32 * target)
{
    auto src = g_value_get_uint(value);
    if (src == *target)
    {
        return false;
    }
    else
    {
        *target = src;
        return true;
    }
}

bool gst_cfgosrc_set_uint64_property(const GValue * value, guint64 * target)
{
    auto src = g_value_get_uint64(value);
    if (src == *target)
    {
        return false;
    }
    else
    {
        *target = src;
        return true;
    }
}

bool gst_cfgosrc_set_enum_property(const GValue * value, gint * target)
{
    auto src = g_value_get_enum(value);
    if (src == *target)
    {
        return false;
    }
    else
    {
        *target = src;
        return true;
    }
}

void gst_cfgosrc_set_property(GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
    GstCfgoSrc *cfgosrc = GST_CFGOSRC(object);

    GST_DEBUG_OBJECT(cfgosrc, "set_property");

    switch (property_id)
    {
    case PROP_CLIENT:
    {
        int handle = g_value_get_int(value);
        if (handle > 0)
        {
            GST_CFGOSRC_LOCK_GUARD(cfgosrc);
            if (handle == cfgosrc->client_handle)
            {
                break;
            }
            if (cfgosrc->client_handle > 0)
            {
                cfgo_client_unref(cfgosrc->client_handle);
            }
            cfgo_client_ref(handle);
            cfgosrc->client_handle = handle;
            GST_DEBUG_OBJECT(cfgosrc, "Client argument was changed to %d\n", handle);
            _gst_cfgosrc_prepare(cfgosrc, true);
        }
        else
        {
            GST_WARNING_OBJECT(cfgosrc, "Accept an invalid client handle: %d.\n", handle);
        }
        break;
    }
    case PROP_PATTERN:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_string_property(value, &cfgosrc->pattern))
        {
             _gst_cfgosrc_prepare(cfgosrc, true);
            GST_DEBUG_OBJECT(cfgosrc, "The pattern argument was changed to %s\n", cfgosrc->pattern);
        }
        break;
    }
    case PROP_REQ_TYPES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_string_property(value, &cfgosrc->req_types))
        {
             _gst_cfgosrc_prepare(cfgosrc, true);
            GST_DEBUG_OBJECT(cfgosrc, "The req-types argument was changed to %s\n", cfgosrc->req_types);
        }
        break;
    }
    case PROP_SUB_TIMEOUT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint64_property(value, &cfgosrc->sub_timeout))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The sub-timeout argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->sub_timeout));
        }
        break;
    }
    case PROP_SUB_TRIES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_int32_property(value, &cfgosrc->sub_tries))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The sub-tries argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->sub_tries));
        }
        break;
    }
    case PROP_SUB_TRY_DELAY_INIT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint64_property(value, &cfgosrc->sub_try_delay_init))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The sub-try-delay-init argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->sub_try_delay_init));
        }
        break;
    }
    case PROP_SUB_TRY_DELAY_STEP:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint32_property(value, &cfgosrc->sub_try_delay_step))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The sub-try-delay-step argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->sub_try_delay_step));
        }
        break;
    }
    case PROP_SUB_TRY_DELAY_LEVEL:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint32_property(value, &cfgosrc->sub_try_delay_level))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The sub-try-delay-level argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->sub_try_delay_level));
        }
        break;
    }
    case PROP_READ_TIMEOUT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint64_property(value, &cfgosrc->read_timeout))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The read-timeout argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->read_timeout));
        }
        break;
    }
    case PROP_READ_TRIES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_int32_property(value, &cfgosrc->read_tries))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The read-tries argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->read_tries));
        }
        break;
    }
    case PROP_READ_TRY_DELAY_INIT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint64_property(value, &cfgosrc->read_try_delay_init))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The read-try-delay-init argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->read_try_delay_init));
        }
        break;
    }
    case PROP_READ_TRY_DELAY_STEP:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint32_property(value, &cfgosrc->read_try_delay_step))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The read-try-delay-step argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->read_try_delay_step));
        }
        break;
    }
    case PROP_READ_TRY_DELAY_LEVEL:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_uint32_property(value, &cfgosrc->read_try_delay_level))
        {
            GST_DEBUG_OBJECT(cfgosrc, "The read-try-delay-level argument was changed to %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(cfgosrc->read_try_delay_level));
        }
        break;
    }
    case PROP_MODE:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (gst_cfgosrc_set_enum_property(value, (gint *) &cfgosrc->mode))
        {
            auto smode = g_enum_to_string(GST_CFGO_SRC_MODE_TYPE, cfgosrc->mode);
            GST_DEBUG_OBJECT(cfgosrc, "The mode argument was changed to %s\n", smode);
            g_free(smode);
            if (GST_CFGOSRC_PVS(cfgosrc)->task)
            {
                GST_CFGOSRC_PVS(cfgosrc)->task->switch_mode(cfgosrc->mode);
            }    
        }
        break;
    }
    case PROP_DECODE_CAPS:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        GstCaps * caps = GST_CAPS(g_value_dup_boxed(value));
        if (cfgosrc->decode_caps == nullptr && caps == nullptr)
        {
            gst_caps_unref(caps);
            break;
        }
        else if (caps == nullptr)
        {
            gst_caps_unref(cfgosrc->decode_caps);
            cfgosrc->decode_caps = nullptr;
        }
        else if (cfgosrc->decode_caps == nullptr)
        {
            cfgosrc->decode_caps = caps;
        }
        else if (gst_caps_is_strictly_equal(cfgosrc->decode_caps, caps))
        {
            gst_caps_unref(caps);
            break;
        }
        else
        {
            gst_caps_unref(cfgosrc->decode_caps);
            cfgosrc->decode_caps = caps;
        }
        if (GST_CFGOSRC_PVS(cfgosrc)->task)
        {
            GST_CFGOSRC_PVS(cfgosrc)->task->set_decode_caps(cfgosrc->decode_caps);
        }
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_cfgosrc_get_property(GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
    GstCfgoSrc *cfgosrc = GST_CFGOSRC(object);

    GST_DEBUG_OBJECT(cfgosrc, "get_property");

    switch (property_id)
    {
    case PROP_CLIENT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_int(value, cfgosrc->client_handle);
        break;
    }
    case PROP_PATTERN:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_string(value, cfgosrc->pattern);
        break;
    }
    case PROP_REQ_TYPES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_string(value, cfgosrc->req_types);
    }
    case PROP_SUB_TIMEOUT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint64(value, cfgosrc->sub_timeout);
        break;
    }
    case PROP_SUB_TRIES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_int(value, cfgosrc->sub_tries);
        break;
    }
    case PROP_SUB_TRY_DELAY_INIT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint64(value, cfgosrc->sub_try_delay_init);
        break;
    }
    case PROP_SUB_TRY_DELAY_STEP:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint(value, cfgosrc->sub_try_delay_step);
        break;
    }
    case PROP_SUB_TRY_DELAY_LEVEL:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint(value, cfgosrc->sub_try_delay_level);
        break;
    }
    case PROP_READ_TIMEOUT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint64(value, cfgosrc->read_timeout);
        break;
    }
    case PROP_READ_TRIES:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_int(value, cfgosrc->read_tries);
        break;
    }
    case PROP_READ_TRY_DELAY_INIT:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint64(value, cfgosrc->read_try_delay_init);
        break;
    }
    case PROP_READ_TRY_DELAY_STEP:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint(value, cfgosrc->read_try_delay_step);
        break;
    }
    case PROP_READ_TRY_DELAY_LEVEL:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_uint(value, cfgosrc->read_try_delay_level);
        break;
    }
    case PROP_MODE:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_enum(value, cfgosrc->mode);
        break;
    }
    case PROP_DECODE_CAPS:
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        g_value_set_boxed (value, cfgosrc->decode_caps);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_cfgosrc_dispose(GObject *object)
{
    GstCfgoSrc *cfgosrc = GST_CFGOSRC(object);

    GST_DEBUG_OBJECT(cfgosrc, "%s", "dispose");
    {
        GST_CFGOSRC_LOCK_GUARD(cfgosrc);
        if (GST_CFGOSRC_PVS(cfgosrc)->task)
        {
            GST_CFGOSRC_PVS(cfgosrc)->task->detach();
            spdlog::debug("cfgosrc destroy.");
            GST_CFGOSRC_PVS(cfgosrc)->task.reset();
        }
    }
    G_OBJECT_CLASS(gst_cfgosrc_parent_class)->dispose(object);
}

void gst_cfgosrc_finalize(GObject *object)
{
    GstCfgoSrc *cfgosrc = GST_CFGOSRC(object);

    GST_DEBUG_OBJECT(cfgosrc, "%s", "finalize");
    if (GST_CFGOSRC_PVS(cfgosrc))
    {
        delete(GST_CFGOSRC_PVS(cfgosrc));
    }
    
    if (cfgosrc->client_handle > 0)
    {
        cfgo_client_unref(cfgosrc->client_handle);
    }
    if (cfgosrc->pattern)
    {
        g_free(cfgosrc->pattern);
    }
    if (cfgosrc->req_types)
    {
        g_free(cfgosrc->req_types);
    }
    if (cfgosrc->decode_caps)
    {
        gst_caps_unref(cfgosrc->decode_caps);
    }
    
    
    G_OBJECT_CLASS(gst_cfgosrc_parent_class)->finalize(object);
}

static GstStateChangeReturn
gst_cfgosrc_change_state(GstElement *element, GstStateChange transition)
{
    GstCfgoSrc *cfgosrc;
    GstStateChangeReturn ret;

    g_return_val_if_fail(GST_IS_CFGOSRC(element), GST_STATE_CHANGE_FAILURE);
    cfgosrc = GST_CFGOSRC(element);

    switch (transition)
    {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        gst_cfgosrc_start(cfgosrc);
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS (gst_cfgosrc_parent_class)->change_state(element, transition);

    switch (transition)
    {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_cfgosrc_stop(cfgosrc);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}

static gboolean
plugin_init(GstPlugin *plugin)
{

    /* FIXME Remember to set the rank if it's an element that is meant
       to be autoplugged by decodebin. */
    return gst_element_register(plugin, "cfgosrc", GST_RANK_NONE,
                                GST_TYPE_CFGOSRC);
}

/* FIXME: these are normally defined by the GStreamer build system.
   If you are creating an element to be included in gst-plugins-*,
   remove these, as they're always defined.  Otherwise, edit as
   appropriate for your external plugin package. */
#ifndef VERSION
#define VERSION "0.0.FIXME"
#endif
#ifndef PACKAGE
#define PACKAGE "FIXME_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "FIXME_package_name"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://FIXME.org/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  cfgosrc,
                  "FIXME plugin description",
                  plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
