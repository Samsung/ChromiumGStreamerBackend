// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/gstreamer/webmediaplayer_gstreamer.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/debug/alias.h"
#include "base/debug/crash_logging.h"
#include "base/metrics/histogram.h"
#include "base/process/process_handle.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "base/trace_event/trace_event.h"
#include "cc/blink/web_layer_impl.h"
#include "cc/layers/video_layer.h"
#include "content/renderer/render_thread_impl.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "content/common/gpu/client/gl_helper.h"
#include "content/common/media/media_messages.h"
#include "content/child/child_process.h"
#include "content/renderer/media/gstreamer/webmediasource_gstreamer.h"
#include "content/renderer/render_thread_impl.h"
#include "gpu/blink/webgraphicscontext3d_impl.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "ipc/ipc_message_macros.h"
#include "media/audio/null_audio_sink.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/cdm_context.h"
#include "media/base/limits.h"
#include "media/base/media_log.h"
#include "media/base/pipeline.h"
#include "media/base/text_renderer.h"
#include "media/base/video_frame.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/encrypted_media_player_support.h"
#include "media/blink/texttrack_impl.h"
#include "media/blink/webaudiosourceprovider_impl.h"
#include "media/blink/webcontentdecryptionmodule_impl.h"
#include "media/blink/webinbandtexttrack_impl.h"
#include "media/blink/webmediaplayer_delegate.h"
#include "media/blink/webmediaplayer_util.h"
#include "media/blink/webmediasource_impl.h"
#include "media/filters/chunk_demuxer.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "third_party/WebKit/public/platform/WebEncryptedMediaTypes.h"
#include "third_party/WebKit/public/platform/WebMediaPlayerEncryptedMediaClient.h"
#include "third_party/WebKit/public/platform/WebMediaSource.h"
#include "third_party/WebKit/public/platform/WebRect.h"
#include "third_party/WebKit/public/platform/WebSize.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebRuntimeFeatures.h"
#include "third_party/WebKit/public/web/WebSecurityOrigin.h"
#include "third_party/WebKit/public/web/WebView.h"

using blink::WebCanvas;
using blink::WebMediaPlayer;
using blink::WebRect;
using blink::WebSize;
using blink::WebString;

namespace {

// Limits the range of playback rate.
//
// TODO(kylep): Revisit these.
//
// Vista has substantially lower performance than XP or Windows7.  If you speed
// up a video too much, it can't keep up, and rendering stops updating except on
// the time bar. For really high speeds, audio becomes a bottleneck and we just
// use up the data we have, which may not achieve the speed requested, but will
// not crash the tab.
//
// A very slow speed, ie 0.00000001x, causes the machine to lock up. (It seems
// like a busy loop). It gets unresponsive, although its not completely dead.
//
// Also our timers are not very accurate (especially for ogg), which becomes
// evident at low speeds and on Vista. Since other speeds are risky and outside
// the norms, we think 1/16x to 16x is a safe and useful range for now.
const double kMinRate = 0.0625;
const double kMaxRate = 16.0;

}  // namespace

namespace media {

// GstPlayer has 100 ms tick interval.
static const int kTimeUpdateInterval = 100;

class BufferedDataSourceHostImpl;

#define STATIC_ASSERT_MATCHING_ENUM(name)                              \
  static_assert(static_cast<int>(WebMediaPlayer::CORSMode##name) ==    \
                    static_cast<int>(BufferedResourceLoader::k##name), \
                "mismatching enum values: " #name)
STATIC_ASSERT_MATCHING_ENUM(Unspecified);
STATIC_ASSERT_MATCHING_ENUM(Anonymous);
STATIC_ASSERT_MATCHING_ENUM(UseCredentials);
#undef STATIC_ASSERT_MATCHING_ENUM

WebMediaPlayerMessageDispatcher::WebMediaPlayerMessageDispatcher(
    int player_id,
    base::WeakPtr<WebMediaPlayerGStreamer> player)
    : player_id_(player_id), player_(player) {
  task_runner_ = base::ThreadTaskRunnerHandle::Get();
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  channel->RegisterDispatcher(player_id_, this);
}

WebMediaPlayerMessageDispatcher::~WebMediaPlayerMessageDispatcher() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  channel->RemoveDispatcher(player_id_);
}

void WebMediaPlayerMessageDispatcher::SendLoad(GURL url) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Load(player_id_, url));
}

void WebMediaPlayerMessageDispatcher::SendStart() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Start(player_id_));
}

void WebMediaPlayerMessageDispatcher::SendPause() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Pause(player_id_));
}

void WebMediaPlayerMessageDispatcher::SendSeek(base::TimeDelta delta) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Seek(player_id_, delta));
}

void WebMediaPlayerMessageDispatcher::SendRelease() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Release(player_id_));
}

