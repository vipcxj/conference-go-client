#ifndef _CFGO_GST_UTILS_HPP_
#define _CFGO_GST_UTILS_HPP_

#include "gst/gst.h"
#include <memory>
#include <string>
#include <functional>

namespace cfgo
{
    #define CFGO_DECLARE_SHARED_PTR(T) using T ## SPtr = std::shared_ptr<T>

    #define CFGO_DECLARE_MAKE_SHARED(N, T) \
        T ## SPtr make_shared_ ## N(T *t)

    #define CFGO_DECLARE_STEAL_SHARED(N, T) \
        T ## SPtr steal_shared_ ## N(T *t)

    #define CFGO_DEFINE_MAKE_SHARED(N, T, REF, UNREF) \
        T ## SPtr make_shared_ ## N(T *t) \
        { \
            if (t) REF(t); \
            return std::shared_ptr<T>(t, [](T * t) { \
                if (t) UNREF(t); \
            }); \
        }

    #define CFGO_DEFINE_STEAL_SHARED(N, T, FREE) \
        T ## SPtr steal_shared_ ## N(T *t) \
        { \
            return std::shared_ptr<T>(t, [](T * t) { \
                if (t) FREE(t); \
            }); \
        }   

    namespace gst
    {
        CFGO_DECLARE_SHARED_PTR(GstElement);
        CFGO_DECLARE_MAKE_SHARED(gst_element, GstElement);
        CFGO_DECLARE_STEAL_SHARED(gst_element, GstElement);
        CFGO_DECLARE_SHARED_PTR(GstPad);
        CFGO_DECLARE_MAKE_SHARED(gst_pad, GstPad);
        CFGO_DECLARE_STEAL_SHARED(gst_pad, GstPad);
        CFGO_DECLARE_SHARED_PTR(GstCaps);
        CFGO_DECLARE_MAKE_SHARED(gst_caps, GstCaps);
        CFGO_DECLARE_STEAL_SHARED(gst_caps, GstCaps);
        CFGO_DECLARE_SHARED_PTR(GstSample);
        CFGO_DECLARE_MAKE_SHARED(gst_sample, GstSample);
        CFGO_DECLARE_STEAL_SHARED(gst_sample, GstSample);
        CFGO_DECLARE_SHARED_PTR(GstBuffer);
        CFGO_DECLARE_MAKE_SHARED(gst_buffer, GstBuffer);
        CFGO_DECLARE_STEAL_SHARED(gst_buffer, GstBuffer);
        CFGO_DECLARE_SHARED_PTR(GError);
        CFGO_DECLARE_STEAL_SHARED(g_error, GError);

        std::string get_pad_full_name(GstPad * pad);

        bool caps_check_any(GstCaps * caps, std::function<bool(const GstStructure *)> checker);
        bool caps_check_all(GstCaps * caps, std::function<bool(const GstStructure *)> checker);
    } // namespace gst
} // namespace cfgo


#endif