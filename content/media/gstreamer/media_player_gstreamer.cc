// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/gstreamer/media_player_gstreamer.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/debug/alias.h"
#include "base/debug/crash_logging.h"
#include "base/process/process_handle.h"
#include "base/synchronization/waitable_event.h"
#include "base/trace_event/trace_event.h"
#include "content/child/child_process.h"
#include "content/child/child_thread_impl.h"
#include "content/child/resource_dispatcher.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/media/media_channel.h"
#include "content/media/gstreamer/gst_chromium_http_source.h"
#include "content/media/gstreamer/gst_chromium_media_src.h"
#include "content/media/gstreamer/gpuprocess/gstglcontext_gpu_process.h"
#include "content/media/gstreamer/gpuprocess/gstgldisplay_gpu_process.h"
#include "content/media/media_child_thread.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/limits.h"
#include "media/base/media_log.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "ui/gfx/x/x11_types.h"

namespace content {

static void end_of_stream_cb(GstPlayer* player,
                             MediaPlayerGStreamer* media_player) {
  DVLOG(1) << __FUNCTION__ << "(end of stream reached)";
  media_player->DidEOS();
}

static void error_cb(GstPlayer* player,
                     GError* err,
                     MediaPlayerGStreamer* media_player) {
  DVLOG(1) << __FUNCTION__ << "(GStreamer error: " << err->message << ")";
  media_player->OnError(err->code);
}

static void seek_done_cb(GstPlayer* player,
                         GstClockTime position,
                         MediaPlayerGStreamer* media_player) {
  media_player->OnSeekDone(position);
}

static void position_updated_cb(GstPlayer* player,
                                GstClockTime pos,
                                MediaPlayerGStreamer* media_player) {
  media_player->OnPositionUpdated(
      base::TimeDelta::FromMilliseconds(GST_TIME_AS_MSECONDS(pos)));
}

static void state_changed_cb(GstPlayer* player,
                             GstPlayerState state,
                             MediaPlayerGStreamer* media_player) {
  DVLOG(1) << __FUNCTION__
           << "(GstPlayer state: " << gst_player_state_get_name(state) << ")";

  switch (state) {
    case GST_PLAYER_STATE_PLAYING:
      media_player->DidPlay();
      break;
    case GST_PLAYER_STATE_PAUSED:
      media_player->DidPause();
      break;
    case GST_PLAYER_STATE_STOPPED:
      media_player->DidStop();
      break;
    case GST_PLAYER_STATE_BUFFERING:
      media_player->DidLoad();
      break;
  }
}

static void duration_changed_cb(GstPlayer* player,
                                GstClockTime duration,
                                MediaPlayerGStreamer* media_player) {
  DVLOG(1) << __FUNCTION__ << "(Duration changed: " << duration
           << ")";  // TODO: use GST_TIME_FORMAT / GST_TIME_ARGS
  media_player->OnDurationChanged(
      base::TimeDelta::FromMilliseconds(GST_TIME_AS_MSECONDS(duration)));
}

static void video_dimensions_changed_cb(GstPlayer* player,
                                        int width,
                                        int height,
                                        MediaPlayerGStreamer* media_player) {
  if (width > 0 && height > 0) {
    DVLOG(1) << __FUNCTION__ << "(Video dimension changed: " << width << "x"
             << height << ")";
    media_player->OnVideoSizeChanged(width, height);
  }
}

static void media_info_updated_cb(GstPlayer* player,
                                  GstPlayerMediaInfo* info,
                                  MediaPlayerGStreamer* media_player) {
  media_player->OnMediaInfoUpdated(info);
}

static void buffering_cb(GstPlayer* player,
                         gint percent,
                         MediaPlayerGStreamer* media_player) {
  media_player->OnBufferingUpdated(percent);
}

static void source_setup_cb(GstElement* playbin,
                            GstElement* src,
                            MediaPlayerGStreamer* player) {
  player->GstSourceSetup(playbin, src);
}

static void sync_bus_call(GstBus* bus,
                          GstMessage* msg,
                          MediaPlayerGStreamer* player) {
  player->SyncMessage(bus, msg);
}

static GstGLContext* gstgldisplay_create_context_cb(
    GstGLDisplay* display,
    GstGLContext* other_context,
    MediaPlayerGStreamer* player) {
  return player->GstgldisplayCreateContextCallback(display, other_context);
}

static gpointer gpu_process_proc_addr(GstGLAPI gl_api, const gchar* name) {
  return (gpointer)gles2::GetGLFunctionPointer(name);
}

static gboolean glimagesink_draw_cb(GstElement* gl_sink,
                                    GstGLContext* context,
                                    GstSample* sample,
                                    MediaPlayerGStreamer* player) {
  return player->GlimagesinkDrawCallback(gl_sink, context, sample);
}

MediaPlayerGStreamerFactory::MediaPlayerGStreamerFactory(
    media::MediaLog* media_log,
    content::ResourceDispatcher* resource_dispatcher,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner)
    : media_log_(media_log),
      resource_dispatcher_(resource_dispatcher),
      main_task_runner_(main_task_runner),
      gl_task_runner_(gl_task_runner) {}

MediaPlayerGStreamerFactory::~MediaPlayerGStreamerFactory() {}

MediaPlayerGStreamer* MediaPlayerGStreamerFactory::create(
    int player_id,
    content::MediaChannel* media_channel) {
  return new MediaPlayerGStreamer(player_id, GURL(), media_channel,
                                  media_log_.get(), resource_dispatcher_,
                                  main_task_runner_, gl_task_runner_);
}

MediaPlayerGStreamer::MediaPlayerGStreamer(
    int player_id,
    const GURL& url,
    content::MediaChannel* media_channel,
    media::MediaLog* media_log,
    content::ResourceDispatcher* resource_dispatcher,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner)
    : player_id_(player_id),
      url_(url),
      provider_(nullptr),
      media_channel_(media_channel),
      media_log_(media_log),
      resource_dispatcher_(resource_dispatcher),
      main_task_runner_(main_task_runner),
      gl_task_runner_(gl_task_runner),
      player_(gst_player_new()),  // It calls gst_init.
      media_source_(nullptr),
      gst_gl_display_(nullptr),
      gst_gl_context_(nullptr),
      seek_time_(GST_CLOCK_TIME_NONE),
      was_preroll_(false),
      weak_factory_(this) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  GstElementFactory* httpSrcFactory =
      gst_element_factory_find("chromiumhttpsrc");
  if (!httpSrcFactory) {
    gst_element_register(0, "chromiumhttpsrc", GST_RANK_PRIMARY + 100,
                         CHROMIUM_TYPE_HTTP_SRC);
  }

