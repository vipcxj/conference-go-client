#ifndef _CFGO_TOKEN_HPP_
#define _CFGO_TOKEN_HPP_

#include "cfgo/asio.hpp"
#include <string>

namespace cfgo
{
    namespace utils
    {
        auto get_token(std::string auth_host, short port, std::string_view key, std::string_view uid, std::string_view uname, std::string_view role, std::string_view room, bool auto_join) -> asio::awaitable<std::string>;
    } // namespace utils
    
} // namespace cfgo


#endif