void WebMediaPlayerMessageDispatcher::SendRealeaseTexture(unsigned texture_id) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_ReleaseTexture(player_id_, texture_id));
}

bool WebMediaPlayerMessageDispatcher::SendAddSourceId(
    const std::string& id,
    const std::string& type,
    const std::vector<std::string>& codecs) {
  bool ret = false;
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();

  if (channel)
    ret = channel->Send(
        new MediaPlayerMsg_AddSourceId(player_id_, id, type, codecs));

  return ret;
}

void WebMediaPlayerMessageDispatcher::SendRemoveSourceId(
    const std::string& id) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_RemoveSourceId(player_id_, id));
}

void WebMediaPlayerMessageDispatcher::SendSetDuration(
    const base::TimeDelta& duration) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_SetDuration(player_id_, duration));
}

void WebMediaPlayerMessageDispatcher::SendMarkEndOfStream() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_MarkEndOfStream(player_id_));
}

void WebMediaPlayerMessageDispatcher::SendUnmarkEndOfStream() {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_UnmarkEndOfStream(player_id_));
}

void WebMediaPlayerMessageDispatcher::SendSetSequenceMode(const std::string& id,
                                                          bool sequence_mode) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(
        new MediaPlayerMsg_SetSequenceMode(player_id_, id, sequence_mode));
}

void WebMediaPlayerMessageDispatcher::SendAppendData(
    const std::string& id,
    const unsigned char* data,
    unsigned length,
    const base::TimeDelta& append_window_start,
    const base::TimeDelta& append_window_end,
    const base::TimeDelta& timestamp_offset) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_AppendData(
        player_id_, id, std::vector<unsigned char>(data, data + length),
        std::vector<base::TimeDelta>{append_window_start, append_window_end,
                                     timestamp_offset}));
}

void WebMediaPlayerMessageDispatcher::SendAbort(const std::string& id) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_Abort(player_id_, id));
}

void WebMediaPlayerMessageDispatcher::
    SendSetGroupStartTimestampIfInSequenceMode(
        const std::string& id,
        const base::TimeDelta& timestamp_offset) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_SetGroupStartTimestampIfInSequenceMode(
        player_id_, id, timestamp_offset));
}

void WebMediaPlayerMessageDispatcher::SendRemoveSegment(
    const std::string& id,
    const base::TimeDelta& start,
    const base::TimeDelta& end) {
  content::MediaChannelHost* channel =
      content::RenderThreadImpl::current()->GetMediaChannel();
  if (channel)
    channel->Send(new MediaPlayerMsg_RemoveSegment(player_id_, id, start, end));
}

bool WebMediaPlayerMessageDispatcher::OnMessageReceived(
    const IPC::Message& message) {
  WebMediaPlayerGStreamer* player = player_.get();
  if (!player)
    return false;

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WebMediaPlayerMessageDispatcher, message)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_SetCurrentFrame, player,
                        WebMediaPlayerGStreamer::OnSetCurrentFrame)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaDurationChanged, player,
                        WebMediaPlayerGStreamer::OnMediaDurationChanged)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaPlaybackCompleted, player,
                        WebMediaPlayerGStreamer::OnMediaPlaybackCompleted)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaBufferingUpdate, player,
                        WebMediaPlayerGStreamer::OnMediaBufferingUpdate)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_SeekCompleted, player,
                        WebMediaPlayerGStreamer::OnSeekCompleted)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaError, player,
                        WebMediaPlayerGStreamer::OnMediaError)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaVideoSizeChanged, player,
                        WebMediaPlayerGStreamer::OnVideoSizeChanged)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaTimeUpdate, player,
                        WebMediaPlayerGStreamer::OnTimeUpdate)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_MediaPlayerReleased, player,
                        WebMediaPlayerGStreamer::OnMediaPlayerReleased)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_DidMediaPlayerPlay, player,
                        WebMediaPlayerGStreamer::OnPlayerPlay)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_DidMediaPlayerPause, player,
                        WebMediaPlayerGStreamer::OnPlayerPause)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_SourceSelected, player,
                        WebMediaPlayerGStreamer::OnSourceSelected)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_DidAddSourceId, player,
                        WebMediaPlayerGStreamer::OnAddSourceId)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_DidRemoveSourceId, player,
                        WebMediaPlayerGStreamer::OnRemoveSourceId)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_InitSegmentReceived, player,
                        WebMediaPlayerGStreamer::OnInitSegmentReceived)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_BufferedRangeUpdate, player,
                        WebMediaPlayerGStreamer::OnBufferedRangeUpdate)
    IPC_MESSAGE_FORWARD(MediaPlayerMsg_TimestampOffsetUpdate, player,
                        WebMediaPlayerGStreamer::OnTimestampOffsetUpdate)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

