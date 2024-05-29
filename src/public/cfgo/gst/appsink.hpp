#ifndef _CFGO_GST_APPSINK_HPP_
#define _CFGO_GST_APPSINK_HPP_

#include "gst/app/gstappsink.h"
#include "cfgo/async.hpp"
#include "cfgo/utils.hpp"
#include "cfgo/gst/utils.hpp"
#include "cfgo/move_only_function.hpp"

namespace cfgo
{
    namespace gst
    {
        namespace detail
        {
            class AppSink;
        } // namespace detail
        

        class AppSink : public ImplBy<detail::AppSink>
        {
        public:
            struct Statistics
            {
                std::uint64_t m_droped_bytes = 0;
                std::uint32_t m_droped_samples = 0;
                std::uint64_t m_received_bytes = 0;
                std::uint32_t m_received_samples = 0;

                inline float droped_bytes_rate() const noexcept
                {
                    if (m_received_bytes > 0)
                    {
                        return 1.0f * m_droped_bytes / m_received_bytes;
                    }
                    else
                    {
                        return 0.0f;
                    }
                }

                inline double droped_samples_rate() const noexcept
                {
                    if (m_received_samples > 0)
                    {
                        return 1.0f * m_droped_samples / m_received_samples;
                    }
                    else
                    {
                        return 0.0f;
                    }
                }

                inline std::uint32_t samples_mean_size() const noexcept
                {
                    if (m_received_samples > 0)
                    {
                        return static_cast<std::uint32_t>(m_received_bytes / m_received_samples);
                    }
                    else
                    {
                        return 0;
                    }
                }
            };

            using OnSampleCb = std::function<void(GstSample *)>;
            using OnSampleCbUnique = unique_function<void(GstSample *)>;
            using OnStatCb = std::function<void(const Statistics &)>;
            using OnStatCbUnique = unique_function<void(const Statistics &)>;
            AppSink(GstAppSink * sink, int cache_capicity);
            void init() const;
            /**
             * throw CancelError when closer is closed. return null shared_ptr when eos and no sample available.
            */
            auto pull_sample(close_chan closer = INVALID_CLOSE_CHAN) const -> asio::awaitable<GstSampleSPtr>;
            void set_on_sample(const OnSampleCb & cb) const;
            void set_on_sample(OnSampleCbUnique && cb) const;
            void unset_on_sample() const noexcept;
            void set_on_stat(const OnStatCb & cb) const;
            void set_on_stat(OnStatCbUnique && cb) const;
            void unset_on_stat() const noexcept;
        };
    } // namespace gst
    
} // namespace cfgo


#endif