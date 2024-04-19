#include "cfgo/sio_helper.hpp"
#include <sstream>
#include <algorithm>

namespace cfgo
{
    std::string sio_msg_to_str(const sio::message::ptr &msg, bool format, int ident) {
        using namespace sio;
        if (!msg)
        {
            return "<empty msg>";
        }
        switch (msg->get_flag())
        {
        case message::flag::flag_string: 
            /* code */
            return "\"" + msg->get_string() + "\"";
        case message::flag::flag_binary:
        {
            auto binary = msg->get_binary();
            if (binary)
            {
                return "b\"" + *binary + "\"";
            }
            else
            {
                return "b<empty>";
            }
        }
        case message::flag::flag_boolean:
            return msg->get_bool() ? "true" : "false";
        case message::flag::flag_double:
            return std::to_string(msg->get_double());
        case message::flag::flag_integer:
            return std::to_string(msg->get_int());
        case message::flag::flag_null:
            return "nullptr";
        case message::flag::flag_object:
        {
            const auto & map = msg->get_map();
            std::stringstream ss;
            ss << "{";
            if (format)
            {
                std::string s_ident(std::min(std::max(ident, 0), 300), ' ');
                int i = 0;
                for (auto &&[key, value] : map)
                {   
                    ss << std::endl << s_ident << "  " << key << ": " << sio_msg_to_str(value, format, ident + 2);
                    if (++i != map.size())
                    {
                        ss << ",";
                    }
                }
                if (!map.empty())
                {
                    ss << std::endl << s_ident << "}";
                }
                else
                {
                    ss << "}";
                }
            }
            else
            {
                int i = 0;
                for (auto &&[key, value] : map)
                {
                    if (i == 0)
                    {
                        ss << " ";
                    }
                    else
                    {
                        ss << ", ";
                    }
                    ss << key << ": " << sio_msg_to_str(value, format, ident + 2);
                    if (++i == map.size())
                    {
                        ss << " ";
                    }
                    
                }
                ss << "}";
            }
            return ss.str();
        }
        case message::flag::flag_array:
        {
            const auto & vec = msg->get_vector();
            std::stringstream ss;
            ss << "[";
            if (format)
            {
                std::string s_ident(std::min(std::max(ident, 0), 300), ' ');
                int i = 0;
                for (auto && value : vec)
                {
                    ss << std::endl << s_ident << "  " << sio_msg_to_str(value, format, ident + 2);
                    if (++i != vec.size())
                    {
                        ss << ",";
                    }
                }
                if (!vec.empty())
                {
                    ss << std::endl << s_ident << "]";
                }
                else
                {
                    ss << "]";
                }
            }
            else
            {
                int i = 0;
                for (auto &&value : vec)
                {
                    if (i == 0)
                    {
                        ss << " ";
                    }
                    else
                    {
                        ss << ", ";
                    }
                    ss << sio_msg_to_str(value, format, ident + 2);
                    if (++i == vec.size())
                    {
                        ss << " ";
                    }
                    
                }
                ss << "]";
            }
            return ss.str();
        }
        default:
            return "<unknown msg type " + std::to_string(msg->get_flag()) + ">";
        }
    }
} // namespace cfgo
