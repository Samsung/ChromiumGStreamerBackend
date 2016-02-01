// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_GSTREAMER_WEBMEDIASOURCE_GSTREAMER_H_
#define CONTENT_RENDERER_MEDIA_GSTREAMER_WEBMEDIASOURCE_GSTREAMER_H_

#include <string>
#include <vector>

#include "media/base/media_export.h"
#include "media/base/media_log.h"
#include "media/base/ranges.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebMediaSource.h"

namespace media {
class WebMediaPlayerGStreamer;
class WebMediaPlayerMessageDispatcher;
class WebSourceBufferGStreamer;

class WebMediaSourceGStreamer : public blink::WebMediaSource {
 public:
  typedef base::Callback<void(blink::WebMediaPlayer::NetworkState)>
      SetNetworkStateCB;

  WebMediaSourceGStreamer(WebMediaPlayerGStreamer* player,
                          WebMediaPlayerMessageDispatcher* message_dispatcher,
                          const SetNetworkStateCB& set_network_state_cb);
  virtual ~WebMediaSourceGStreamer();

  // blink::WebMediaSource implementation.
  virtual AddStatus addSourceBuffer(const blink::WebString& type,
                                    const blink::WebString& codecs,
                                    blink::WebSourceBuffer** source_buffer);
  virtual double duration();
  virtual void setDuration(double duration);
  virtual void markEndOfStream(EndOfStreamStatus status);
  virtual void unmarkEndOfStream();

  void OnAddSourceId(const std::string& id);
  void OnRemoveSourceId(const std::string& id);
  void OnInitSegmentReceived(const std::string& id);
  void OnBufferedRangeUpdate(const std::string& id,
                             const Ranges<base::TimeDelta>& ranges);
  void OnTimestampOffsetUpdate(const std::string& id,
                               const base::TimeDelta& timestamp_offset);

 private:
  // WebMediaPlayerGStreamer will not be released before
  // WebMediaSourceGStreamer.
  WebMediaPlayerGStreamer* player_;
  WebMediaPlayerMessageDispatcher* message_dispatcher_;
  SetNetworkStateCB set_network_state_cb_;

  typedef base::hash_map<std::string, media::WebSourceBufferGStreamer*>
      WebSourceBufferGStreamerMap;
  // Threading notes: |dispatchers_| is only accessed on the IO thread. Every
  // other field is protected by |lock_|.
  WebSourceBufferGStreamerMap source_buffers_;

  DISALLOW_COPY_AND_ASSIGN(WebMediaSourceGStreamer);
};

}  // namespace media

#endif  // CONTENT_RENDERER_MEDIA_GSTREAMER_WEBMEDIASOURCE_GSTREAMER_H_
