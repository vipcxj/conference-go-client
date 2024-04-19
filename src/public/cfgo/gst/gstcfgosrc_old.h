/* GStreamer
 * Copyright (C) 2024 FIXME <fixme@example.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_CFGO_SRC_H_
#define _GST_CFGO_SRC_H_

#include "gst/base/gstbasesrc.h"
#include "cfgo/capi.h"

G_BEGIN_DECLS

#define GST_TYPE_CFGO_SRC   (gst_cfgo_src_get_type())
#define GST_CFGO_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CFGO_SRC,GstCfgoSrc))
#define GST_CFGO_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CFGO_SRC,GstCfgoSrcClass))
#define GST_IS_CFGO_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CFGO_SRC))
#define GST_IS_CFGO_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CFGO_SRC))

typedef struct _GstCfgoSrc GstCfgoSrc;
typedef struct _GstCfgoSrcClass GstCfgoSrcClass;
typedef struct _GstCfgoSrcPrivate GstCfgoSrcPrivate;

struct _GstCfgoSrc
{
  GstBaseSrc base_cfgosrc;
  int client_handle;
  const gchar * pattern;
  GstCfgoSrcPrivate * priv;
};

struct _GstCfgoSrcClass
{
  GstBaseSrcClass base_cfgosrc_class;
};

GType gst_cfgo_src_get_type (void);

GST_PLUGIN_STATIC_DECLARE(cfgosrc);

G_END_DECLS

#endif
