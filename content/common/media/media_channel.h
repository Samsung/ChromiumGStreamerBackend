// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_H_
#define CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_H_

#include <deque>
#include <string>

#include "base/id_map.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "build/build_config.h"
#include "content/common/message_router.h"
#include "ipc/ipc_sync_channel.h"
#include "url/gurl.h"

namespace base {
class WaitableEvent;
}

namespace IPC {
class MessageFilter;
}

namespace content {
class MediaChannelFilter;
class MediaPlayerGStreamer;

// Encapsulates an IPC channel between the media process and one renderer
// process. On the renderer side there's a corresponding web media player.
class MediaChannel : public IPC::Listener, public IPC::Sender {
 public:
  // Takes ownership of the renderer process handle.
  MediaChannel(int, MediaChannelFilter*);
  ~MediaChannel() override;

  void Init(base::SingleThreadTaskRunner* io_task_runner,
            base::WaitableEvent* shutdown_event);

  // Returns the name of the associated IPC channel.
  std::string GetChannelName();

#if defined(OS_POSIX)
  base::ScopedFD TakeRendererFileDescriptor();
#endif  // defined(OS_POSIX)

  base::ProcessId renderer_pid() const { return channel_->GetPeerPID(); }

  int client_id() const { return client_id_; }

  MediaPlayerGStreamer* GetMediaPlayer(int player_id);

  // IPC::Listener implementation:
  bool OnMessageReceived(const IPC::Message& msg) override;
  void OnChannelError() override;

  void OnMediaPlayerCreate(int player_id);
  void OnMediaPlayerLoad(int player_id, GURL url, unsigned position_update_interval_ms);
  void OnMediaPlayerStart(int player_id);
  void OnMediaPlayerPause(int player_id);
  void OnMediaPlayerSeek(int player_id, base::TimeDelta delta);
  void OnMediaPlayerRelease(int player_id);
  void OnMediaPlayerReleaseTexture(int player_id, unsigned texture_id);
  void OnMediaPlayerAddSourceId(int player_id,
                                const std::string& source_id,
                                const std::string& type,
                                const std::vector<std::string>& codecs);
  void OnMediaPlayerRemoveSourceId(int player_id, const std::string& source_id);
  void OnMediaPlayerSetDuration(int player_id, const base::TimeDelta& duration);
  void OnMediaPlayerMarkEndOfStream(int player_id);
  void OnMediaPlayerUnmarkEndOfStream(int player_id);
  void OnMediaPlayerSetSequenceMode(int player_id,
                                    const std::string& source_id,
                                    bool sequence_mode);
  void OnMediaPlayerAppendData(int player_id,
                               const std::string& source_id,
                               const std::vector<unsigned char>& data,
                               const std::vector<base::TimeDelta>& times);
  void OnMediaPlayerAbort(int player_id, const std::string& source_id);
  void OnMediaPlayerSetGroupStartTimestampIfInSequenceMode(
      int player_id,
      const std::string& source_id,
      const base::TimeDelta& timestamp_offset);
  void OnMediaPlayerRemoveSegment(int player_id,
                                  const std::string& source_id,
                                  const base::TimeDelta& start,
                                  const base::TimeDelta& end);
  void OnMediaPlayerAddKey(int player_id,
                           const std::string& session_id,
                           const std::string& key_id,
                           const std::string& key);

  // IPC::Sender implementation:
  bool Send(IPC::Message* msg) override;

  void SendMediaError(int player_id, int error);
  void SendMediaPlaybackCompleted(int player_id);
  void SendMediaDurationChanged(int player_id, const base::TimeDelta& duration);
  void SendSeekCompleted(int player_id, const base::TimeDelta& current_time);
  void SendMediaVideoSizeChanged(int player_id, int width, int height);
  void SendMediaTimeUpdate(int player_id,
                           base::TimeDelta current_timestamp,
                           base::TimeTicks current_time_ticks);
  void SendBufferingUpdate(int player_id, int percent);
  void SendMediaPlayerReleased(int player_id);
  void SendDidMediaPlayerPlay(int player_id);
  void SendDidMediaPlayerPause(int player_id);
  void SendSetCurrentFrame(int player_id,
                           int width,
                           int height,
                           unsigned texture_id,
                           unsigned target,
                           const std::vector<int32_t>& name);
  void SendSourceSelected(int player_id);
  void SendDidAddSourceId(int player_id, const std::string& source_id);
  void SendDidRemoveSourceId(int player_id, const std::string& source_id);
  void SendInitSegmentReceived(int player_id, const std::string& source_id);
  void SendBufferedRangeUpdate(int player_id,
                               const std::string& source_id,
                               const std::vector<base::TimeDelta>& ranges);
  void SendTimestampOffsetUpdate(int player_id,
                                 const std::string& source_id,
                                 const base::TimeDelta& timestamp_offset);
  void SendNeedKey(int player_id,
                   const std::string& system_id,
                   const std::vector<unsigned char>& init_data);

 private:
  scoped_ptr<IPC::SyncChannel> channel_;

  // The id of the client who is on the other side of the channel.
  int client_id_;

  // Uniquely identifies the channel within this GPU process.
  std::string channel_id_;

  scoped_refptr<MediaChannelFilter> channel_filter_;

  typedef base::ScopedPtrHashMap<int, scoped_ptr<MediaPlayerGStreamer>>
      MediaPlayerMap;
  MediaPlayerMap media_players_;

  DISALLOW_COPY_AND_ASSIGN(MediaChannel);
};

}  // namespace content

#endif  // CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_H_