base::AtomicSequenceNumber WebMediaPlayerGStreamer::next_player_id_;

WebMediaPlayerGStreamer::WebMediaPlayerGStreamer(
    blink::WebLocalFrame* frame,
    blink::WebMediaPlayerClient* client,
    blink::WebMediaPlayerEncryptedMediaClient* encrypted_client,
    base::WeakPtr<WebMediaPlayerDelegate> delegate,
    CdmFactory* cdm_factory,
    media::MediaPermission* media_permission,
    blink::WebContentDecryptionModule* initial_cdm,
    MediaLog* media_log)
    : video_frame_provider_client_(nullptr),
      // Compositor thread does not exist in layout tests.
      compositor_loop_(
          content::RenderThreadImpl::current()->compositor_task_runner().get()
              ? content::RenderThreadImpl::current()->compositor_task_runner()
              : base::ThreadTaskRunnerHandle::Get()),
      current_frame_(nullptr),
      frame_(frame),
      network_state_(WebMediaPlayer::NetworkStateEmpty),
      ready_state_(WebMediaPlayer::ReadyStateHaveNothing),
      preload_(BufferedDataSource::AUTO),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      media_log_(media_log),
      load_type_(LoadTypeURL),
      paused_(true),
      seeking_(false),
      playback_rate_(0.0f),
      natural_size_(0, 0),
      ended_(false),
      pending_seek_(false),
      did_loading_progress_(false),
      buffered_(static_cast<size_t>(1)),
      client_(client),
      encrypted_client_(encrypted_client),
      interpolator_(&default_tick_clock_),
      delegate_(delegate),
      media_source_(nullptr),
      supports_save_(true),
      encrypted_media_support_(cdm_factory,
                               encrypted_client,
                               media_permission,
                               base::Bind(&WebMediaPlayerGStreamer::SetCdm,
                                          AsWeakPtr(),
                                          base::Bind(&IgnoreCdmAttached))),
      message_dispatcher_(next_player_id_.GetNext(), AsWeakPtr()) {
  media_log_->AddEvent(
      media_log_->CreateEvent(MediaLogEvent::WEBMEDIAPLAYER_CREATED));

  interpolator_.SetUpperBound(base::TimeDelta());

  if (initial_cdm) {
    SetCdm(base::Bind(&IgnoreCdmAttached),
           ToWebContentDecryptionModuleImpl(initial_cdm)->GetCdmContext());
  }

  // Ideally we could use the existing compositor thread
  // but it does not seem easy to get the context so let's
  // initialize one in the main thread for now.
  main_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&WebMediaPlayerGStreamer::SetupGLContext, AsWeakPtr()));
}

WebMediaPlayerGStreamer::~WebMediaPlayerGStreamer() {
  SetVideoFrameProviderClient(NULL);
  client_->setWebLayer(NULL);

  DCHECK(main_task_runner_->BelongsToCurrentThread());
  media_log_->AddEvent(
      media_log_->CreateEvent(MediaLogEvent::WEBMEDIAPLAYER_DESTROYED));

  if (delegate_)
    delegate_->PlayerGone(this);

  message_dispatcher_.SendRelease();
}

void WebMediaPlayerGStreamer::SetupGLContext() {
  gpu::gles2::GLES2Interface* gles2_ctx =
      content::RenderThreadImpl::current()
          ->SharedMainThreadContextProvider()
          ->ContextGL();
  ::gles2::Initialize();
  ::gles2::SetGLContext(gles2_ctx);
}

void WebMediaPlayerGStreamer::SetVideoFrameProviderClient(
    cc::VideoFrameProvider::Client* client) {
  // This is called from both the main renderer thread and the compositor
  // thread (when the main thread is blocked).
  if (video_frame_provider_client_ && video_frame_provider_client_ != client)
    video_frame_provider_client_->StopUsingProvider();
  video_frame_provider_client_ = client;
}

void WebMediaPlayerGStreamer::SetCurrentFrameInternal(
    scoped_refptr<media::VideoFrame>& video_frame) {
  base::AutoLock auto_lock(current_frame_lock_);
  current_frame_ = video_frame;
  if (video_frame_provider_client_)
    compositor_loop_->PostTask(
        FROM_HERE, base::Bind(&cc::VideoFrameProvider::Client::DidReceiveFrame,
                              base::Unretained(video_frame_provider_client_)));
}

bool WebMediaPlayerGStreamer::UpdateCurrentFrame(base::TimeTicks deadline_min,
                                                 base::TimeTicks deadline_max) {
  NOTIMPLEMENTED();
  return false;
}

bool WebMediaPlayerGStreamer::HasCurrentFrame() {
  base::AutoLock auto_lock(current_frame_lock_);
  return current_frame_;
}

