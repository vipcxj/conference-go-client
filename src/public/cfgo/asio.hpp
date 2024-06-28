#ifndef _CFGO_ASIO_HPP_
#define _CFGO_ASIO_HPP_

#ifdef STANDALONE_ASIO
#include "asio.hpp"
#define ASIOCHAN_USE_STANDALONE_ASIO
#else
#include "boost/asio.hpp"
namespace asio = boost::asio;
#endif

#include "asiochan/asiochan.hpp"

namespace cfgo
{
    using standard_executor_type = asio::io_context::executor_type;
    using StandardStrand = asio::strand<standard_executor_type>;
} // namespace cfgo


#endif