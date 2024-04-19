#ifndef _CFGO_GST_ERROR_HPP_
#define _CFGO_GST_ERROR_HPP_

#include "cfgo/gst/error.h"
#include <exception>
#include <string>

namespace cfgo
{
    namespace gst
    {
        GError * create_gerror_timeout(const std::string & message, bool trace = false);
        GError * create_gerror_general(const std::string & message, bool trace = false);
        GError * create_gerror_from_except(const std::exception_ptr & except, bool trace = false);
    } // namespace gst
    
} // namespace cfgo


#endif