scoped_refptr<media::VideoFrame> WebMediaPlayerGStreamer::GetCurrentFrame() {
  scoped_refptr<VideoFrame> video_frame;
  {
    base::AutoLock auto_lock(current_frame_lock_);
    video_frame = current_frame_;
  }

  return video_frame;
}

void WebMediaPlayerGStreamer::PutCurrentFrame() {}

void WebMediaPlayerGStreamer::OnReleaseTexture(unsigned texture_id,
                                               uint32 release_sync_point) {
  gpu::gles2::GLES2Interface* gl = ::gles2::GetGLContext();

  if (!gl) {
    message_dispatcher_.SendRealeaseTexture(texture_id);
    return;
  }

  gl->WaitSyncPointCHROMIUM(release_sync_point);

  message_dispatcher_.SendRealeaseTexture(texture_id);
}

void WebMediaPlayerGStreamer::OnSetCurrentFrame(
    int width,
    int height,
    unsigned texture_id,
    const std::vector<int32_t>& v_name) {
  gpu::gles2::GLES2Interface* gl = ::gles2::GetGLContext();

  if (!gl) {
    message_dispatcher_.SendRealeaseTexture(texture_id);
    return;
  }

  int8_t name[GL_MAILBOX_SIZE_CHROMIUM];
  for (int i = 0; i < GL_MAILBOX_SIZE_CHROMIUM; ++i)
    name[i] = v_name[i];

  gpu::Mailbox mailbox;
  mailbox.SetName(name);
  GLuint sync_point = gl->InsertSyncPointCHROMIUM();

  // TODO: use ubercompositor to avoid inheriting from cc::VideoFrameProvider
  // and to avoid creating media::VideoFrame::WrapNativeTexture here.
  scoped_refptr<media::VideoFrame> frame = media::VideoFrame::WrapNativeTexture(
      media::PIXEL_FORMAT_ARGB,
      gpu::MailboxHolder(mailbox, GL_TEXTURE_2D, sync_point),
      media::BindToCurrentLoop(base::Bind(
          &WebMediaPlayerGStreamer::OnReleaseTexture, AsWeakPtr(), texture_id)),
      gfx::Size(width, height), gfx::Rect(width, height),
      gfx::Size(width, height), base::TimeDelta());

  SetCurrentFrameInternal(frame);
}

void WebMediaPlayerGStreamer::OnMediaDurationChanged(
    const base::TimeDelta& duration) {
  DVLOG(1) << __FUNCTION__ << "(" << duration << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  SetNetworkState(WebMediaPlayer::NetworkStateLoaded);
  duration_ = duration;

  if (ready_state_ != WebMediaPlayer::ReadyStateHaveEnoughData) {
    SetReadyState(WebMediaPlayer::ReadyStateHaveMetadata);
    SetReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);
  }

  client_->durationChanged();
}

void WebMediaPlayerGStreamer::OnMediaPlaybackCompleted() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  // Ignore state changes until we've completed all outstanding seeks.
  if (seeking_ || pending_seek_)
    return;

  interpolator_.SetBounds(duration_, duration_);

  ended_ = true;
  client_->timeChanged();

  if (!paused_) {
    OnPlayerPause();
  }
}

void WebMediaPlayerGStreamer::OnMediaBufferingUpdate(int percent) {
  DVLOG(1) << __FUNCTION__ << "(" << percent << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  // Ignore buffering state changes until we've completed all outstanding seeks.
  if (seeking_ || pending_seek_)
    return;

  buffered_[0].end = duration() * percent / 100;
  did_loading_progress_ = true;
}

void WebMediaPlayerGStreamer::OnSeekCompleted(
    const base::TimeDelta& current_time) {
  DVLOG(1) << __FUNCTION__ << "(" << current_time << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  seeking_ = false;
  if (pending_seek_) {
    pending_seek_ = false;
    seek(pending_seek_time_.InSecondsF());
    return;
  }
  interpolator_.SetBounds(current_time, current_time);

  SetReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);

  client_->timeChanged();
}

void WebMediaPlayerGStreamer::OnMediaError(int error) {
  DVLOG(1) << __FUNCTION__ << "(" << error << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  if (ready_state_ == WebMediaPlayer::ReadyStateHaveNothing) {
    // Any error that occurs before reaching ReadyStateHaveMetadata should
    // be considered a format error.
    SetNetworkState(WebMediaPlayer::NetworkStateFormatError);
    return;
  }

  // TODO: Convert error SetNetworkState(error);
  // but for now at least notify client_ there is an error.
  SetNetworkState(WebMediaPlayer::NetworkStateFormatError);
}