  GstElementFactory* mediaSrcFactory =
      gst_element_factory_find("chromiummediasrc");
  if (!mediaSrcFactory) {
    gst_element_register(0, "chromiummediasrc", GST_RANK_PRIMARY + 100,
                         CHROMIUM_TYPE_MEDIA_SRC);
  }

  g_signal_connect(player_, "duration-changed", G_CALLBACK(duration_changed_cb),
                   this);
  g_signal_connect(player_, "position-updated", G_CALLBACK(position_updated_cb),
                   this);
  g_signal_connect(player_, "state-changed", G_CALLBACK(state_changed_cb),
                   this);
  g_signal_connect(player_, "video-dimensions-changed",
                   G_CALLBACK(video_dimensions_changed_cb), this);
  g_signal_connect(player_, "media-info-updated",
                   G_CALLBACK(media_info_updated_cb), this);
  g_signal_connect(player_, "buffering", G_CALLBACK(buffering_cb), this);
  g_signal_connect(player_, "end-of-stream", G_CALLBACK(end_of_stream_cb),
                   this);
  g_signal_connect(player_, "error", G_CALLBACK(error_cb), this);
  g_signal_connect(player_, "seek-done", G_CALLBACK(seek_done_cb), this);

  GstElement* pipeline = gst_player_get_pipeline(player_);
  g_signal_connect(pipeline, "source-setup", G_CALLBACK(source_setup_cb), this);

  GstElement* glimagesink = gst_element_factory_make("glimagesink", NULL);

  if (!glimagesink) {
    DVLOG(1) << __FUNCTION__ << "(Failed: glimagesink is required)";
    OnError(0);
    return;
  }

