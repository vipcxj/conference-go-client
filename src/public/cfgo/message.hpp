#ifndef _CFGO_IMPL_MESSAGE_HPP_
#define _CFGO_IMPL_MESSAGE_HPP_

#include "cfgo/pattern.hpp"
#include <memory>
#include <optional>

namespace cfgo
{
    namespace msg
    {
        struct Router {
            std::string room {};
            std::string socketFrom {};
            std::string socketTo {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Router, room, socketFrom, socketTo)
        };

        constexpr const char* SDP_TYPE_ANSWER = "answer";
        constexpr const char* SDP_TYPE_OFFER = "offer";
        constexpr const char* SDP_TYPE_PRANSWER = "pranswer";
        constexpr const char* SDP_TYPE_ROLLBACK = "rollback";

        struct SdpMessage {
            std::string type {};
            std::string sdp {};
            int mid {0};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SdpMessage, type, sdp, mid)
        };

        enum struct CandidateOp {
            ADD,
            END,
            UNKNOWN = -1,
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(CandidateOp, {
            {CandidateOp::UNKNOWN, nullptr},
            {CandidateOp::ADD, "add"},
            {CandidateOp::END, "end"},
        })

        struct RTCIceCandidateInit {
            std::string candidate {};
            std::optional<std::uint16_t> sdpMLineIndex {0};
            std::optional<std::string> sdpMid {};
            std::optional<std::string> usernameFragment {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RTCIceCandidateInit, candidate, sdpMLineIndex, sdpMid, usernameFragment)
        };

        struct CandidateMessage {
            CandidateOp op {CandidateOp::UNKNOWN};
            RTCIceCandidateInit candidate {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CandidateMessage, op, candidate)
        };

        struct PingMessage {
            Router router {};
            std::uint32_t msgId {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PingMessage, router, msgId)
        };

        struct PongMessage {
            Router router {};
            std::uint32_t msgId {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PongMessage, router, msgId)
        };

        struct ParticipantJoinMessage {
            Router router {};
            std::string userId {};
            std::string userName {};
            std::string socketId {};
            std::uint32_t joinId {0};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ParticipantJoinMessage, router, userId, userName, socketId, joinId)
        };

        struct ParticipantLeaveMessage {
            Router router {};
            std::string userId {};
            std::string socketId {};
            std::uint32_t joinId {0};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ParticipantLeaveMessage, router, userId, socketId, joinId)
        };

        struct JoinMessage {
            std::vector<std::string> rooms {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinMessage, rooms)
        };

        struct LeaveMessage {
            std::vector<std::string> rooms {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveMessage, rooms)
        };

        struct Track {
            std::string type {};
            std::string pubId {};
            std::string globalId {};
            std::string bindId {};
            std::string rid {};
            std::string streamId {};
            std::unordered_map<std::string, std::string> labels {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Track, type, pubId, globalId, bindId, rid, streamId, labels)
        };
        using TrackPtr = std::shared_ptr<Track>;

        enum struct SubscribeOp {
            ADD = 0,
            UPDATE = 1,
            REMOVE = 2,
        };
        
        struct SubscribeMessage {
            SubscribeOp op {SubscribeOp::ADD};
            std::string id {};
            std::vector<std::string> reqTypes {};
            Pattern pattern {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SubscribeMessage, op, id, reqTypes, pattern)
        };

        struct SubscribeResultMessage {
            std::string id {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SubscribeResultMessage, id)
        };

        struct SubscribedMessage {
            std::string subId {};
            std::string pubId {};
            int sdpId {};
            std::vector<Track> tracks {};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SubscribedMessage, subId, pubId, sdpId, tracks)
        };

        struct CustomMessage {
            Router router {};
            std::string content {};
            std::uint32_t msgId {0};
            bool ack {false};

            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomMessage, router, content, msgId, ack)
        };

        struct CustomAckMessage {
            Router router {};
            std::uint32_t msgId {0};
            std::string content {};
            bool err {false};
            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomAckMessage, router, msgId, content, err)
        };

        struct UserInfoMessage {
            std::string socketId {};
            std::string key {};
            std::string userId {};
            std::string userName {};
            std::string role {};
            std::vector<std::string> rooms {};
            NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(UserInfoMessage, socketId, key, userId, userName, role, rooms)
        };
        
    } // namespace msg
    
} // namespace cfgo


#endif