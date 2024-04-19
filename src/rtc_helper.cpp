#include "cfgo/rtc_helper.hpp"

namespace cfgo
{
    std::string signaling_state_to_str(rtc::PeerConnection::SignalingState state) {
        using ss = rtc::PeerConnection::SignalingState;
        switch (state)
        {
        case ss::HaveLocalOffer:
            return "HaveLocalOffer";
        case ss::HaveLocalPranswer:
            return "HaveLocalPranswer";
        case ss::HaveRemoteOffer:
            return "HaveRemoteOffer";
        case ss::HaveRemotePranswer:
            return "HaveRemotePranswer";
        case ss::Stable:
            return "Stable";
        default:
            return "unknown state " + std::to_string((int) state);
        }
    }

    std::string gathering_state_to_str(rtc::PeerConnection::GatheringState state) {
        using gs = rtc::PeerConnection::GatheringState;
        switch (state)
        {
        case gs::New:
            return "New";
        case gs::InProgress:
            return "InProgress";
        case gs::Complete:
            return "Complete";
        default:
            return "unknown state " + std::to_string((int) state);
        }
    }

    std::string ice_state_to_str(rtc::PeerConnection::IceState state) {
        using ice_state = rtc::PeerConnection::IceState;
        switch (state)
        {
        case ice_state::New:
            return "New";
        case ice_state::Checking:
            return "Checking";
        case ice_state::Closed:
            return "Closed";
        case ice_state::Completed:
            return "Completed";
        case ice_state::Connected:
            return "Connected";
        case ice_state::Disconnected:
            return "Disconnected";
        case ice_state::Failed:
            return "Failed";
        default:
            return "unknown state " + std::to_string((int) state);
        }
    }

    std::string peer_state_to_str(rtc::PeerConnection::State state) {
        using ps = rtc::PeerConnection::State;
        switch (state)
        {
        case ps::Failed:
            return "Failed";
        case ps::Closed:
            return "Closed";
        case ps::Connected:
            return "Connected";
        case ps::Connecting:
            return "Connecting";
        case ps::Disconnected:
            return "Disconnected";
        default:
            return "unknown state " + std::to_string((int) state);
        } 
    }
} // namespace cfgo