#ifndef _CFGO_UVC_CTX_HPP_
#define _CFGO_UVC_CTX_HPP_

#include "libuvc/libuvc.h"

namespace cfgo
{
    namespace uvc
    {
        class Context
        {
        private:
            uvc_context_t * m_ctx;
        public:
            Context();
            Context(const Context &) = delete;
            Context & operator= (const Context &) = delete;
            Context(Context &&);
            Context & operator= (Context &&);
            ~Context();
        };
        
        using context_t = Context;
        
    } // namespace uvc
    
} // namespace cfgo


#endif