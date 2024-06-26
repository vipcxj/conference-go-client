#include "cfgo/gst/appsink.hpp"
#include "boost/circular_buffer.hpp"

#include <cstdint>
#include <limits>

namespace cfgo
{
    namespace gst
    {
        namespace detail
        {
            class AppSink : public std::enable_shared_from_this<AppSink>
            {
            public:
                using SampleBuffer = boost::circular_buffer<std::pair<std::uint32_t, GstSampleSPtr>>;
                using Statistics = gst::AppSink::Statistics;
                using OnSampleCb = gst::AppSink::OnSampleCb;
                using OnStatCb = gst::AppSink::OnStatCb;
                AppSink(GstAppSink * sink, int cache_capicity);
                ~AppSink();

                void init();
                auto pull_sample(close_chan closer) -> asio::awaitable<GstSampleSPtr>;
                void set_on_sample(const OnSampleCb & cb);
                void set_on_sample(OnSampleCb && cb);
                void unset_on_sample() noexcept;
                void set_on_stat(const OnStatCb & cb);
                void set_on_stat(OnStatCb && cb);
                void unset_on_stat() noexcept;
            private:
                GstAppSink * m_sink;
                SampleBuffer m_cache;
                unique_void_chan m_sample_notify;
                unique_void_chan m_eos_notify;
                OnSampleCb m_on_sample;
                Statistics m_stat;
                OnStatCb m_on_stat;
                mutex m_mutex;
                std::uint32_t m_seq;
                bool m_eos;
                bool m_init;

                static void on_eos(GstAppSink *appsink, gpointer userdata);
                static GstFlowReturn on_new_preroll(GstAppSink *appsink, gpointer userdata);
                static GstFlowReturn on_new_sample(GstAppSink *appsink, gpointer userdata);
                static gboolean on_new_event(GstAppSink *appsink, gpointer userdata);
                static gboolean on_propose_allocation(GstAppSink *appsink, GstQuery *query, gpointer userdata);

                void _init();
                std::uint32_t _makesure_min_seq() const noexcept;
            };
            
            AppSink::AppSink(GstAppSink * sink, int cache_capicity): m_sink(sink), m_seq(0), m_eos(false), m_cache(cache_capicity), m_init(false)
            {
                gst_object_ref(m_sink);
            }

            void AppSink::_init()
            {
                if (!m_init)
                {
                    m_init = true;
                    GstAppSinkCallbacks callbacks{};
                    callbacks.eos = &AppSink::on_eos;
                    callbacks.new_preroll = &AppSink::on_new_preroll;
                    callbacks.new_sample = &AppSink::on_new_sample;
                    callbacks.new_event = &AppSink::on_new_event;
                    callbacks.propose_allocation = &AppSink::on_propose_allocation;
                    gst_app_sink_set_callbacks(m_sink, &callbacks, make_weak_holder(weak_from_this()), destroy_weak_holder<AppSink>);
                }
            }

            void AppSink::init()
            {
                std::lock_guard lk(m_mutex);
                _init();
            }
            
            AppSink::~AppSink()
            {
                GstAppSinkCallbacks callbacks {};
                gst_app_sink_set_callbacks(m_sink, &callbacks, NULL, NULL);
                gst_object_unref(m_sink);
            }

            void AppSink::on_eos(GstAppSink *appsink, gpointer userdata)
            {
                if (auto self = cast_weak_holder<AppSink>(userdata)->lock())
                {
                    std::lock_guard lk(self->m_mutex);
                    self->m_eos = true;
                    chan_maybe_write(self->m_eos_notify);
                }
            }
            GstFlowReturn AppSink::on_new_preroll(GstAppSink *appsink, gpointer userdata)
            {
                return GST_FLOW_OK;
            }
            GstFlowReturn AppSink::on_new_sample(GstAppSink *appsink, gpointer userdata)
            {
                if (auto self = cast_weak_holder<AppSink>(userdata)->lock())
                {
                    std::lock_guard lk(self->m_mutex);
                    if (self->m_seq == std::numeric_limits<std::uint32_t>::max())
                    {
                        auto offset = self->_makesure_min_seq();
                        for (auto && v : self->m_cache)
                        {
                            v.first -= offset;
                        }
                        self->m_seq -= offset;
                    }
                    auto sample = gst_app_sink_pull_sample(appsink);
                    if (sample)
                    {
                        auto sample_size = gst_buffer_get_size(gst_sample_get_buffer(sample));
                        self->m_stat.m_received_bytes += sample_size;
                        ++ self->m_stat.m_received_samples;
                        if (self->m_cache.full())
                        {
                            auto sample_size = gst_buffer_get_size(gst_sample_get_buffer(self->m_cache.front().second.get()));
                            self->m_stat.m_droped_bytes += sample_size;
                            ++ self->m_stat.m_droped_samples;
                        }
                        if (self->m_on_sample)
                        {
                            self->m_on_sample(sample);
                        }
                        if (self->m_on_stat)
                        {
                            self->m_on_stat(self->m_stat);
                        }
                        self->m_cache.push_back(std::make_pair(self->m_seq++, steal_shared_gst_sample(sample)));
                        chan_maybe_write(self->m_sample_notify);
                    }
                }
                return GST_FLOW_OK;
            }
            gboolean AppSink::on_new_event(GstAppSink *appsink, gpointer userdata)
            {
                return FALSE;
            }
            gboolean AppSink::on_propose_allocation(GstAppSink *appsink, GstQuery *query, gpointer userdata)
            {
                return FALSE;
            }

