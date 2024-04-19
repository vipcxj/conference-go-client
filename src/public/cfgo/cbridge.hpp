#ifndef _CFGO_CBRIDGE_HPP_
#define _CFGO_CBRIDGE_HPP_

#include "cfgo/alias.hpp"
#include "cfgo/client.hpp"
#include "cfgo/pattern.hpp"
#include "cfgo/subscribation.hpp"
#include "cfgo/track.hpp"
#include "cfgo/capi.h"
#include "rtc/rtc.hpp"

namespace cfgo
{
    Client::CtxPtr get_execution_context(int handle);
    int wrap_execution_context(Client::CtxPtr ptr);
    void ref_execution_context(int handle);
    void unref_execution_context(int handle);

    close_chan_ptr get_close_chan(int handle);
    int wrap_close_chan(close_chan_ptr ptr);
    void ref_close_chan(int handle);
    void unref_close_chan(int handle);

    Client::Ptr get_client(int handle);
    int wrap_client(Client::Ptr ptr);
    void ref_client(int handle);
    void unref_client(int handle);

    Subscribation::Ptr get_subscribation(int handle);
    int wrap_subscribation(Subscribation::Ptr ptr);
    void ref_subscribation(int handle);
    void unref_subscribation(int handle);

    Track::Ptr get_track(int handle);
    int wrap_track(Track::Ptr ptr);
    void ref_track(int handle);
    void unref_track(int handle);

    rtc::Configuration rtc_config_to_cpp(const rtcConfiguration * conf);
    cfgo::Configuration cfgo_config_to_cpp(const cfgoConfiguration * conf);
    void cfgo_pattern_parse(const char * pattern_json, cfgo::Pattern & pattern);
    void cfgo_req_types_parse(const char * req_types_str, std::vector<std::string> & req_types);
} // namespace cfgo

#endif