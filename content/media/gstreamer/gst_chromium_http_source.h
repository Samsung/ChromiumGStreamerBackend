// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_HTTP_SOURCE_H_
#define CONTENT_MEDIA_GSTREAMER_HTTP_SOURCE_H_

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include "base/basictypes.h"
#include "content/renderer/media/render_media_log.h"
#include "media/base/data_source.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/buffered_data_source_host_impl.h"

namespace content {
class ResourceDispatcher;
}

G_BEGIN_DECLS

#define CHROMIUM_TYPE_HTTP_SRC (chromium_http_src_get_type())
#define CHROMIUM_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), CHROMIUM_TYPE_HTTP_SRC, ChromiumHttpSrc))
#define CHROMIUM_HTTP_SRC_CLASS(klass)                      \
  (G_TYPE_CHECK_CLASS_CAST((klass), CHROMIUM_TYPE_HTTP_SRC, \
                           ChromiumHttpSrcClass))
#define CHROMIUM_IS_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), CHROMIUM_TYPE_HTTP_SRC))
#define CHROMIUM_IS_HTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), CHROMIUM_TYPE_HTTP_SRC))

typedef struct _ChromiumHttpSrc ChromiumHttpSrc;
typedef struct _ChromiumHttpSrcClass ChromiumHttpSrcClass;
typedef struct _ChromiumHttpSrcPrivate ChromiumHttpSrcPrivate;

struct _ChromiumHttpSrc {
  GstBaseSrc parent;

  _ChromiumHttpSrcPrivate* priv;
};

struct _ChromiumHttpSrcClass {
  GstBaseSrcClass parentClass;
};

GType chromium_http_src_get_type(void);
void chromiumHttpSrcSetup(
    ChromiumHttpSrc* src,
    scoped_refptr<media::MediaLog> media_log,
    media::BufferedDataSourceHost* buffered_data_source_host,
    content::ResourceDispatcher* resource_dispatcher,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner);

G_END_DECLS

#endif  // CONTENT_MEDIA_GSTREAMER_HTTP_SOURCE_H_