            std::uint32_t AppSink::_makesure_min_seq() const noexcept
            {
                if (m_cache.empty())
                {
                    return std::numeric_limits<std::uint32_t>::max();
                }
                else
                {
                    return m_cache.front().first;
                }
            }

            // only support one receiver at same time.
            auto AppSink::pull_sample(close_chan closer) -> asio::awaitable<GstSampleSPtr>
            {
                auto self = shared_from_this();
                init();
                GstSampleSPtr sample_ptr = nullptr;
                bool done = false;
                {
                    std::lock_guard lk(m_mutex);
                    if (!m_cache.empty())
                    {
                        sample_ptr = m_cache.front().second;
                        m_cache.pop_front();
                        done = true;
                    }
                    else if (m_eos)
                    {
                        done = true;
                    }
                }
                if (done)
                {
                    co_return sample_ptr;
                }
                do
                {
                    co_await select_or_throw(closer, asiochan::ops::read(m_sample_notify, m_eos_notify));
                    {
                        std::lock_guard lk(m_mutex);
                        if (!m_cache.empty() || m_eos)
                        {
                            if (!m_cache.empty())
                            {
                                sample_ptr = m_cache.front().second;
                                m_cache.pop_front();
                            }
                            break;
                        }
                    }
                } while (true);
                co_return sample_ptr;
            }

            void AppSink::set_on_sample(const OnSampleCb & cb)
            {
                std::lock_guard lk(m_mutex);
                m_on_sample = cb;
            }

            void AppSink::set_on_sample(OnSampleCb && cb)
            {
                std::lock_guard lk(m_mutex);
                m_on_sample = std::move(cb);
            }

            void AppSink::unset_on_sample() noexcept
            {
                std::lock_guard lk(m_mutex);
                m_on_sample = nullptr;
            }

            void AppSink::set_on_stat(const OnStatCb & cb)
            {
                std::lock_guard lk(m_mutex);
                m_on_stat = cb;
            }

            void AppSink::set_on_stat(OnStatCb && cb)
            {
                std::lock_guard lk(m_mutex);
                m_on_stat = std::move(cb);
            }

            void AppSink::unset_on_stat() noexcept
            {
                std::lock_guard lk(m_mutex);
                m_on_stat = nullptr;
            }

        } // namespace detail

        AppSink::AppSink(GstAppSink *sink, int cache_capicity) : ImplBy(sink, cache_capicity) {}

        void AppSink::init() const
        {
            impl()->init();
        }

        auto AppSink::pull_sample(close_chan closer) const -> asio::awaitable<GstSampleSPtr>
        {
            return impl()->pull_sample(std::move(closer));
        }

        void AppSink::set_on_sample(const OnSampleCb & cb) const
        {
            impl()->set_on_sample(cb);
        }

        void AppSink::set_on_sample(OnSampleCb && cb) const
        {
            impl()->set_on_sample(std::move(cb));
        }

        void AppSink::unset_on_sample() const noexcept
        {
            impl()->unset_on_sample();
        }

        void AppSink::set_on_stat(const OnStatCb & cb) const
        {
            impl()->set_on_stat(cb);
        }

        void AppSink::set_on_stat(OnStatCb && cb) const
        {
            impl()->set_on_stat(std::move(cb));
        }

        void AppSink::unset_on_stat() const noexcept
        {
            impl()->unset_on_stat();
        }

    } // namespace gst
    
} // namespace cfgo
