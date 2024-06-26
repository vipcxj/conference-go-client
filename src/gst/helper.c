#include "cfgo/gst/helper.h"

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field(GQuark field, const GValue *value, gpointer pfx)
{
    gchar *str = gst_value_serialize(value);

    g_print("%s  %15s: %s\n", (gchar *)pfx, g_quark_to_string(field), str);
    g_free(str);
    return TRUE;
}

CFGO_API void cfgo_gst_print_caps(const GstCaps *caps, const gchar *pfx)
{
    guint i;

    g_return_if_fail(caps != NULL);

    if (gst_caps_is_any(caps))
    {
        g_print("%sANY\n", pfx);
        return;
    }
    if (gst_caps_is_empty(caps))
    {
        g_print("%sEMPTY\n", pfx);
        return;
    }

    for (i = 0; i < gst_caps_get_size(caps); i++)
    {
        GstStructure *structure = gst_caps_get_structure(caps, i);

        g_print("%s%s\n", pfx, gst_structure_get_name(structure));
        gst_structure_foreach(structure, print_field, (gpointer)pfx);
    }
}

/* Prints information about a Pad Template, including its Capabilities */
CFGO_API void cfgo_gst_print_pad_templates_information(GstElementFactory *factory)
{
    const GList *pads;
    GstStaticPadTemplate *padtemplate;

    g_print("Pad Templates for %s:\n", gst_element_factory_get_longname(factory));
    if (!gst_element_factory_get_num_pad_templates(factory))
    {
        g_print("  none\n");
        return;
    }

    pads = gst_element_factory_get_static_pad_templates(factory);
    while (pads)
    {
        padtemplate = pads->data;
        pads = g_list_next(pads);

        if (padtemplate->direction == GST_PAD_SRC)
            g_print("  SRC template: '%s'\n", padtemplate->name_template);
        else if (padtemplate->direction == GST_PAD_SINK)
            g_print("  SINK template: '%s'\n", padtemplate->name_template);
        else
            g_print("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

        if (padtemplate->presence == GST_PAD_ALWAYS)
            g_print("    Availability: Always\n");
        else if (padtemplate->presence == GST_PAD_SOMETIMES)
            g_print("    Availability: Sometimes\n");
        else if (padtemplate->presence == GST_PAD_REQUEST)
            g_print("    Availability: On request\n");
        else
            g_print("    Availability: UNKNOWN!!!\n");

        if (padtemplate->static_caps.string)
        {
            GstCaps *caps;
            g_print("    Capabilities:\n");
            caps = gst_static_caps_get(&padtemplate->static_caps);
            cfgo_gst_print_caps(caps, "      ");
            gst_caps_unref(caps);
        }

        g_print("\n");
    }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
CFGO_API void cfgo_gst_print_pad_capabilities(GstElement *element, const gchar *pad_name)
{
    GstPad *pad = NULL;
    GstCaps *caps = NULL;

    /* Retrieve pad */
    pad = gst_element_get_static_pad(element, pad_name);
    if (!pad)
    {
        g_printerr("Could not retrieve pad '%s'\n", pad_name);
        return;
    }

    /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
    caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, NULL);

    /* Print and free */
    g_print("Caps for the %s pad:\n", pad_name);
    cfgo_gst_print_caps(caps, "      ");
    gst_caps_unref(caps);
    gst_object_unref(pad);
}

CFGO_API void cfgo_gst_release_pad(GstPad *pad, GstElement * owner)
{
    GstPadTemplate * templ = GST_PAD_PAD_TEMPLATE (pad);
    if (templ && GST_PAD_TEMPLATE_PRESENCE (templ) == GST_PAD_REQUEST)
    {
        if (owner)
        {
            gst_element_release_request_pad(owner, pad);
        }
        else
        {
            owner = gst_pad_get_parent_element(pad);
            if (owner)
            {
                gst_element_release_request_pad(owner, pad);
                gst_object_unref(owner);
            }
        }
    }
    gst_object_unref(pad);
}