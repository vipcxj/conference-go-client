#ifndef _GST_CFGO_GST_GST_CFGO_SRC_PRIVATE_API_H_
#define _GST_CFGO_GST_GST_CFGO_SRC_PRIVATE_API_H_

#include "gst/gst.h"
#include "gst/app/gstappsrc.h"

typedef struct _GstCfgoSrc GstCfgoSrc;

namespace cfgo
{
    namespace gst
    {
        void rtp_src_set_callbacks(GstCfgoSrc * parent, GstAppSrcCallbacks & callbacks, gpointer user_data, GDestroyNotify notify);
        void rtp_src_remove_callbacks(GstCfgoSrc * parent);
        void rtcp_src_set_callbacks(GstCfgoSrc * parent, GstAppSrcCallbacks & callbacks, gpointer user_data, GDestroyNotify notify);
        void rtcp_src_remove_callbacks(GstCfgoSrc * parent);
        void link_rtp_src(GstCfgoSrc * parent, GstPad * pad);
        void link_rtcp_src(GstCfgoSrc * parent, GstPad * pad);
        GstFlowReturn push_rtp_buffer(GstCfgoSrc * parent, GstBuffer * buffer);
        GstFlowReturn push_rtcp_buffer(GstCfgoSrc * parent, GstBuffer * buffer);
    } // namespace gst
    
} // namespace cfgo


#endif