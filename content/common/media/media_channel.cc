// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/media/media_channel.h"

#include <queue>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "content/public/common/content_switches.h"
#include "content/child/child_process.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/message_filter.h"
#include "content/common/media/media_channel_filter.h"
#include "content/common/media/media_player_messages_gstreamer.h"
#include "content/media/gstreamer/media_player_gstreamer.h"

#if defined(OS_POSIX)
#include "ipc/ipc_channel_posix.h"
#endif

namespace content {

MediaChannel::MediaChannel(int client_id, MediaChannelFilter* channel_filter)
    : client_id_(client_id), channel_filter_(channel_filter) {
  DCHECK(client_id);

  channel_id_ = IPC::Channel::GenerateVerifiedChannelID("media");
}

MediaChannel::~MediaChannel() {
}

void MediaChannel::Init(base::SingleThreadTaskRunner* io_task_runner,
                        base::WaitableEvent* shutdown_event) {
  DCHECK(!channel_.get());

  // Map renderer ID to a (single) channel to that process.
  channel_ =
      IPC::SyncChannel::Create(channel_id_, IPC::Channel::MODE_SERVER, this,
                               io_task_runner, false, shutdown_event);
}

std::string MediaChannel::GetChannelName() {
  return channel_id_;
}

#if defined(OS_POSIX)
base::ScopedFD MediaChannel::TakeRendererFileDescriptor() {
  if (!channel_) {
    NOTREACHED();
    return base::ScopedFD();
  }
  return channel_->TakeClientFileDescriptor();
}
#endif  // defined(OS_POSIX)

bool MediaChannel::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;

  IPC_BEGIN_MESSAGE_MAP(MediaChannel, message)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_Load, OnMediaPlayerLoad)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_Start, OnMediaPlayerStart)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_Pause, OnMediaPlayerPause)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_Seek, OnMediaPlayerSeek)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_Release, OnMediaPlayerRelease)
  IPC_MESSAGE_HANDLER(MediaPlayerMsg_ReleaseTexture,
                      OnMediaPlayerReleaseTexture)
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MediaChannel::OnChannelError() {
  media_players_.clear();
  channel_filter_->RemoveChannel(client_id_);
}

MediaPlayerGStreamer* MediaChannel::GetMediaPlayer(int player_id) {
  MediaPlayerMap::const_iterator iter = media_players_.find(player_id);
  if (iter != media_players_.end())
    return iter->second;

  scoped_ptr<MediaPlayerGStreamer> player(
      channel_filter_->GetMediaPlayerFactory()->create(player_id, this));
  media_players_.set(player_id, player.Pass());
  return media_players_.find(player_id)->second;
}

void MediaChannel::OnMediaPlayerLoad(int player_id, GURL url) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Load(url);
  }
}

void MediaChannel::OnMediaPlayerStart(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Play();
  }
}

void MediaChannel::OnMediaPlayerPause(int player_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Pause();
  }
}

void MediaChannel::OnMediaPlayerSeek(int player_id, base::TimeDelta delta) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->Seek(delta);
  }
}

void MediaChannel::OnMediaPlayerRelease(int player_id) {
  media_players_.erase(player_id);
}

void MediaChannel::OnMediaPlayerReleaseTexture(int player_id,
                                               unsigned texture_id) {
  MediaPlayerGStreamer* player = GetMediaPlayer(player_id);
  if (player) {
    player->ReleaseTexture(texture_id);
  }
}

void MediaChannel::SendMediaError(int player_id, int error) {
  Send(new MediaPlayerMsg_MediaError(player_id, error));
}

void MediaChannel::SendMediaPlaybackCompleted(int player_id) {
  Send(new MediaPlayerMsg_MediaPlaybackCompleted(player_id));
}

void MediaChannel::SendMediaDurationChanged(int player_id,
                                            const base::TimeDelta& duration) {
  Send(new MediaPlayerMsg_MediaDurationChanged(player_id, duration));
}

void MediaChannel::SendSeekCompleted(int player_id,
                                     const base::TimeDelta& current_time) {
  Send(new MediaPlayerMsg_SeekCompleted(player_id, current_time));
}

void MediaChannel::SendMediaVideoSizeChanged(int player_id,
                                             int width,
                                             int height) {
  Send(new MediaPlayerMsg_MediaVideoSizeChanged(player_id, width, height));
}

void MediaChannel::SendMediaTimeUpdate(int player_id,
                                       base::TimeDelta current_timestamp,
                                       base::TimeTicks current_time_ticks) {
  Send(new MediaPlayerMsg_MediaTimeUpdate(player_id, current_timestamp,
                                          current_time_ticks));
}

void MediaChannel::SendBufferingUpdate(int player_id, int percent) {
  Send(new MediaPlayerMsg_MediaBufferingUpdate(player_id, percent));
}

void MediaChannel::SendMediaPlayerReleased(int player_id) {
  Send(new MediaPlayerMsg_MediaPlayerReleased(player_id));
}

void MediaChannel::SendDidMediaPlayerPlay(int player_id) {
  Send(new MediaPlayerMsg_DidMediaPlayerPlay(player_id));
}

void MediaChannel::SendDidMediaPlayerPause(int player_id) {
  Send(new MediaPlayerMsg_DidMediaPlayerPause(player_id));
}

void MediaChannel::SendSetCurrentFrame(int player_id,
                                       int width,
                                       int height,
                                       unsigned texture_id,
                                       const std::vector<int32_t>& name) {
  Send(new MediaPlayerMsg_SetCurrentFrame(player_id, width, height, texture_id,
                                          name));
}

bool MediaChannel::Send(IPC::Message* message) {
  // The media process must never send a synchronous IPC message to the renderer
  // process. This could result in deadlock.
  DCHECK(!message->is_sync());

  if (!channel_) {
    delete message;
    return false;
  }

  return channel_->Send(message);
}

}  // namespace content
