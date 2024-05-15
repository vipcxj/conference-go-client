#include "cfgo/gst/buffer_pool.hpp"
#include "cfgo/fmt.hpp"
#include "cpptrace/cpptrace.hpp"

namespace cfgo
{
    namespace gst
    {
        namespace detail
        {
            class BufferPool
            {
            public:
                BufferPool(guint buf_size, guint min_buf, guint max_buf);
                ~BufferPool();

                GstBuffer * acquire_buffer();
            private:
                GstBufferPool * m_pool;
                guint m_buf_size;
            };
            
            BufferPool::BufferPool(guint buf_size, guint min_buf, guint max_buf): 
                m_pool(gst_buffer_pool_new()),
                m_buf_size(buf_size)
            {
                GstStructure *conf = gst_buffer_pool_get_config (m_pool);
                gst_buffer_pool_config_set_params (conf, nullptr, m_buf_size, min_buf, max_buf);
                if (!gst_buffer_pool_set_config (m_pool, conf))
                {
                    throw cpptrace::runtime_error("Unable to config the pool.");
                }
                if (!gst_buffer_pool_set_active (m_pool, TRUE))
                {
                    throw cpptrace::runtime_error("Unable to active the pool.");
                }
            }
            
            BufferPool::~BufferPool()
            {
                gst_buffer_pool_set_active (m_pool, FALSE);
                gst_object_unref (m_pool);
            }

            GstBuffer * BufferPool::acquire_buffer()
            {
                GstBuffer * buf = nullptr;
                GstBufferPoolAcquireParams params {
                    .format = GstFormat::GST_FORMAT_UNDEFINED, 
                    .start = 0, 
                    .stop = 0, 
                    .flags = GstBufferPoolAcquireFlags::GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT
                };
                auto gfr = gst_buffer_pool_acquire_buffer(m_pool, &buf, &params);
                if (gfr == GST_FLOW_EOS)
                {
                    return gst_buffer_new_and_alloc(m_buf_size);
                }
                else if (gfr == GST_FLOW_OK)
                {
                    return buf;
                }
                else
                {
                    throw cpptrace::runtime_error(fmt::format("Unable to acquire a buffer, the pool return {}.", (int) gfr));
                }
            }

        } // namespace detail

        BufferPool::BufferPool(std::nullptr_t): ImplBy(std::shared_ptr<detail::BufferPool>()) {}

        BufferPool::BufferPool(guint buf_size, guint min_buf, guint max_buf): ImplBy(buf_size, min_buf, max_buf) {}

        BufferPool::operator bool() const noexcept
        {
            return (bool) impl();
        }

        GstBuffer * BufferPool::acquire_buffer() const
        {
            auto & ptr = impl();
            if (ptr)
            {
                return ptr->acquire_buffer();
            }
            else
            {
                throw cpptrace::runtime_error("The pool is nullptr.");
            }
        }
        
    } // namespace gst
    
} // namespace cfgo
