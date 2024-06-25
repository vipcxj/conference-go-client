#include "cfgo/token.hpp"
#include "cfgo/fmt.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/http.hpp"
#include "boost/beast/version.hpp"
#include "boost/url.hpp"
#include <chrono>
#include <random>

namespace cfgo
{
    namespace utils
    {
        auto get_token(std::string auth_host, short port, std::string_view key, std::string_view uid, std::string_view uname, std::string_view role, std::string_view room, bool auto_join) -> asio::awaitable<std::string> {
            namespace beast = boost::beast;
            namespace http = beast::http;
            namespace urls = boost::urls;
            using tcp = asio::ip::tcp;
            auto resolver = asio::use_awaitable.as_default_on(
                tcp::resolver(co_await asio::this_coro::executor)
            );
            auto stream = asio::use_awaitable.as_default_on(
                beast::tcp_stream(co_await asio::this_coro::executor)
            );
            auto const results = co_await resolver.async_resolve(auth_host, std::to_string(port));
            stream.expires_after(std::chrono::seconds(30));
            co_await stream.async_connect(results);

            thread_local std::mt19937 generator {};
            std::uniform_int_distribution<std::uint32_t> dist(0, 99999);
            auto nonce = dist(generator);

            urls::url url {"/token"};
            url.params().set("key", key);
            url.params().set("uid", uid);
            url.params().set("uname", uname);
            url.params().set("role", role);
            url.params().set("room", room);
            url.params().set("nonce", std::to_string(nonce));
            url.params().set("autojoin", std::to_string(auto_join));
            http::request<http::string_body> req {http::verb::get, url, 11};
            req.set(http::field::host, auth_host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            // Set the timeout.
            stream.expires_after(std::chrono::seconds(30));

            co_await http::async_write(stream, req);
            beast::flat_buffer b;
            http::response<http::string_body> res;
            co_await http::async_read(stream, b, res);

            auto token = res.body();

            // Gracefully close the socket
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            // not_connected happens sometimes
            // so don't bother reporting it.
            //
            if(ec && ec != beast::errc::not_connected)
                throw boost::system::system_error(ec, "shutdown");

            co_return token;
        }
    } // namespace utils
    
} // namespace cfgo