  g_object_set(G_OBJECT(pipeline), "video-sink", glimagesink, NULL);
  g_signal_connect(G_OBJECT(glimagesink), "client-draw",
                   G_CALLBACK(glimagesink_draw_cb), this);

  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_enable_sync_message_emission(bus);
  g_signal_connect(bus, "sync-message", G_CALLBACK(sync_bus_call), this);

  gst_object_unref(bus);
  gst_object_unref(pipeline);

  provider_ = static_cast<MediaChildThread*>(MediaChildThread::current())
                  ->CreateSharedContextProvider();

  if (!provider_) {
    DVLOG(1) << __FUNCTION__ << "(Failed to create context provider)";
    OnError(0);
    return;
  }

  SetupContextProvider();

  if (!gst_gl_context_) {
    DVLOG(1) << __FUNCTION__ << "(Failed to create GstGL context)";
    OnError(0);
  }
}

MediaPlayerGStreamer::~MediaPlayerGStreamer() {
  // TODO: release provider and call ::gles2::Terminate(); from gl thread.
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  DVLOG(1) << __FUNCTION__ << "(Releasing GstPlayer)";

  // 1. Clean up samples first.
  {
    std::unique_lock<std::mutex> gl_thread_lock(gl_thread_mutex_);
    gl_task_runner_->PostTask(FROM_HERE,
                              base::Bind(&MediaPlayerGStreamer::CleanupSamples,
                                         weak_factory_.GetWeakPtr()));
    gl_thread_condition_.wait(gl_thread_lock);
  }

  // 2. Destroy the pipeline. Note that some cleanup callbacks will be called later in this task runner.
  gst_object_unref(player_);

  // 3. Clean up the context finally.
  {
    std::unique_lock<std::mutex> gl_thread_lock(gl_thread_mutex_);
    gl_task_runner_->PostTask(FROM_HERE,
                              base::Bind(&MediaPlayerGStreamer::CleanupGLContext,
                                         weak_factory_.GetWeakPtr()));
    gl_thread_condition_.wait(gl_thread_lock);
  }

  DVLOG(1) << __FUNCTION__ << "(GstPlayer release)";
}

void MediaPlayerGStreamer::SetupContextProvider() {
  std::unique_lock<std::mutex> lock(gl_thread_mutex_);

  gl_task_runner_->PostTask(FROM_HERE,
                            base::Bind(&MediaPlayerGStreamer::SetupGLContext,
                                       weak_factory_.GetWeakPtr()));

  gl_thread_condition_.wait(lock);
}

void MediaPlayerGStreamer::SetupGLContext() {
  DVLOG(1) << __FUNCTION__ << "(Setting up GstGL)";
  std::lock_guard<std::mutex> lock(gl_thread_mutex_);

  {
    if (!provider_->ContextSupport()) {
      bool ret = provider_->BindToCurrentThread();
      if (!ret) {
        DVLOG(1) << __FUNCTION__ << "(Failed to setup gl context)";
        OnError(0);
        return;
      }

      gpu::gles2::GLES2Interface* gles2_ctx = provider_->ContextGL();

      ::gles2::Initialize();
      ::gles2::SetGLContext(gles2_ctx);
    }

    gst_gl_display_ =
        reinterpret_cast<GstGLDisplay*>(gst_gl_display_gpu_process_new());

    g_signal_connect(G_OBJECT(gst_gl_display_), "create-context",
                     G_CALLBACK(gstgldisplay_create_context_cb), this);

    gst_gl_context_ = gst_gl_context_gpu_process_new(
        gst_gl_display_, GST_GL_API_GLES2,
        (GstGLProcAddrFunc)gpu_process_proc_addr);
  }

  gl_thread_condition_.notify_one();
}

void MediaPlayerGStreamer::CleanupSamples() {
  {
    std::lock_guard<std::mutex> lock(gl_thread_mutex_);

    DVLOG(1) << __FUNCTION__ << "(Cleaning samples)";

    for (GstSampleMap::iterator iter = samples_.begin(); iter != samples_.end();
         ++iter) {
      GstSample* sample = iter->second;
      if (sample) {
        iter->second = nullptr;
        gst_sample_unref(sample);
      }
    }
  }

  gl_thread_condition_.notify_one();
}