void WebMediaPlayerGStreamer::OnVideoSizeChanged(int width, int height) {
  DVLOG(1) << __FUNCTION__ << "(" << width << "," << height << ")";
  if (natural_size_.width == width && natural_size_.height == height)
    return;

  natural_size_.width = width;
  natural_size_.height = height;

  if (!video_weblayer_) {
    video_weblayer_.reset(new cc_blink::WebLayerImpl(
        cc::VideoLayer::Create(cc_blink::WebLayerImpl::LayerSettings(), this,
                               media::VIDEO_ROTATION_0)));
    client_->setWebLayer(video_weblayer_.get());
  }

  client_->timeChanged();
}

void WebMediaPlayerGStreamer::OnTimeUpdate(base::TimeDelta current_timestamp,
                                           base::TimeTicks current_time_ticks) {
  DVLOG(1) << __FUNCTION__ << "(" << current_timestamp << ","
           << current_time_ticks << ")";

  // Compensate the current_timestamp with the IPC latency.
  base::TimeDelta lower_bound =
      base::TimeTicks::Now() - current_time_ticks + current_timestamp;
  base::TimeDelta upper_bound = lower_bound;
  // We should get another time update in about |kTimeUpdateInterval|
  // milliseconds.
  if (!paused_) {
    upper_bound +=
        base::TimeDelta::FromMilliseconds(media::kTimeUpdateInterval);
  }
  // if the lower_bound is smaller than the current time, just use the current
  // time so that the timer is always progressing.
  lower_bound =
      std::min(lower_bound, base::TimeDelta::FromSecondsD(currentTime()));
  interpolator_.SetBounds(lower_bound, upper_bound);
}

void WebMediaPlayerGStreamer::OnMediaPlayerReleased() {
  DVLOG(1) << __FUNCTION__;

  if (!paused_) {
    OnPlayerPause();
  }
}

void WebMediaPlayerGStreamer::OnPlayerPlay() {
  DVLOG(1) << __FUNCTION__;

  UpdatePlayingState(true);
  client_->playbackStateChanged();
}

void WebMediaPlayerGStreamer::OnPlayerPause() {
  DVLOG(1) << __FUNCTION__;

  UpdatePlayingState(false);
  client_->playbackStateChanged();
}

void WebMediaPlayerGStreamer::OnSourceSelected() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  DVLOG(1) << __FUNCTION__;

  media_source_ = new WebMediaSourceGStreamer(
      this, &message_dispatcher_,
      base::Bind(&WebMediaPlayerGStreamer::SetNetworkState, AsWeakPtr()));
  client_->mediaSourceOpened(media_source_);
}

void WebMediaPlayerGStreamer::OnAddSourceId(const std::string& id) {
  DVLOG(1) << __FUNCTION__;

  media_source_->OnAddSourceId(id);
}

void WebMediaPlayerGStreamer::OnRemoveSourceId(const std::string& id) {
  DVLOG(1) << __FUNCTION__;

  media_source_->OnRemoveSourceId(id);
}

void WebMediaPlayerGStreamer::OnInitSegmentReceived(const std::string& id) {
  DVLOG(1) << __FUNCTION__;

  media_source_->OnInitSegmentReceived(id);
}

void WebMediaPlayerGStreamer::OnBufferedRangeUpdate(
    const std::string& id,
    const std::vector<base::TimeDelta>& raw_ranges) {
  DVLOG(1) << __FUNCTION__;

  // TODO really receive ranges
  Ranges<base::TimeDelta> ranges;
  for (auto& iter : raw_ranges)
    ranges.Add(base::TimeDelta(), iter);

  media_source_->OnBufferedRangeUpdate(id, ranges);
}

void WebMediaPlayerGStreamer::OnTimestampOffsetUpdate(
    const std::string& id,
    const base::TimeDelta& timestamp_offset) {
  DVLOG(1) << __FUNCTION__;

  media_source_->OnTimestampOffsetUpdate(id, timestamp_offset);
}

void WebMediaPlayerGStreamer::load(LoadType load_type,
                                   const blink::WebURL& url,
                                   CORSMode cors_mode) {
  DVLOG(1) << __FUNCTION__ << "(" << load_type << ", " << url << ", "
           << cors_mode << ")";

  DoLoad(load_type, url, cors_mode);
}

void WebMediaPlayerGStreamer::DoLoad(LoadType load_type,
                                     const blink::WebURL& url,
                                     CORSMode cors_mode) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  GURL gurl(url);
  ReportMetrics(load_type, gurl,
                GURL(frame_->document().securityOrigin().toString()));

  // Set subresource URL for crash reporting.
  base::debug::SetCrashKeyValue("subresource_url", gurl.spec());

  load_type_ = load_type;

  if (load_type == LoadTypeMediaSource) {
    // TODO maybe just do blob://
    std::string media_url = gurl.spec();
    size_t pos = media_url.find("blob:");
    if (pos != std::string::npos) {
      media_url = media_url.substr(pos + 5);
      gurl = GURL("mediasourceblob://" + media_url);
    }
  }

  SetNetworkState(WebMediaPlayer::NetworkStateLoading);
  SetReadyState(WebMediaPlayer::ReadyStateHaveNothing);
  media_log_->AddEvent(media_log_->CreateLoadEvent(url.spec()));

  message_dispatcher_.SendLoad(gurl);
}

