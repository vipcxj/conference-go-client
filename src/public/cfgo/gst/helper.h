#ifndef _CFGO_GST_HELPER_H_
#define _CFGO_GST_HELPER_H_

#include "gst/gst.h"
#include "cfgo/exports.h"

#ifdef __cplusplus
extern "C" {
#endif

CFGO_API void cfgo_gst_print_caps (const GstCaps * caps, const gchar * pfx);

/* Prints information about a Pad Template, including its Capabilities */
CFGO_API void cfgo_gst_print_pad_templates_information (GstElementFactory * factory);

/* Shows the CURRENT capabilities of the requested pad in the given element */
CFGO_API void cfgo_gst_print_pad_capabilities (GstElement *element, const gchar *pad_name);

CFGO_API void cfgo_gst_release_pad(GstPad *pad, GstElement * owner);

#ifdef __cplusplus
}
#endif

#endif