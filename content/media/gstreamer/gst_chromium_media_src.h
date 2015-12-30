// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_GST_CHROMIUM_MEDIA_SRC_H_
#define CONTENT_MEDIA_GSTREAMER_GST_CHROMIUM_MEDIA_SRC_H_

#include <gst/gst.h>

#include <string>
#include <vector>

#include "base/time/time.h"
#include "third_party/WebKit/public/platform/WebMediaSource.h"

G_BEGIN_DECLS

#define CHROMIUM_TYPE_MEDIA_SRC (chromium_media_src_get_type())
#define CHROMIUM_MEDIA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), CHROMIUM_TYPE_MEDIA_SRC, ChromiumMediaSrc))
#define CHROMIUM_MEDIA_SRC_CLASS(klass)                      \
  (G_TYPE_CHECK_CLASS_CAST((klass), CHROMIUM_TYPE_MEDIA_SRC, \
                           ChromiumMediaSrcClass))
#define CHROMIUM_IS_MEDIA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), CHROMIUM_TYPE_MEDIA_SRC))
#define CHROMIUM_IS_MEDIA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), CHROMIUM_TYPE_MEDIA_SRC))

typedef struct _ChromiumMediaSrc ChromiumMediaSrc;
typedef struct _ChromiumMediaSrcClass ChromiumMediaSrcClass;
typedef struct _ChromiumMediaSrcPrivate ChromiumMediaSrcPrivate;

struct _ChromiumMediaSrc {
  GstBin parent;

  ChromiumMediaSrcPrivate* priv;
};

struct _ChromiumMediaSrcClass {
  GstBinClass parentClass;
};

GType chromium_media_src_get_type(void);

blink::WebMediaSource::AddStatus chromiumMediaSrcAddSourceBufferId(
    ChromiumMediaSrc* src,
    const std::string& source_id,
    const std::string& type,
    const std::vector<std::string>& codecs);
void chromiumMediaSrcRemoveSourceBufferId(ChromiumMediaSrc* src,
                                          const std::string& source_id);
void chromiumMediaSrcSetDuration(ChromiumMediaSrc* src,
                                 const base::TimeDelta& duration);
void chromiumMediaSrcMarkEndOfStream(ChromiumMediaSrc* src);
bool chromiumMediaSrcAppendData(ChromiumMediaSrc* src,
                                const std::string& source_id,
                                const std::vector<unsigned char>& data,
                                const std::vector<base::TimeDelta>& times,
                                base::TimeDelta& timestamp_offset);
void chromiumMediaSrcAbort(ChromiumMediaSrc* src, const std::string& source_id);
void chromiumMediaSrcIsMatchingSourceId(ChromiumMediaSrc* src,
                                        const std::string& media_kind,
                                        const std::string& codec,
                                        std::string& source_id);

// Unimplemented.
void chromiumMediaSrcUnmarkEndOfStream(ChromiumMediaSrc* src);
void chromiumMediaSrcSetSequenceMode(ChromiumMediaSrc* src,
                                     const std::string& source_id,
                                     bool sequence_mod);
void chromiumMediaSrcSetGroupStartTimestampIfInSequenceMode(
    ChromiumMediaSrc* src,
    const std::string& source_id,
    const base::TimeDelta& timestamp_offset);
void chromiumMediaSrcRemoveSegment(ChromiumMediaSrc* src,
                                   const std::string& source_id,
                                   const base::TimeDelta& start,
                                   const base::TimeDelta& end);

G_END_DECLS

#endif  // CONTENT_MEDIA_GSTREAMER_GST_CHROMIUM_MEDIA_SRC_H_
