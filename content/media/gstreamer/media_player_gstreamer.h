// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_MEDIAPLAYER_GSTREAMER_H_
#define CONTENT_MEDIA_GSTREAMER_MEDIAPLAYER_GSTREAMER_H_

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "content/common/media/media_player_channel_filter.h"
#include "content/media/gstreamer/gst_chromium_http_source.h"
#include "content/renderer/media/render_media_log.h"
#include "media/base/time_delta_interpolator.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/buffered_data_source_host_impl.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebFrameClient.h"
#include "ui/gfx/geometry/rect_f.h"

#include <gst/player/gstplayer.h>

struct _GstGLContext;
struct _GstGLDisplay;
typedef struct _GstGLContext GstGLContext;
typedef struct _GstGLDisplay GstGLDisplay;

namespace cc {
class ContextProvider;
}

namespace media {
class MediaLog;
}

namespace content {

struct RunHolder;
class MediaPlayerGStreamer;
class MediaPlayerChannel;
class ResourceDispatcher;
class GStreamerBufferedDataSourceFactory;

class MediaPlayerGStreamerFactory {
 public:
  MediaPlayerGStreamerFactory(
      media::MediaLog* media_log,
      content::ResourceDispatcher* resource_dispatcher,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner);

  ~MediaPlayerGStreamerFactory();

  MediaPlayerGStreamer* create(int player_id,
                               content::MediaPlayerChannel* media_channel);

 private:
  scoped_refptr<media::MediaLog> media_log_;
  ResourceDispatcher* resource_dispatcher_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner_;
};

// TODO: use ubercompositor and inherits from cc::VideoFrameProvider.
class MediaPlayerGStreamer {
 public:
  MediaPlayerGStreamer(
      int player_id,
      const GURL& url,
      content::MediaPlayerChannel* media_channel,
      media::MediaLog* media_log,
      content::ResourceDispatcher* resource_dispatcher,
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner);

  virtual ~MediaPlayerGStreamer();

  void Load(GURL url, unsigned position_update_interval_ms);
  void Play();
  void Pause();
  void Seek(const base::TimeDelta& duration);
  void ReleaseTexture(unsigned texture_id);
  void AddSourceId(const std::string& source_id,
                   const std::string& type,
                   const std::vector<std::string>& codecs);
  void RemoveSourceId(const std::string& source_id);
  void SetDuration(const base::TimeDelta& duration);
  void MarkEndOfStream();
  void UnmarkEndOfStream();
  void SetSequenceMode(const std::string& source_id, bool sequence_mode);
  void AppendData(const std::string& source_id,
                  const std::vector<unsigned char>& data,
                  const std::vector<base::TimeDelta>& times);
  void Abort(const std::string& source_id);
  void SetGroupStartTimestampIfInSequenceMode(
      const std::string& source_id,
      const base::TimeDelta& timestamp_offset);
  void RemoveSegment(const std::string& source_id,
                     const base::TimeDelta& start,
                     const base::TimeDelta& end);
  void AddKey(const std::string& session_id,
              const std::string& key_id,
              const std::string& key);

  void DidLoad();
  void OnDurationChanged(const base::TimeDelta& duration);
  void OnVideoSizeChanged(int width, int height);
  void OnMediaInfoUpdated(GstPlayerMediaInfo* info);
  void DidPlay();
  void DidPause();
  void DidSeek(const base::TimeDelta& delta);
  void DidEOS();
  void DidStop();
  void InitSegmentReceived(const std::string& source_id);
  void BufferedRangeUpdate(const std::string& source_id,
                           const std::vector<base::TimeDelta>& ranges);
  void OnPositionUpdated(base::TimeDelta position);
  void OnBufferingUpdated(int percent);
  void OnError(int error);
  void OnSeekDone(GstClockTime position);

  void DoReleaseTexture(unsigned texture_id);

  GstGLContext* GstgldisplayCreateContextCallback(GstGLDisplay* display,
                                                  GstGLContext* other_context);
  void ActivateGLContext();
  bool GlimagesinkDrawCallback(GstElement* sink,
                               GstGLContext* context,
                               GstSample* sample);
  void GstSourceSetup(GstElement* playbin, GstElement* src);
  void SyncMessage(GstBus* bus, GstMessage* msg);
  void AsyncMessage(GstBus* bus, GstMessage* msg);

 private:
  void SetupContextProvider();
  void SetupGLContext();
  void CleanupSamples();
  void CleanupGLContext();
  void ExecuteInGLThread(RunHolder* holder);

  void DataSourceInitialized(bool success);
  void NotifyDownloading(bool is_downloading);

  const int player_id_;

  GURL url_;

  scoped_refptr<cc::ContextProvider> provider_;

  content::MediaPlayerChannel* media_channel_;

  scoped_refptr<media::MediaLog> media_log_;
  media::BufferedDataSourceHostImpl buffered_data_source_host_;

  blink::WebLocalFrame* web_frame_;

  ResourceDispatcher* resource_dispatcher_;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner_;

  GstPlayer* player_;
  GstElement* media_source_;
  GstGLDisplay* gst_gl_display_;
  GstGLContext* gst_gl_context_;

  std::mutex gl_thread_mutex_;
  std::condition_variable gl_thread_condition_;

  GstClockTime seek_time_;

  typedef base::hash_map<unsigned, GstSample*> GstSampleMap;
  GstSampleMap samples_;
  bool was_preroll_;

  base::WeakPtrFactory<MediaPlayerGStreamer> weak_factory_;
};

}  // namespace content

#endif  // CONTENT_MEDIA_GSTREAMER_MEDIAPLAYER_GSTREAMER_H_
