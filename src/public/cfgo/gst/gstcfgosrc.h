#ifndef _CFGO_GST_GSTCFGOSRC_H_
#define _CFGO_GST_GSTCFGOSRC_H_

#include <gst/gst.h>
#include "cfgo/exports.h"
#include "cfgo/gst/boxed.h"

G_BEGIN_DECLS

#define CFGO_TYPE_BOXED_TRACK (cfgo_boxed_track_get_type())
CFGO_DECLARE_BOXED_PTR_LIKE(Track, track)

#define GST_TYPE_CFGOSRC (gst_cfgosrc_get_type())
#define GST_CFGOSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CFGOSRC, GstCfgoSrc))
#define GST_CFGOSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CFGOSRC, GstCfgoSrcClass))
#define GST_IS_CFGOSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CFGOSRC))
#define GST_IS_CFGOSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CFGOSRC))

typedef struct _GstCfgoSrc GstCfgoSrc;
typedef struct _GstCfgoSrcClass GstCfgoSrcClass;
typedef struct _GstCfgoSrcPrivate GstCfgoSrcPrivate;

typedef enum {
    GST_CFGO_SRC_MODE_RAW,
    GST_CFGO_SRC_MODE_PARSE,
    GST_CFGO_SRC_MODE_DECODE
} GstCfgoSrcMode;

struct _GstCfgoSrc
{
    GstBin bin;

    int client_handle;
    gchar *pattern;
    gchar *req_types;
    guint64 sub_timeout;
    gint32 sub_tries;
    guint64 sub_try_delay_init;
    guint32 sub_try_delay_step;
    guint32 sub_try_delay_level;
    guint64 read_timeout;
    gint32 read_tries;
    guint64 read_try_delay_init;
    guint32 read_try_delay_step;
    guint32 read_try_delay_level;
    GstCfgoSrcMode mode;
    GstCaps * decode_caps;

    /*< private >*/
    GstCfgoSrcPrivate *priv;
};

struct _GstCfgoSrcClass
{
    GstBinClass parent_class;

    void (*decodebin_created) (GstElement * element, GstElement * decodebin);
    void (*parsebin_created) (GstElement * element, GstElement * decodebin);
    GstBuffer * (*buffer_allocate) (GstElement * element);
    void (*on_track) (GstElement * element, CfgoBoxedTrack * track);
};

GType gst_cfgosrc_get_type(void);

CFGO_API_WITHOUT_EXTERN GST_PLUGIN_STATIC_DECLARE(cfgosrc);

G_END_DECLS

#endif
