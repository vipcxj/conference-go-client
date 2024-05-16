#ifndef _CFGO_GST_ERROR_H_
#define _CFGO_GST_ERROR_H_

#include "glib.h"
#include "gst/gst.h"

G_BEGIN_DECLS

typedef enum
{
    CFGO_ERROR_TIMEOUT,
    CFGO_ERROR_GENERAL
} CfgoError;

#define CFGO_ERR (cfgo_error_quark ())
GQuark cfgo_error_quark (void);

void cfgo_error_set_timeout (GError ** error, const gchar * message, gboolean trace);
void cfgo_error_set_general (GError ** error, const gchar * message, gboolean trace);
const gchar * cfgo_error_get_trace (GError *error);
const gchar * cfgo_error_get_message (GError *error);

void cfgo_error_submit (GstElement * src, GError * error);
void cfgo_error_submit_timeout (GstElement * src, const gchar * message, gboolean log, gboolean trace);
void cfgo_error_submit_general (GstElement * src, const gchar * message, gboolean log, gboolean trace);

G_END_DECLS

#endif