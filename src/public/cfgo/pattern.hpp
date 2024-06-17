#ifndef _CFGO_PATTERN_HPP_
#define _CFGO_PATTERN_HPP_

#include <string>
#include <vector>
#include "cfgo/json.hpp"
// #include "sio_message.h"

namespace cfgo {
    struct Pattern
    {
        enum Op {
            ALL = 0,
            SOME = 1,
            NONE = 2,
            PUBLISH_ID = 3,
            STREAM_ID = 4,
            TRACK_ID = 5,
            TRACK_RID = 6,
            TRACK_LABEL_ALL_MATCH = 7,
            TRACK_LABEL_SOME_MATCH = 8,
            TRACK_LABEL_NONE_MATCH = 9,
            TRACK_LABEL_ALL_HAS = 10,
            TRACK_LABEL_SOME_HAS = 11,
            TRACK_LABEL_NONE_HAS = 12,
            TRACK_TYPE = 13
        };
        Op op;
        std::vector<std::string> args;
        std::vector<Pattern> children;

        // sio::message::ptr create_message() const {
        //     auto msg = sio::object_message::create();
        //     msg->get_map()["op"] = sio::int_message::create(op);
        //     auto args_msg = sio::array_message::create();
        //     for (auto &&arg : args)
        //     {
        //         args_msg->get_vector().push_back(sio::string_message::create(arg));
        //     }
        //     msg->get_map()["args"] = args_msg;
        //     auto children_msg = sio::array_message::create();
        //     for (auto &&child : children)
        //     {
        //         children_msg->get_vector().push_back(child.create_message());
        //     }
        //     msg->get_map()["children"] = children_msg;
        //     return msg;
        // }
    };
    
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Pattern, op, args, children)
}

#endif