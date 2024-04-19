#include "cfgo/gst/utils.hpp"

namespace cfgo
{
    namespace gst
    {
        CFGO_DEFINE_MAKE_SHARED(gst_element, GstElement, gst_object_ref, gst_object_unref)
        CFGO_DEFINE_STEAL_SHARED(gst_element, GstElement, g_object_unref)
        CFGO_DEFINE_MAKE_SHARED(gst_pad, GstPad, gst_object_ref, gst_object_unref)
        CFGO_DEFINE_STEAL_SHARED(gst_pad, GstPad, g_object_unref)
        CFGO_DEFINE_MAKE_SHARED(gst_caps, GstCaps, gst_caps_ref, gst_caps_unref)
        CFGO_DEFINE_STEAL_SHARED(gst_caps, GstCaps, gst_caps_unref)
        CFGO_DEFINE_MAKE_SHARED(gst_sample, GstSample, gst_sample_ref, gst_sample_unref)
        CFGO_DEFINE_STEAL_SHARED(gst_sample, GstSample, gst_sample_unref)
        CFGO_DEFINE_MAKE_SHARED(gst_buffer, GstBuffer, gst_buffer_ref, gst_buffer_unref)
        CFGO_DEFINE_STEAL_SHARED(gst_buffer, GstBuffer, gst_buffer_unref)
        CFGO_DEFINE_STEAL_SHARED(g_error, GError, g_error_free)

        std::string get_pad_full_name(GstPad * pad)
        {
            std::string res = "pad ";
            auto element = gst_pad_get_parent_element(pad);
            if (element)
            {
                res = res + GST_PAD_NAME(pad) + " of " + GST_ELEMENT_NAME(element);
                gst_object_unref(element);
                return res;
            }
            else
            {
                return res + GST_PAD_NAME(pad);
            }
        }

        bool caps_check_any(GstCaps * caps, std::function<bool(const GstStructure *)> checker)
        {
            if (gst_caps_is_any(caps))
            {
                return true;
            }
            if (gst_caps_is_empty(caps))
            {
                return false;
            }
            for (guint i = 0; i < gst_caps_get_size(caps); ++i)
            {
                GstStructure *structure = gst_caps_get_structure(caps, i);
                if (checker(structure))
                {
                    return true;
                }                
            }
            return false;
        }

        bool caps_check_all(GstCaps * caps, std::function<bool(const GstStructure *)> checker)
        {
            if (gst_caps_is_any(caps))
            {
                return true;
            }
            if (gst_caps_is_empty(caps))
            {
                return false;
            }
            for (guint i = 0; i < gst_caps_get_size(caps); ++i)
            {
                GstStructure *structure = gst_caps_get_structure(caps, i);
                if (!checker(structure))
                {
                    return false;
                }                
            }
            return true;
        }
    } // namespace gst
    
} // namespace cfgo
