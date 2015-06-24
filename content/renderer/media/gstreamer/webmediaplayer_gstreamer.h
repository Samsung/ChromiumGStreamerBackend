// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_
#define MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
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
#include "ipc/ipc_listener.h"
#include "ipc/ipc_message_macros.h"
#include "media/base/cdm_factory.h"
#include "media/base/media_export.h"
#include "media/base/pipeline.h"
#include "media/base/renderer_factory.h"
#include "media/base/text_track.h"
#include "media/base/time_delta_interpolator.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/buffered_data_source_host_impl.h"
#include "media/blink/encrypted_media_player_support.h"
#include "media/blink/encrypted_media_player_support.h"
#include "media/blink/webmediaplayer_util.h"
#include "third_party/WebKit/public/platform/WebContentDecryptionModuleResult.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebMediaPlayerClient.h"
#include "url/gurl.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace blink {
class WebGraphicsContext3D;
class WebLocalFrame;
}

namespace cc_blink {
class WebLayerImpl;
}

namespace media {
class MediaLog;
class WebMediaPlayerDelegate;
class WebMediaPlayerGStreamer;

class WebMediaPlayerMessageDispatcher
    : public IPC::Listener,
      public base::SupportsWeakPtr<WebMediaPlayerMessageDispatcher> {
 public:
  WebMediaPlayerMessageDispatcher(
      int player_id,
      base::WeakPtr<WebMediaPlayerGStreamer> player);
  ~WebMediaPlayerMessageDispatcher();

  void SendLoad(GURL);
  void SendStart();
  void SendPause();
  void SendSeek(base::TimeDelta);
  void SendStop();
  void SendRelease();
  void SendRealeaseTexture(unsigned texture_id);

  bool OnMessageReceived(const IPC::Message& message) override;

  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() {
    return task_runner_.get();
  }

 private:
  int player_id_;
  base::WeakPtr<WebMediaPlayerGStreamer> _player;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
};

// TODO: use ubercompositor to avoid inheriting from cc::VideoFrameProvider.
// Instead MediaPlayerGStreamer form Media Process will inherit this interface.
class MEDIA_EXPORT WebMediaPlayerGStreamer
    : public NON_EXPORTED_BASE(blink::WebMediaPlayer),
      public cc::VideoFrameProvider,
      public base::SupportsWeakPtr<WebMediaPlayerGStreamer> {
 public:
  WebMediaPlayerGStreamer(blink::WebLocalFrame* frame,
                          blink::WebMediaPlayerClient* client,
                          base::WeakPtr<WebMediaPlayerDelegate> delegate,
                          CdmFactory* cdm_factory,
                          media::MediaPermission* media_permission,
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
                    CORSMode cors_mode);

  // Playback controls.
  virtual void play();
  virtual void pause();
  virtual bool supportsSave() const;
  virtual void seek(double seconds);
  virtual void setRate(double rate);
  virtual void setVolume(double volume);
  virtual void setSinkId(const blink::WebString& device_id,
                         WebSetSinkIdCB* web_callbacks);
  virtual void setPreload(blink::WebMediaPlayer::Preload preload);
  virtual blink::WebTimeRanges buffered() const;
  virtual blink::WebTimeRanges seekable() const;

  // Methods for painting.
  virtual void paint(blink::WebCanvas* canvas,
                     const blink::WebRect& rect,
                     unsigned char alpha,
                     SkXfermode::Mode mode);

  // True if the loaded media has a playable video/audio track.
  virtual bool hasVideo() const;
  virtual bool hasAudio() const;

  // Dimensions of the video.
  virtual blink::WebSize naturalSize() const;

  // Getters of playback state.
  virtual bool paused() const;
  virtual bool seeking() const;
  virtual double duration() const;
  virtual double timelineOffset() const;
  virtual double currentTime() const;

  // Internal states of loading and network.
  virtual blink::WebMediaPlayer::NetworkState networkState() const;
  virtual blink::WebMediaPlayer::ReadyState readyState() const;

  virtual bool didLoadingProgress();

  virtual bool hasSingleSecurityOrigin() const;
  virtual bool didPassCORSAccessCheck() const;

  virtual double mediaTimeForTimeValue(double timeValue) const;

  virtual unsigned decodedFrameCount() const;
  virtual unsigned droppedFrameCount() const;
  virtual unsigned audioDecodedByteCount() const;
  virtual unsigned videoDecodedByteCount() const;

  // TODO(dshwang): remove |level|. crbug.com/443151
  virtual bool copyVideoTextureToPlatformTexture(
      blink::WebGraphicsContext3D* web_graphics_context,
      unsigned int texture,
      unsigned int level,
      unsigned int internal_format,
      unsigned int type,
      bool premultiply_alpha,
      bool flip_y);
  virtual bool copyVideoTextureToPlatformTexture(
      blink::WebGraphicsContext3D* web_graphics_context,
      unsigned int texture,
      unsigned int internal_format,
      unsigned int type,
      bool premultiply_alpha,
      bool flip_y);

  virtual MediaKeyException generateKeyRequest(
      const blink::WebString& key_system,
      const unsigned char* init_data,
      unsigned init_data_length);

  virtual MediaKeyException addKey(const blink::WebString& key_system,
                                   const unsigned char* key,
                                   unsigned key_length,
                                   const unsigned char* init_data,
                                   unsigned init_data_length,
                                   const blink::WebString& session_id);

  virtual MediaKeyException cancelKeyRequest(
      const blink::WebString& key_system,
      const blink::WebString& session_id);

  virtual void setContentDecryptionModule(
      blink::WebContentDecryptionModule* cdm,
      blink::WebContentDecryptionModuleResult result);

  void OnAddTextTrack(const TextTrackConfig& config,
                      const AddTextTrackDoneCB& done_cb);

  void OnReleaseTexture(unsigned texture_id, uint32 release_sync_point);

  void OnSetCurrentFrame(int width,
                         int height,
                         unsigned texture_id,
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
                                const std::vector<uint8>& init_data);

  // Called when a decoder detects that the key needed to decrypt the stream
  // is not available.
  void OnWaitingForDecryptionKey();

  void SetCdm(const CdmAttachedCB& cdm_attached_cb, CdmContext* cdm_context);

  // Called when a CDM has been attached to the |pipeline_|.
  void OnCdmAttached(blink::WebContentDecryptionModuleResult result,
                     bool success);

  void UpdatePlayingState(bool is_playing);

  // A pointer back to the compositor to inform it about state changes. This is
  // not NULL while the compositor is actively using this webmediaplayer.
  // Accessed on main thread and on compositor thread when main thread is
  // blocked.
  cc::VideoFrameProvider::Client* video_frame_provider_client_;
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
  double pending_seek_seconds_;

  bool did_loading_progress_;

  // Save the list of buffered time ranges.
  blink::WebTimeRanges buffered_;

  blink::WebMediaPlayerClient* client_;

  // base::TickClock used by |interpolator_|.
  base::DefaultTickClock default_tick_clock_;

  // Tracks the most recent media time update and provides interpolated values
  // as playback progresses.
  media::TimeDeltaInterpolator interpolator_;

  base::WeakPtr<WebMediaPlayerDelegate> delegate_;

  bool supports_save_;

  // TODO:
  EncryptedMediaPlayerSupport encrypted_media_support_;

  static base::AtomicSequenceNumber next_player_id_;
  WebMediaPlayerMessageDispatcher message_dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(WebMediaPlayerGStreamer);
};

}  // namespace media

#endif  // MEDIA_GSTREAMER_WEBMEDIAPLAYER_GSTREAMER_H_
