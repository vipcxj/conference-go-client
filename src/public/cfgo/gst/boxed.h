#ifndef _CFGO_GST_BOXED_HPP_
#define _CFGO_GST_BOXED_HPP_
#include "cfgo/gst/utils.hpp"
#include "cfgo/track.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#define CFGO_TYPE_BOXED_TRACK (cfgo_boxed_track_get_type())
CFGO_DECLARE_BOXED_PTR_LIKE(cfgo::Track, Track, track)

#ifdef __cplusplus
}
#endif

#endif