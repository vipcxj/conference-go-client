#ifndef _CFGO_CAPI_H_
#define _CFGO_CAPI_H_

#include "rtc/rtc.h"
#include "cfgo/exports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CFGO_MSG_TYPE_RTP,
    CFGO_MSG_TYPE_RTCP,
    CFGO_MSG_TYPE_ALL
} cfgoMsgType;

typedef enum
{
    CFGO_ERR_SUCCESS = 0,
    CFGO_ERR_FAILURE = -1,
    CFGO_ERR_TIMEOUT = -2
} cfgoErr;

typedef struct
{
    const char * signal_url;
    const char * token;
    const unsigned long ready_timeout;
    const rtcConfiguration * rtc_config;
    bool thread_safe;
    int execution_context_handle;
} cfgoConfiguration;

CFGO_API int cfgo_io_context_create();
CFGO_API int cfgo_io_context_ref(int handle);
CFGO_API int cfgo_io_context_unref(int handle);

CFGO_API int cfgo_close_chan_create();
CFGO_API int cfgo_close_chan_close(int handle);
CFGO_API int cfgo_close_chan_ref(int handle);
CFGO_API int cfgo_close_chan_unref(int handle);

CFGO_API int cfgo_client_create(const cfgoConfiguration * config, int io_context_handle, int closer_handle);
CFGO_API int cfgo_client_ref(int handle);
CFGO_API int cfgo_client_unref(int handle);
typedef void(*cfgoOnSubCallback)(int sub_handle, void * user_data);
CFGO_API int cfgo_client_subscribe(
    int client_handle, 
    const char * pattern, 
    const char * req_types, 
    int close_chan_handle,
    cfgoOnSubCallback on_sub_callback, void * user_data
);

CFGO_API const char * cfgo_subscribation_get_sub_id(int sub_handle);
CFGO_API const char * cfgo_subscribation_get_pub_id(int sub_handle);
CFGO_API int cfgo_subscribation_get_track_count(int sub_handle);
CFGO_API int cfgo_subscribation_get_track_at(int sub_handle, int index);
CFGO_API int cfgo_subscribation_ref(int sub_handle);
CFGO_API int cfgo_subscribation_unref(int sub_handle);

CFGO_API const char * cfgo_track_get_type(int track_handle);
CFGO_API const char * cfgo_track_get_pub_id(int track_handle);
CFGO_API const char * cfgo_track_get_global_id(int track_handle);
CFGO_API const char * cfgo_track_get_bind_id(int track_handle);
CFGO_API const char * cfgo_track_get_rid(int track_handle);
CFGO_API const char * cfgo_track_get_stream_id(int track_handle);
CFGO_API int cfgo_track_get_label_count(int track_handle);
CFGO_API const char * cfgo_track_get_label_at(int track_handle, const char * name);
CFGO_API void * cfgo_track_get_gst_caps(int track_handle, int payload_type);
CFGO_API const unsigned char * cfgo_track_receive_msg(int track_handle, cfgoMsgType msg_type);
CFGO_API int cfgo_track_ref(int track_handle);
CFGO_API int cfgo_track_unref(int track_handle);


#ifdef __cplusplus
}
#endif

#endif