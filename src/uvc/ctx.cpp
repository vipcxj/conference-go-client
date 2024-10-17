#include "cfgo/uvc/ctx.hpp"
#include "cfgo/uvc/err.hpp"

namespace cfgo
{
    namespace uvc
    {
        Context::Context()
        {
            check_err(uvc_init(&m_ctx, NULL), "unable to init the uvc context, ");
        }
        Context::~Context()
        {
            if (m_ctx)
            {
                uvc_exit(m_ctx);
            }
        }
        Context::Context(Context && other): m_ctx(other.m_ctx)
        {
            other.m_ctx = nullptr;
        }
        Context & Context::operator= (Context && other)
        {
            m_ctx = other.m_ctx;
            other.m_ctx = nullptr;
            return *this;
        }
    } // namespace uvc
    
} // namespace cfgo