void MediaPlayerGStreamer::CleanupGLContext() {
  {
    std::lock_guard<std::mutex> lock(gl_thread_mutex_);

    DVLOG(1) << __FUNCTION__ << "(Cleaning GstGL)";

    if (gst_gl_context_) {
      gst_object_unref(gst_gl_context_);
      gst_gl_context_ = nullptr;
    }
  }

  gl_thread_condition_.notify_one();
}

void MediaPlayerGStreamer::GstSourceSetup(GstElement* playbin,
                                          GstElement* src) {
  if (url_.SchemeIs("mediasourceblob")) {
    DCHECK(!media_source_);
    media_source_ = GST_ELEMENT(gst_object_ref(src));
    media_channel_->SendSourceSelected(player_id_);
  } else if (url_.SchemeIs(url::kDataScheme)) {
    DVLOG(1) << __FUNCTION__ << "data url: " << url_.spec().c_str();
    // nothing todo
  } else if (url_.SchemeIsHTTPOrHTTPS() || url_.SchemeIsBlob() ||
             url_.SchemeIsFile()) {
    // nothing todo
  } else {
    DVLOG(1) << __FUNCTION__ << "(Restricted protocol: << " << url_.spec()
             << ")";
    OnError(0);
  }
}

void MediaPlayerGStreamer::SyncMessage(GstBus* bus, GstMessage* msg) {
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ASYNC_DONE: {
      was_preroll_ = true;
    } break;
    case GST_MESSAGE_NEED_CONTEXT: {
      const gchar* context_type = NULL;
      gst_message_parse_context_type(msg, &context_type);

      DVLOG(1) << __FUNCTION__ << "(Need context: " << context_type << ")";

      if (gst_gl_display_ &&
          g_strcmp0(context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {
        GstContext* display_context =
            gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
        gst_context_set_gl_display(display_context, gst_gl_display_);
        gst_element_set_context(GST_ELEMENT(msg->src), display_context);
        gst_object_unref(gst_gl_display_);
        gst_gl_display_ = nullptr;
      }
      break;
    }
    default:
      break;
  }
}

void MediaPlayerGStreamer::DoReleaseTexture(unsigned texture_id) {
  GstSampleMap::iterator iter = samples_.find(texture_id);
  if (iter != samples_.end()) {
    GstSample* sample = iter->second;
    if (sample) {
      DVLOG(1) << __FUNCTION__ << "(Releasing texture id: " << texture_id
               << ")";
      iter->second = nullptr;
      gst_sample_unref(sample);
    }
  }
}

GstGLContext* MediaPlayerGStreamer::GstgldisplayCreateContextCallback(
    GstGLDisplay* display,
    GstGLContext* other_context) {
  return gst_gl_context_;
}

bool MediaPlayerGStreamer::GlimagesinkDrawCallback(GstElement* sink,
                                                   GstGLContext* context,
                                                   GstSample* sample) {
  GstVideoFrame v_frame;
  GstVideoInfo v_info;
  guint texture_id = 0;
  GstBuffer* buf = gst_sample_get_buffer(sample);
  GstCaps* caps = gst_sample_get_caps(sample);

  gst_video_info_from_caps(&v_info, caps);

  if (!gst_video_frame_map(&v_frame, &v_info, buf,
                           (GstMapFlags)(GST_MAP_READ | GST_MAP_GL))) {
    DVLOG(1) << __FUNCTION__ << "(Failed to map GstGL buffer)";
    OnError(0);
    // Here the return value means that the callback has been processed.
    return true;
  }

  texture_id = *(guint*)v_frame.data[0];

  if (texture_id == 0) {
    DVLOG(1) << __FUNCTION__ << "(Wrong texture id: 0)";
    OnError(0);
    // Here the return value means that the callback has been processed.
    return true;
  }

  DVLOG(1) << __FUNCTION__ << "(Using texture id: " << texture_id << ")";

  if (was_preroll_ && samples_[texture_id]) {
    was_preroll_ = false;
    gst_video_frame_unmap(&v_frame);
    return true;
  }

  DCHECK(samples_[texture_id] == 0);

  samples_[texture_id] = gst_sample_ref(sample);

  gpu::gles2::GLES2Interface* gl = ::gles2::GetGLContext();

  gpu::Mailbox mailbox;
  gl->GenMailboxCHROMIUM(mailbox.name);
  gl->ProduceTextureDirectCHROMIUM(texture_id, GL_TEXTURE_2D, mailbox.name);
  gl->Flush();

  std::vector<int32_t> name(mailbox.name,
                            mailbox.name + GL_MAILBOX_SIZE_CHROMIUM);

  // TODO: use ubercompositor to avoid send the texture id to renderer process.
  // MediaPlayerGStreamer needs to inherit from cc::VideoFrameProvider and
  // register a cc::VideoLayer.
  // In other words, media::VideoFrame::WrapNativeTexture has to be created
  // here.
  media_channel_->SendSetCurrentFrame(player_id_, v_info.width, v_info.height,
                                      texture_id, name);

  gst_video_frame_unmap(&v_frame);

  return true;
}

void MediaPlayerGStreamer::Load(GURL url, unsigned position_update_interval_ms) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  url_ = url;
  gst_player_set_uri(player_, url_.spec().c_str());
  gst_player_set_position_update_interval (player_, position_update_interval_ms);
  gst_player_pause(player_);
}

