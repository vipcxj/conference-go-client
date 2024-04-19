#ifndef _CFGO_RTC_HELPER_H_
#define _CFGO_RTC_HELPER_H_

#include "rtc/peerconnection.hpp"

namespace cfgo
{
    std::string signaling_state_to_str(rtc::PeerConnection::SignalingState state);

    std::string gathering_state_to_str(rtc::PeerConnection::GatheringState state);

    std::string ice_state_to_str(rtc::PeerConnection::IceState state);

    std::string peer_state_to_str(rtc::PeerConnection::State state);
} // namespace cfgo


#endif