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
    std::shared_ptr<asio::io_context> get_io_context(int handle);
    int wrap_io_context(std::shared_ptr<asio::io_context> ptr);
    void ref_io_context(int handle);
    void unref_io_context(int handle);

    close_chan get_close_chan(int handle);
    int wrap_close_chan(close_chan ptr);
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