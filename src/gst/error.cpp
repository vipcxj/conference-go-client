#include "cfgo/gst/error.hpp"
#include "cfgo/gst/utils.hpp"
#include "cfgo/utils.hpp"
#include <exception>
#include <mutex>
#include <memory>
#include <sstream>
#include "cpptrace/cpptrace.hpp"
#include "spdlog/spdlog.h"

namespace cfgo
{
    namespace gst
    {
        struct ErrorPrivateState;
    } // namespace gst
} // namespace cfgo


typedef struct
{
    std::shared_ptr<cfgo::gst::ErrorPrivateState> m_state;
} CfgoErrorPrivate;

static void
cfgo_error_private_init (CfgoErrorPrivate *priv)
{
    priv->m_state = nullptr;
}

static void
cfgo_error_private_copy (const CfgoErrorPrivate * src_priv, CfgoErrorPrivate * dest_priv)
{
    dest_priv->m_state = src_priv->m_state;
}

static void
cfgo_error_private_clear (CfgoErrorPrivate * priv)
{}

// This defines the cfgo_error_get_private and cfgo_error_quark functions.
G_DEFINE_EXTENDED_ERROR (CfgoError, cfgo_error)

namespace cfgo
{
    namespace gst
    {
        struct ErrorPrivateState
        {
            std::exception_ptr m_except_ptr;
            bool m_cached;
            std::string m_trace_str;
            std::string m_trace_color_str;
            std::string m_message;
            mutex m_mutex;

            void _capture();
            void _cache_trace(const cpptrace::stacktrace & trace, bool overwrite = true);
            ErrorPrivateState(std::exception_ptr except);
            ErrorPrivateState(const cpptrace::stacktrace & trace);
            ErrorPrivateState(std::exception_ptr except, const cpptrace::stacktrace & trace);
            const char * get_trace_str();
            const char * get_message();
        };

        ErrorPrivateState::ErrorPrivateState(std::exception_ptr except): m_except_ptr(except), m_cached(false)
        {}

        ErrorPrivateState::ErrorPrivateState(const cpptrace::stacktrace & trace): m_cached(true)
        {
            _cache_trace(trace);
        }

        ErrorPrivateState::ErrorPrivateState(std::exception_ptr except, const cpptrace::stacktrace & trace): m_except_ptr(except), m_cached(false)
        {
            _cache_trace(trace);
        }

        void ErrorPrivateState::_cache_trace(const cpptrace::stacktrace & trace, bool overwrite)
        {
            std::stringstream ss;
            if (overwrite || m_trace_str.empty())
            {
                trace.print_with_snippets(ss, false);
                m_trace_str = ss.str();
                ss.clear();
                ss.str("");
            }
            if (overwrite || m_trace_color_str.empty())
            {
                trace.print_with_snippets(ss, true);
                m_trace_color_str = ss.str();
            }
        }

        void ErrorPrivateState::_capture()
        {
            m_cached = true;
            if (!m_except_ptr)
            {
                return;
            }
            try
            {
                std::rethrow_exception(m_except_ptr);
            }
            catch(const cpptrace::exception & e)
            {   
                _cache_trace(e.trace(), false);
                m_message = e.message();
                return;
            }
            m_message = cfgo::what(m_except_ptr);
        }

        const char * ErrorPrivateState::get_trace_str()
        {
            if (m_cached)
            {
                return m_trace_str.c_str();
            }
            std::lock_guard lock(m_mutex);
            _capture();
            return m_trace_str.c_str();
        }

        const char * ErrorPrivateState::get_message()
        {
            if (m_cached)
            {
                return m_message.c_str();
            }
            std::lock_guard lock(m_mutex);
            _capture();
            return m_message.c_str();
        }

        auto crete_gerror (CfgoError type, const gchar * message, bool gen_trace, const std::exception_ptr & except) -> GError *
        {
            auto error = g_error_new(CFGO_ERROR, type, "%s", message);
            if (gen_trace || except)
            {
                auto priv = cfgo_error_get_private(error);
                if (!priv)
                {
                    spdlog::error("Unable to get the private from GError object.");
                    return error;
                }
                if (except && gen_trace)
                {
                    auto trace = cpptrace::generate_trace(1);
                    priv->m_state = std::make_shared<ErrorPrivateState>(except, trace);
                }
                else if (gen_trace)
                {
                    auto trace = cpptrace::generate_trace(1);
                    priv->m_state = std::make_shared<ErrorPrivateState>(trace);
                }
                else
                {
                    priv->m_state = std::make_shared<ErrorPrivateState>(except);
                }
            }
            return error;
        }

        GError * create_gerror_timeout(const std::string & message, bool trace)
        {
            return crete_gerror(CFGO_ERROR_TIMEOUT, message.c_str(), trace, nullptr);
        }

        GError * create_gerror_general(const std::string & message, bool trace)
        {
            return crete_gerror(CFGO_ERROR_GENERAL, message.c_str(), trace, nullptr);
        }

        GError * create_gerror_from_except(const std::exception_ptr & except, bool trace)
        {
            return crete_gerror(CFGO_ERROR_GENERAL, nullptr, trace, except);
        }
    } // namespace gst
    
} // namespace cfgo


const gchar * cfgo_error_get_trace (GError *error)
{
    auto priv = cfgo_error_get_private(error);
    if (!priv || !priv->m_state)
    {
        return "";
    }
    return priv->m_state->get_trace_str();
}

const gchar * cfgo_error_get_message (GError *error)
{
    auto msg = error->message;
    if (msg && strlen(msg) > 0)
    {
        return msg;
    }
    auto priv = cfgo_error_get_private(error);
    if (!priv || !priv->m_state)
    {
        return "";
    }
    return priv->m_state->get_message();
}

void cfgo_error_set_timeout (GError ** error, const gchar * message, gboolean trace)
{
    *error = cfgo::gst::create_gerror_timeout(message, trace);
}

void cfgo_error_set_general (GError ** error, const gchar * message, gboolean trace)
{
    *error = cfgo::gst::create_gerror_general(message, trace);
}

void cfgo_error_submit(GstElement * src, GError * error)
{
    auto message = gst_message_new_error(GST_OBJECT(src), error, cfgo_error_get_trace(error));
    if (!message || !gst_element_post_message(GST_ELEMENT(src), message))
    {
        if (message)
        {
            gst_message_unref(message);
        }
        spdlog::warn("Failed to post the error message to gst element {}. The error is \"{}\".", 
            GST_ELEMENT_NAME(src), 
            cfgo_error_get_message(error)
        );
    }
}

void cfgo_error_submit_timeout (GstElement * src, const gchar * message, gboolean log, gboolean trace)
{
    if (log)
    {
        GST_ERROR_OBJECT(src, "%s", message);
    }
    using namespace cfgo::gst;
    auto error = steal_shared_g_error(create_gerror_timeout(message, trace));
    cfgo_error_submit(src, error.get());
}

void cfgo_error_submit_general (GstElement * src, const gchar * message, gboolean log, gboolean trace)
{
    if (log)
    {
        GST_ERROR_OBJECT(src, "%s", message);
    }
    using namespace cfgo::gst;
    auto error = steal_shared_g_error(create_gerror_general(message, trace));
    cfgo_error_submit(src, error.get());
}