void MediaPlayerGStreamer::Play() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  gst_player_play(player_);
}

void MediaPlayerGStreamer::Pause() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  gst_player_pause(player_);
}

void MediaPlayerGStreamer::Seek(const base::TimeDelta& delta) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  GstClockTime seek_time = delta.InMicroseconds() * 1000;
  if (GST_CLOCK_TIME_IS_VALID(seek_time)) {
    gst_player_seek(player_, seek_time);
  }
}

void MediaPlayerGStreamer::ReleaseTexture(unsigned texture_id) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  gl_task_runner_->PostTask(FROM_HERE,
                            base::Bind(&MediaPlayerGStreamer::DoReleaseTexture,
                                       weak_factory_.GetWeakPtr(), texture_id));
}

void MediaPlayerGStreamer::AddSourceId(const std::string& source_id,
                                       const std::string& type,
                                       const std::vector<std::string>& codecs) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  blink::WebMediaSource::AddStatus ret = chromiumMediaSrcAddSourceBufferId(
      CHROMIUM_MEDIA_SRC(media_source_), source_id, type, codecs);

  if (ret == blink::WebMediaSource::AddStatusOk)
    media_channel_->SendDidAddSourceId(player_id_, source_id);
  else
    media_channel_->SendDidAddSourceId(player_id_, "");
}

void MediaPlayerGStreamer::RemoveSourceId(const std::string& source_id) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  DCHECK(media_source_);

  chromiumMediaSrcRemoveSourceBufferId(CHROMIUM_MEDIA_SRC(media_source_),
                                       source_id);

  media_channel_->SendDidRemoveSourceId(player_id_, source_id);
}

void MediaPlayerGStreamer::SetDuration(const base::TimeDelta& duration) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcSetDuration(CHROMIUM_MEDIA_SRC(media_source_), duration);
}

void MediaPlayerGStreamer::MarkEndOfStream() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcMarkEndOfStream(CHROMIUM_MEDIA_SRC(media_source_));
}

void MediaPlayerGStreamer::UnmarkEndOfStream() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcUnmarkEndOfStream(CHROMIUM_MEDIA_SRC(media_source_));
}

void MediaPlayerGStreamer::SetSequenceMode(const std::string& source_id,
                                           bool sequence_mode) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcSetSequenceMode(CHROMIUM_MEDIA_SRC(media_source_), source_id,
                                  sequence_mode);
}

void MediaPlayerGStreamer::AppendData(
    const std::string& source_id,
    const std::vector<unsigned char>& data,
    const std::vector<base::TimeDelta>& times) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  base::TimeDelta timestamp_offset;
  chromiumMediaSrcAppendData(CHROMIUM_MEDIA_SRC(media_source_), source_id, data,
                             times, timestamp_offset);

  media_channel_->SendTimestampOffsetUpdate(player_id_, source_id,
                                            timestamp_offset);
}

