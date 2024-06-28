#ifndef _CFGO_GST_GSTCFGOSRC_PUBLIC_API_H_
#define _CFGO_GST_GSTCFGOSRC_PUBLIC_API_H_

#include "cfgo/track.hpp"
#include "cfgo/gst/boxed.h"

typedef struct _GstCfgoSrc GstCfgoSrc;

CFGO_DECLARE_CPP_BOXED_PTR_LIKE(cfgo::Track, Track, track)


#endif