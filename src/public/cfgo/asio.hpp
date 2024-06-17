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

#endif