void MediaPlayerGStreamer::Abort(const std::string& source_id) {
  chromiumMediaSrcAbort(CHROMIUM_MEDIA_SRC(media_source_), source_id);
}

void MediaPlayerGStreamer::SetGroupStartTimestampIfInSequenceMode(
    const std::string& source_id,
    const base::TimeDelta& timestamp_offset) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcSetGroupStartTimestampIfInSequenceMode(
      CHROMIUM_MEDIA_SRC(media_source_), source_id, timestamp_offset);
}

void MediaPlayerGStreamer::RemoveSegment(const std::string& source_id,
                                         const base::TimeDelta& start,
                                         const base::TimeDelta& end) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  chromiumMediaSrcRemoveSegment(CHROMIUM_MEDIA_SRC(media_source_), source_id,
                                start, end);
}

void MediaPlayerGStreamer::OnDurationChanged(const base::TimeDelta& duration) {
  media_channel_->SendMediaDurationChanged(player_id_, duration);
}

void MediaPlayerGStreamer::OnVideoSizeChanged(int width, int height) {
  media_channel_->SendMediaVideoSizeChanged(player_id_, width, height);
}

void MediaPlayerGStreamer::OnMediaInfoUpdated(GstPlayerMediaInfo* media_info) {
  for (GList* list = gst_player_media_info_get_stream_list(media_info);
       list != NULL; list = list->next) {
    GstPlayerStreamInfo* stream = (GstPlayerStreamInfo*)list->data;
    const gchar* codec = gst_player_stream_info_get_codec(stream);
    if (media_source_) {
      std::string source_id = chromiumMediaSrcIsMatchingSourceId(
          CHROMIUM_MEDIA_SRC(media_source_),
          gst_player_stream_info_get_stream_type(stream), codec);
      if (!source_id.empty())
        InitSegmentReceived(source_id);
    }
  }
}

void MediaPlayerGStreamer::DidLoad() {}

void MediaPlayerGStreamer::DidPlay() {
  VLOG(1) << __FUNCTION__ << "Media player GStreamer did play";
  media_channel_->SendDidMediaPlayerPlay(player_id_);
}

void MediaPlayerGStreamer::DidPause() {
  media_channel_->SendDidMediaPlayerPause(player_id_);
}

void MediaPlayerGStreamer::DidSeek(const base::TimeDelta& delta) {
  media_channel_->SendSeekCompleted(player_id_, delta);
}

void MediaPlayerGStreamer::DidEOS() {
  media_channel_->SendMediaPlaybackCompleted(player_id_);

  DVLOG(1) << "Media player GStreamer EOS";
}

void MediaPlayerGStreamer::DidStop() {
  DVLOG(1) << "Media player GStreamer stopped";
}

void MediaPlayerGStreamer::InitSegmentReceived(const std::string& source_id) {
  media_channel_->SendInitSegmentReceived(player_id_, source_id);
}

// TODO: ranges is the duration of the data that had been append.
// We should have a GstMessage each time a new chunck is parsed.
void MediaPlayerGStreamer::BufferedRangeUpdate(
    const std::string& source_id,
    const std::vector<base::TimeDelta>& ranges) {
  media_channel_->SendBufferedRangeUpdate(player_id_, source_id, ranges);
}

void MediaPlayerGStreamer::OnPositionUpdated(base::TimeDelta position) {
  media_channel_->SendMediaTimeUpdate(player_id_, position,
                                      base::TimeTicks::Now());
}

void MediaPlayerGStreamer::OnBufferingUpdated(int percent) {
  media_channel_->SendBufferingUpdate(player_id_, percent);
}

void MediaPlayerGStreamer::OnError(int error) {
  media_channel_->SendMediaError(player_id_, error);
}

void MediaPlayerGStreamer::OnSeekDone(GstClockTime position) {
  if (GST_CLOCK_TIME_IS_VALID(position)) {
    DidSeek(base::TimeDelta::FromMilliseconds(GST_TIME_AS_MSECONDS(position)));
  }
}

}  // namespace media
