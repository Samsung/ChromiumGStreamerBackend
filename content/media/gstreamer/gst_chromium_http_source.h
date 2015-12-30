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

G_END_DECLS

namespace content {

class GStreamerBufferedDataSource {
public:
    GStreamerBufferedDataSource(GURL url, media::BufferedResourceLoader::CORSMode cors_mode, ChromiumHttpSrc* src);
    media::BufferedDataSourceHostImpl* buffered_data_source_host() { return &buffered_data_source_host_; }
    media::BufferedDataSource* data_source() { return data_source_.get(); }

private:
    media::BufferedDataSourceHostImpl buffered_data_source_host_;
    scoped_ptr<media::BufferedDataSource> data_source_;
};

class GStreamerBufferedDataSourceFactory {
public:
    GStreamerBufferedDataSourceFactory();
    void create(gchar* uri, media::BufferedResourceLoader::CORSMode cors_mode, ChromiumHttpSrc* src);
    static GStreamerBufferedDataSourceFactory* Get();
    static void Init(scoped_refptr<media::MediaLog>, content::ResourceDispatcher*, scoped_refptr<base::SingleThreadTaskRunner>);
    void Set(scoped_refptr<media::MediaLog>, content::ResourceDispatcher*, scoped_refptr<base::SingleThreadTaskRunner>);
    scoped_refptr<media::MediaLog> media_log() { return media_log_; }
    content::ResourceDispatcher* resource_dispatcher() { return resource_dispatcher_; }
    scoped_refptr<base::SingleThreadTaskRunner> data_source_task_runner() { return data_source_task_runner_; }

private:
    scoped_refptr<media::MediaLog> media_log_;
    content::ResourceDispatcher* resource_dispatcher_;
    scoped_refptr<base::SingleThreadTaskRunner> data_source_task_runner_;
    DISALLOW_COPY_AND_ASSIGN(GStreamerBufferedDataSourceFactory);
};

}

#endif  // CONTENT_MEDIA_GSTREAMER_HTTP_SOURCE_H_