void WebMediaPlayerGStreamer::play() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  media_log_->AddEvent(media_log_->CreateEvent(MediaLogEvent::PLAY));

  if (playback_rate_ > 0) {
    message_dispatcher_.SendStart();
    UpdatePlayingState(true);
  }
}

void WebMediaPlayerGStreamer::pause() {
  DVLOG(1) << __FUNCTION__;
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  UpdatePlayingState(false);
  message_dispatcher_.SendPause();
}

bool WebMediaPlayerGStreamer::supportsSave() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return supports_save_;
}

void WebMediaPlayerGStreamer::seek(double seconds) {
  DVLOG(1) << __FUNCTION__ << "(" << seconds << "s)";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  if (seeking_) {
    pending_seek_ = true;
    pending_seek_time_ = media::ConvertSecondsToTimestamp(seconds);
    return;
  }

  seek_time_ = media::ConvertSecondsToTimestamp(seconds);
  seeking_ = true;
  media_log_->AddEvent(media_log_->CreateSeekEvent(seconds));
  message_dispatcher_.SendSeek(base::TimeDelta::FromSecondsD(seconds));
}

void WebMediaPlayerGStreamer::setRate(double rate) {
  DVLOG(1) << __FUNCTION__ << "(" << rate << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  // TODO(kylep): Remove when support for negatives is added. Also, modify the
  // following checks so rewind uses reasonable values also.
  if (rate < 0.0)
    return;

  // Limit rates to reasonable values by clamping.
  if (rate != 0.0) {
    if (rate < kMinRate)
      rate = kMinRate;
    else if (rate > kMaxRate)
      rate = kMaxRate;
    if (playback_rate_ == 0 && !paused_ && delegate_)
      delegate_->DidPlay(this);
  } else if (playback_rate_ != 0 && !paused_ && delegate_) {
    delegate_->DidPause(this);
  }

  playback_rate_ = rate;
  if (!paused_) {
    /* TODO:
     * SetPlaybackRate(rate);
     * if (data_source_)
     *   data_source_->MediaPlaybackRateChanged(rate);
     */
  }
}

void WebMediaPlayerGStreamer::setVolume(double volume) {
  DVLOG(1) << __FUNCTION__ << "(" << volume << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  NOTIMPLEMENTED();
}

void WebMediaPlayerGStreamer::setSinkId(const blink::WebString& device_id,
                                        WebSetSinkIdCB* web_callbacks) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NOTIMPLEMENTED();
}

#define STATIC_ASSERT_MATCHING_ENUM(webkit_name, chromium_name)          \
  static_assert(static_cast<int>(WebMediaPlayer::webkit_name) ==         \
                    static_cast<int>(BufferedDataSource::chromium_name), \
                "mismatching enum values: " #webkit_name)
STATIC_ASSERT_MATCHING_ENUM(PreloadNone, NONE);
STATIC_ASSERT_MATCHING_ENUM(PreloadMetaData, METADATA);
STATIC_ASSERT_MATCHING_ENUM(PreloadAuto, AUTO);
#undef STATIC_ASSERT_MATCHING_ENUM

void WebMediaPlayerGStreamer::setPreload(WebMediaPlayer::Preload preload) {
  DVLOG(1) << __FUNCTION__ << "(" << preload << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  // TODO:
}

bool WebMediaPlayerGStreamer::hasVideo() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

bool WebMediaPlayerGStreamer::hasAudio() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

blink::WebSize WebMediaPlayerGStreamer::naturalSize() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return natural_size_;
}

bool WebMediaPlayerGStreamer::paused() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return paused_;
}

bool WebMediaPlayerGStreamer::seeking() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  if (ready_state_ == WebMediaPlayer::ReadyStateHaveNothing)
    return false;

  return seeking_;
}

double WebMediaPlayerGStreamer::duration() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  // HTML5 spec requires duration to be NaN if readyState is HAVE_NOTHING

  if (ready_state_ == WebMediaPlayer::ReadyStateHaveNothing)
    return std::numeric_limits<double>::quiet_NaN();

  if (duration_ == media::kInfiniteDuration())
    return std::numeric_limits<double>::infinity();

  return duration_.InSecondsF();
}

