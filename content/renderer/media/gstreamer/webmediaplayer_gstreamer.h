// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_
#define MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/threading/thread.h"
#include "cc/layers/video_frame_provider.h"
#include "content/common/media/media_channel_host.h"
#include "content/common/media/media_player_messages_gstreamer.h"
#include "content/common/media/media_process_launch_causes.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_message_macros.h"
#include "media/base/cdm_factory.h"
#include "media/base/media_export.h"
#include "media/base/pipeline.h"
#include "media/base/ranges.h"
#include "media/base/renderer_factory.h"
#include "media/base/text_track.h"
#include "media/base/time_delta_interpolator.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/buffered_data_source_host_impl.h"
#include "media/blink/webmediaplayer_delegate.h"
#include "media/blink/webmediaplayer_util.h"
#include "third_party/WebKit/public/platform/WebContentDecryptionModuleResult.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebMediaPlayerClient.h"
#include "third_party/WebKit/public/platform/WebSetSinkIdCallbacks.h"
#include "url/gurl.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace blink {
class WebGraphicsContext3D;
class WebLocalFrame;
class WebMediaPlayerEncryptedMediaClient;
}

namespace cc_blink {
class WebLayerImpl;
}

namespace media {
class MediaLog;
class WebMediaPlayerDelegate;
class WebMediaPlayerGStreamer;
class WebMediaSourceGStreamer;

class WebMediaPlayerMessageDispatcher
    : public IPC::Listener,
      public base::SupportsWeakPtr<WebMediaPlayerMessageDispatcher> {
 public:
  WebMediaPlayerMessageDispatcher(
      int player_id,
      base::WeakPtr<WebMediaPlayerGStreamer> player);
  ~WebMediaPlayerMessageDispatcher();

  void SendCreate();
  void SendLoad(GURL);
  void SendStart();
  void SendPause();
  void SendSeek(base::TimeDelta);
  void SendRelease();
  void SendRealeaseTexture(unsigned texture_id);

  // MSE
  bool SendAddSourceId(const std::string& id,
                       const std::string& type,
                       const std::vector<std::string>& codecs);
  void SendRemoveSourceId(const std::string& id);
  void SendSetDuration(const base::TimeDelta& duration);
  void SendMarkEndOfStream();
  void SendUnmarkEndOfStream();
  void SendSetSequenceMode(const std::string& id, bool sequence_mode);
  void SendAppendData(const std::string& id,
                      const unsigned char* data,
                      unsigned length,
                      const base::TimeDelta& append_window_start,
                      const base::TimeDelta& append_window_end,
                      const base::TimeDelta& timestamp_offset);
  void SendAbort(const std::string& id);
  void SendSetGroupStartTimestampIfInSequenceMode(
      const std::string& id,
      const base::TimeDelta& timestamp_offset);
  void SendRemoveSegment(const std::string& id,
                         const base::TimeDelta& start,
                         const base::TimeDelta& end);
  void SendAddKey(const std::string& session_id,
                  const std::string& key_id,
                  const std::string& key);

  bool OnMessageReceived(const IPC::Message& message) override;

  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() {
    return task_runner_.get();
  }

 private:
  int player_id_;
  base::WeakPtr<WebMediaPlayerGStreamer> player_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
};

// TODO: use ubercompositor to avoid inheriting from cc::VideoFrameProvider.
// Instead MediaPlayerGStreamer form Media Process will inherit this interface.
class MEDIA_EXPORT WebMediaPlayerGStreamer
    : public NON_EXPORTED_BASE(blink::WebMediaPlayer),
      public NON_EXPORTED_BASE(WebMediaPlayerDelegate::Observer),
      public cc::VideoFrameProvider,
      public base::SupportsWeakPtr<WebMediaPlayerGStreamer> {
 public:
  WebMediaPlayerGStreamer(
      blink::WebLocalFrame* frame,
      blink::WebMediaPlayerClient* client,
      blink::WebMediaPlayerEncryptedMediaClient* encrypted_client,
      base::WeakPtr<WebMediaPlayerDelegate> delegate,
      blink::WebContentDecryptionModule* initial_cdm,
      MediaLog* media_log);
  virtual ~WebMediaPlayerGStreamer();

  void SetVideoFrameProviderClient(
      cc::VideoFrameProvider::Client* client) override;
  bool UpdateCurrentFrame(base::TimeTicks deadline_min,
                          base::TimeTicks deadline_max) override;
  bool HasCurrentFrame() override;
  scoped_refptr<media::VideoFrame> GetCurrentFrame() override;
  void PutCurrentFrame() override;

  virtual void load(LoadType load_type,
                    const blink::WebURL& url,
                    CORSMode cors_mode) override;

  // Playback controls.
  virtual void play() override;
  virtual void pause() override;
  virtual bool supportsSave() const override;
  virtual void seek(double seconds) override;
  virtual void setRate(double rate) override;
  virtual void setVolume(double volume) override;
  virtual void setSinkId(const blink::WebString& sink_id,
                         const blink::WebSecurityOrigin& security_origin,
                         blink::WebSetSinkIdCallbacks* web_callback) override;
  virtual void setPreload(blink::WebMediaPlayer::Preload preload) override;
  virtual blink::WebTimeRanges buffered() const override;
  virtual blink::WebTimeRanges seekable() const override;

  // Methods for painting.
  virtual void paint(blink::WebCanvas* canvas,
                     const blink::WebRect& rect,
                     unsigned char alpha,
                     SkXfermode::Mode mode) override;

  // True if the loaded media has a playable video/audio track.
  virtual bool hasVideo() const override;
  virtual bool hasAudio() const override;

  // Dimensions of the video.
  virtual blink::WebSize naturalSize() const override;

  // Getters of playback state.
  virtual bool paused() const override;
  virtual bool seeking() const override;
  virtual double duration() const override;
  virtual double timelineOffset() const;
  virtual double currentTime() const override;

  // Internal states of loading and network.
  virtual blink::WebMediaPlayer::NetworkState networkState() const override;
  virtual blink::WebMediaPlayer::ReadyState readyState() const override;

  virtual bool didLoadingProgress() override;

  virtual bool hasSingleSecurityOrigin() const override;
  virtual bool didPassCORSAccessCheck() const override;

  virtual double mediaTimeForTimeValue(double timeValue) const override;

  virtual unsigned decodedFrameCount() const override;
  virtual unsigned droppedFrameCount() const override;
  virtual unsigned audioDecodedByteCount() const override;
  virtual unsigned videoDecodedByteCount() const override;

  virtual bool copyVideoTextureToPlatformTexture(blink::WebGraphicsContext3D*,
                                                 unsigned target,
                                                 unsigned texture,
                                                 unsigned internalFormat,
                                                 unsigned type,
                                                 int level,
                                                 bool premultiplyAlpha,
                                                 bool flipY) override;

  virtual bool copyVideoSubTextureToPlatformTexture(
      blink::WebGraphicsContext3D*,
      unsigned target,
      unsigned texture,
      int level,
      int xoffset,
      int yoffset,
      bool premultiplyAlpha,
      bool flipY) override;

  virtual void setContentDecryptionModule(
      blink::WebContentDecryptionModule* cdm,
      blink::WebContentDecryptionModuleResult result) override;

  void OnAddTextTrack(const TextTrackConfig& config,
                      const AddTextTrackDoneCB& done_cb);

  void OnReleaseTexture(uint32_t texture_id, const gpu::SyncToken& sync_token);

  void OnSetCurrentFrame(int width,
                         int height,
                         unsigned texture_id,
                         unsigned target,
                         const std::vector<int32_t>& name);
  void OnMediaDurationChanged(const base::TimeDelta& duration);
  void OnMediaPlaybackCompleted();
  void OnMediaBufferingUpdate(int percent);
  void OnSeekCompleted(const base::TimeDelta& current_time);
  void OnMediaError(int error);
  void OnVideoSizeChanged(int width, int height);
  void OnTimeUpdate(base::TimeDelta current_timestamp,
                    base::TimeTicks current_time_ticks);
  void OnMediaPlayerReleased();
  void OnPlayerPlay();
  void OnPlayerPause();

  void OnSourceSelected();
  void OnSourceDeleted(WebMediaSourceGStreamer*);
  void OnAddSourceId(const std::string& id);
  void OnRemoveSourceId(const std::string& id);
  void OnInitSegmentReceived(const std::string& id);
  void OnBufferedRangeUpdate(const std::string& id,
                             const std::vector<base::TimeDelta>& raw_ranges);
  void OnTimestampOffsetUpdate(const std::string& id,
                               const base::TimeDelta& timestamp_offset);
  void OnNeedKey(const std::string& system_id,
                 const std::vector<uint8_t>& init_data);

  // WebMediaPlayerDelegate::Observer implementation.
  void OnHidden(bool) override;
  void OnShown() override;
  void OnPlay() override;
  void OnPause() override;
  void OnVolumeMultiplierUpdate(double multiplier) override;

 private:
  void SetupGLContext();
  void SetCurrentFrameInternal(scoped_refptr<media::VideoFrame>& frame);

  // Called after |defer_load_cb_| has decided to allow the load. If
  // |defer_load_cb_| is null this is called immediately.
  void DoLoad(LoadType load_type, const blink::WebURL& url, CORSMode cors_mode);

  // Helpers that set the network/ready state and notifies the client if
  // they've changed.
  void SetNetworkState(blink::WebMediaPlayer::NetworkState state);
  void SetReadyState(blink::WebMediaPlayer::ReadyState state);

  // Called when the demuxer encounters encrypted streams.
  void OnEncryptedMediaInitData(EmeInitDataType init_data_type,
                                const std::vector<uint8_t>& init_data);

  // Called when a decoder detects that the key needed to decrypt the stream
  // is not available.
  void OnWaitingForDecryptionKey();

  void SetCdm(const CdmAttachedCB& cdm_attached_cb, CdmContext* cdm_context);

  // Called when a CDM has been attached to the |pipeline_|.
  void OnCdmAttached(blink::WebContentDecryptionModuleResult result,
                     bool success);

  void OnCdmKeysReady(const std::string& session_id,
                      bool has_additional_usable_key,
                      CdmKeysInfo keys_info);

  void UpdatePlayingState(bool is_playing);

  // A pointer back to the compositor to inform it about state changes. This is
  // not NULL while the compositor is actively using this webmediaplayer.
  // Accessed on main thread and on compositor thread when main thread is
  // blocked.
  cc::VideoFrameProvider::Client* video_frame_provider_client_;
  const scoped_refptr<base::SingleThreadTaskRunner> compositor_loop_;
  scoped_ptr<cc_blink::WebLayerImpl> video_weblayer_;
  scoped_refptr<media::VideoFrame> current_frame_;
  base::Lock current_frame_lock_;

  blink::WebLocalFrame* frame_;

  // TODO(hclam): get rid of these members and read from the pipeline directly.
  blink::WebMediaPlayer::NetworkState network_state_;
  blink::WebMediaPlayer::ReadyState ready_state_;

  // Preload state for when |data_source_| is created after setPreload().
  BufferedDataSource::Preload preload_;

  // Task runner for posting tasks on Chrome's main thread. Also used
  // for DCHECKs so methods calls won't execute in the wrong thread.
  const scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

  scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
  scoped_refptr<MediaLog> media_log_;

  // The LoadType passed in the |load_type| parameter of the load() call.
  LoadType load_type_;

  // Cache of metadata for answering hasAudio(), hasVideo(), and naturalSize().
  PipelineMetadata pipeline_metadata_;

  // Playback state.
  //
  // TODO(scherkus): we have these because Pipeline favours the simplicity of a
  // single "playback rate" over worrying about paused/stopped etc...  It forces
  // all clients to manage the pause+playback rate externally, but is that
  // really a bad thing?
  //
  // TODO(scherkus): since SetPlaybackRate(0) is asynchronous and we don't want
  // to hang the render thread during pause(), we record the time at the same
  // time we pause and then return that value in currentTime().  Otherwise our
  // clock can creep forward a little bit while the asynchronous
  // SetPlaybackRate(0) is being executed.
  bool paused_;
  bool seeking_;
  base::TimeDelta seek_time_;
  double playback_rate_;
  base::TimeDelta paused_time_;
  base::TimeDelta duration_;

  // Size of the video.
  blink::WebSize natural_size_;

  // TODO(scherkus): Replace with an explicit ended signal to HTMLMediaElement,
  // see http://crbug.com/409280
  bool ended_;

  // Seek gets pending if another seek is in progress. Only last pending seek
  // will have effect.
  bool pending_seek_;
  base::TimeDelta pending_seek_time_;

  bool did_loading_progress_;

  // Save the list of buffered time ranges.
  blink::WebTimeRanges buffered_;

  blink::WebMediaPlayerClient* client_;
  blink::WebMediaPlayerEncryptedMediaClient* encrypted_client_;

  // base::TickClock used by |interpolator_|.
  base::DefaultTickClock default_tick_clock_;

  // Tracks the most recent media time update and provides interpolated values
  // as playback progresses.
  media::TimeDeltaInterpolator interpolator_;

  base::WeakPtr<WebMediaPlayerDelegate> delegate_;
  int delegate_id_;

  WebMediaSourceGStreamer* media_source_;

  bool supports_save_;

  static base::AtomicSequenceNumber next_player_id_;
  WebMediaPlayerMessageDispatcher message_dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(WebMediaPlayerGStreamer);
};

}  // namespace media

#endif  // MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_
