#include "cfgo/str_helper.hpp"

namespace cfgo
{
    std::vector<std::string> str_split(std::string_view s, std::string_view delimiter)
    {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos)
        {
            token = s.substr(pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back(token);
        }

        res.emplace_back(s.substr(pos_start));
        return res;
    }

    std::vector<std::string> str_split(const std::string & s, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream ss(s);
        std::string item;

        while (std::getline(ss, item, delimiter))
        {
            result.push_back(item);
        }

        return result;
    }
} // namespace cfgo