double WebMediaPlayerGStreamer::timelineOffset() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  base::Time timeline_offset;
  // TODO:
  /*  if (media_source_delegate_)
    timeline_offset = media_source_delegate_->GetTimelineOffset();
   */

  if (timeline_offset.is_null())
    return std::numeric_limits<double>::quiet_NaN();

  return timeline_offset.ToJsTime();
}

double WebMediaPlayerGStreamer::currentTime() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  // If the player is processing a seek, return the seek time.
  // Blink may still query us if updatePlaybackState() occurs while seeking.

  if (seeking()) {
    return pending_seek_ ?
        pending_seek_time_.InSecondsF() : seek_time_.InSecondsF();
  }

  return std::min((const_cast<media::TimeDeltaInterpolator*>(&interpolator_))
                      ->GetInterpolatedTime(),
                  duration_)
      .InSecondsF();
}

WebMediaPlayer::NetworkState WebMediaPlayerGStreamer::networkState() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return network_state_;
}

WebMediaPlayer::ReadyState WebMediaPlayerGStreamer::readyState() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return ready_state_;
}

blink::WebTimeRanges WebMediaPlayerGStreamer::buffered() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return buffered_;
}

blink::WebTimeRanges WebMediaPlayerGStreamer::seekable() const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  if (ready_state_ < WebMediaPlayer::ReadyStateHaveMetadata)
    return blink::WebTimeRanges();

  const double seekable_end = duration();

  // Allow a special exception for seeks to zero for streaming sources with a
  // finite duration; this allows looping to work.
  // TODO: const bool allow_seek_to_zero = data_source_ &&
  // data_source_->IsStreaming() &&
  //                                base::IsFinite(seekable_end);

  // TODO(dalecurtis): Technically this allows seeking on media which return an
  // infinite duration so long as DataSource::IsStreaming() is false.  While not
  // expected, disabling this breaks semi-live players, http://crbug.com/427412.
  const blink::WebTimeRange seekable_range(0.0, seekable_end);
  return blink::WebTimeRanges(&seekable_range, 1);
}

bool WebMediaPlayerGStreamer::didLoadingProgress() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  bool ret = did_loading_progress_;
  did_loading_progress_ = false;
  return ret;
}

void WebMediaPlayerGStreamer::paint(blink::WebCanvas* canvas,
                                    const blink::WebRect& rect,
                                    unsigned char alpha,
                                    SkXfermode::Mode mode) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
}

bool WebMediaPlayerGStreamer::hasSingleSecurityOrigin() const {
  /* TODO
    if (data_source_)
      return data_source_->HasSingleOrigin();
   */

  return true;
}

bool WebMediaPlayerGStreamer::didPassCORSAccessCheck() const {
  /* TODO
    if (data_source_)
      return data_source_->DidPassCORSAccessCheck();
   */

  return false;
}

double WebMediaPlayerGStreamer::mediaTimeForTimeValue(double timeValue) const {
  return ConvertSecondsToTimestamp(timeValue).InSecondsF();
}

unsigned WebMediaPlayerGStreamer::decodedFrameCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerGStreamer::droppedFrameCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerGStreamer::audioDecodedByteCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerGStreamer::videoDecodedByteCount() const {
  NOTIMPLEMENTED();
  return 0;
}

bool WebMediaPlayerGStreamer::copyVideoTextureToPlatformTexture(
    blink::WebGraphicsContext3D* web_graphics_context,
    unsigned int texture,
    unsigned int level,
    unsigned int internal_format,
    unsigned int type,
    bool premultiply_alpha,
    bool flip_y) {
  return copyVideoTextureToPlatformTexture(web_graphics_context, texture,
                                           internal_format, type,
                                           premultiply_alpha, flip_y);
}

bool WebMediaPlayerGStreamer::copyVideoTextureToPlatformTexture(
    blink::WebGraphicsContext3D* web_graphics_context,
    unsigned int texture,
    unsigned int internal_format,
    unsigned int type,
    bool premultiply_alpha,
    bool flip_y) {
  NOTIMPLEMENTED();
  return false;
}

WebMediaPlayer::MediaKeyException WebMediaPlayerGStreamer::generateKeyRequest(
    const WebString& key_system,
    const unsigned char* init_data,
    unsigned init_data_length) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  return encrypted_media_support_.GenerateKeyRequest(
      frame_, key_system, init_data, init_data_length);
}

WebMediaPlayer::MediaKeyException WebMediaPlayerGStreamer::addKey(
    const WebString& key_system,
    const unsigned char* key,
    unsigned key_length,
    const unsigned char* init_data,
    unsigned init_data_length,
    const WebString& session_id) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  return encrypted_media_support_.AddKey(key_system, key, key_length, init_data,
                                         init_data_length, session_id);
}

WebMediaPlayer::MediaKeyException WebMediaPlayerGStreamer::cancelKeyRequest(
    const WebString& key_system,
    const WebString& session_id) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  return encrypted_media_support_.CancelKeyRequest(key_system, session_id);
}

