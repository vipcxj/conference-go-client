#ifndef _CFGO_GST_BUFFER_POOL_HPP_
#define _CFGO_GST_BUFFER_POOL_HPP_

#include "cfgo/utils.hpp"
#include "gst/gst.h"

namespace cfgo
{
    namespace gst
    {
        namespace detail
        {
            class BufferPool;
        } // namespace detail
        

        class BufferPool : public cfgo::ImplBy<detail::BufferPool>
        {
        public:
            BufferPool(std::nullptr_t);
            BufferPool(guint buf_size, guint min_buf, guint max_buf);
            operator bool() const noexcept;
            GstBuffer * acquire_buffer() const;
        };

    } // namespace gst
    
} // namespace cfgo


#endif