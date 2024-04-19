#ifndef _CFGO_SIO_HELPER_H_
#define _CFGO_SIO_HELPER_H_

#include <string>
#include "sio_message.h"

namespace cfgo
{
    std::string sio_msg_to_str(const sio::message::ptr &msg, bool format = false, int ident = 0);
} // namespace cfgo

#endif