void WebMediaPlayerGStreamer::setContentDecryptionModule(
    blink::WebContentDecryptionModule* cdm,
    blink::WebContentDecryptionModuleResult result) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  // TODO(xhwang): Support setMediaKeys(0) if necessary: http://crbug.com/330324
  if (!cdm) {
    result.completeWithError(
        blink::WebContentDecryptionModuleExceptionNotSupportedError, 0,
        "Null MediaKeys object is not supported.");
    return;
  }

  SetCdm(BindToCurrentLoop(base::Bind(&WebMediaPlayerGStreamer::OnCdmAttached,
                                      AsWeakPtr(), result)),
         ToWebContentDecryptionModuleImpl(cdm)->GetCdmContext());
}

void WebMediaPlayerGStreamer::OnEncryptedMediaInitData(
    EmeInitDataType init_data_type,
    const std::vector<uint8>& init_data) {
  DCHECK(init_data_type != EmeInitDataType::UNKNOWN);

  // Do not fire "encrypted" event if encrypted media is not enabled.
  // TODO(xhwang): Handle this in |client_|.
  if (!blink::WebRuntimeFeatures::isPrefixedEncryptedMediaEnabled() &&
      !blink::WebRuntimeFeatures::isEncryptedMediaEnabled()) {
    return;
  }

  // TODO(xhwang): Update this UMA name.
  UMA_HISTOGRAM_COUNTS("Media.EME.NeedKey", 1);

  encrypted_media_support_.SetInitDataType(init_data_type);

  encrypted_client_->encrypted(
      ConvertToWebInitDataType(init_data_type), vector_as_array(&init_data),
      base::saturated_cast<unsigned int>(init_data.size()));
}

void WebMediaPlayerGStreamer::OnWaitingForDecryptionKey() {
  encrypted_client_->didBlockPlaybackWaitingForKey();

  // TODO(jrummell): didResumePlaybackBlockedForKey() should only be called
  // when a key has been successfully added (e.g. OnSessionKeysChange() with
  // |has_additional_usable_key| = true). http://crbug.com/461903
  encrypted_client_->didResumePlaybackBlockedForKey();
}

void WebMediaPlayerGStreamer::SetCdm(const CdmAttachedCB& cdm_attached_cb,
                                     CdmContext* cdm_context) {
  NOTIMPLEMENTED();
}

void WebMediaPlayerGStreamer::OnCdmAttached(
    blink::WebContentDecryptionModuleResult result,
    bool success) {
  if (success) {
    result.complete();
    return;
  }

  result.completeWithError(
      blink::WebContentDecryptionModuleExceptionNotSupportedError, 0,
      "Unable to set MediaKeys object");
}

void WebMediaPlayerGStreamer::OnAddTextTrack(
    const TextTrackConfig& config,
    const AddTextTrackDoneCB& done_cb) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  const WebInbandTextTrackImpl::Kind web_kind =
      static_cast<WebInbandTextTrackImpl::Kind>(config.kind());
  const blink::WebString web_label = blink::WebString::fromUTF8(config.label());
  const blink::WebString web_language =
      blink::WebString::fromUTF8(config.language());
  const blink::WebString web_id = blink::WebString::fromUTF8(config.id());

  scoped_ptr<WebInbandTextTrackImpl> web_inband_text_track(
      new WebInbandTextTrackImpl(web_kind, web_label, web_language, web_id));

  scoped_ptr<TextTrack> text_track(new TextTrackImpl(
      main_task_runner_, client_, web_inband_text_track.Pass()));

  done_cb.Run(text_track.Pass());
}

void WebMediaPlayerGStreamer::SetNetworkState(
    WebMediaPlayer::NetworkState state) {
  DVLOG(1) << __FUNCTION__ << "(" << state << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  network_state_ = state;
  // Always notify to ensure client has the latest value.
  client_->networkStateChanged();
}

void WebMediaPlayerGStreamer::SetReadyState(WebMediaPlayer::ReadyState state) {
  DVLOG(1) << __FUNCTION__ << "(" << state << ")";
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  ready_state_ = state;
  // Always notify to ensure client has the latest value.
  client_->readyStateChanged();
}

void WebMediaPlayerGStreamer::UpdatePlayingState(bool is_playing) {
  DVLOG(1) << __FUNCTION__ << "(" << is_playing << ")";

  if (is_playing == !paused_)
    return;

  paused_ = !is_playing;

  if (is_playing)
    interpolator_.StartInterpolating();
  else
    interpolator_.StopInterpolating();

  if (delegate_) {
    if (is_playing)
      delegate_->DidPlay(this);
    else
      delegate_->DidPause(this);
  }
}

}  // namespace media
