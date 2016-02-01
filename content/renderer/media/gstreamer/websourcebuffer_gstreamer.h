// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_GSTREAMER_WEBSOURCEBUFFER_GSTREAMER_H_
#define CONTENT_RENDERER_MEDIA_GSTREAMER_WEBSOURCEBUFFER_GSTREAMER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/time/time.h"
#include "media/base/ranges.h"
#include "third_party/WebKit/public/platform/WebSourceBuffer.h"

namespace media {
class WebMediaPlayerMessageDispatcher;

class WebSourceBufferGStreamer : public blink::WebSourceBuffer {
 public:
  WebSourceBufferGStreamer(const std::string& id,
                           WebMediaPlayerMessageDispatcher* message_dispatcher);
  virtual ~WebSourceBufferGStreamer();

  // blink::WebSourceBuffer implementation.
  virtual void setClient(blink::WebSourceBufferClient* client);
  virtual bool setMode(AppendMode mode);
  virtual blink::WebTimeRanges buffered();
  virtual bool evictCodedFrames(double currentPlaybackTime, size_t newDataSize);
  virtual void append(const unsigned char* data,
                      unsigned length,
                      double* timestamp_offset);
  virtual void abort();
  virtual void resetParserState();
  virtual void remove(double start, double end);
  virtual bool setTimestampOffset(double offset);
  virtual void setAppendWindowStart(double start);
  virtual void setAppendWindowEnd(double end);
  virtual void removedFromMediaSource();

  void OnInitSegmentReceived();
  void OnBufferedRangeUpdate(const Ranges<base::TimeDelta>& ranges);
  void OnTimestampOffsetUpdate(const base::TimeDelta& timestamp_offset);

 private:
  std::string id_;
  WebMediaPlayerMessageDispatcher*
      message_dispatcher_;  // Owned by WebMediaPlayerGStreamer.

  blink::WebSourceBufferClient* client_;

  bool initialization_segment_received;

  // Controls the offset applied to timestamps when processing appended media
  // segments. It is initially 0, which indicates that no offset is being
  // applied. Both setTimestampOffset() and append() may update this value.
  base::TimeDelta timestamp_offset_;

  base::TimeDelta append_window_start_;
  base::TimeDelta append_window_end_;

  Ranges<base::TimeDelta> ranges_;

  DISALLOW_COPY_AND_ASSIGN(WebSourceBufferGStreamer);
};

}  // namespace media

#endif  // CONTENT_RENDERER_MEDIA_GSTREAMER_WEBSOURCEBUFFER_GSTREAMER_H_
