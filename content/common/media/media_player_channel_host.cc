// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_player_channel_host.h"

#include <algorithm>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/posix/eintr_wrapper.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/trace_event.h"
#include "content/child/child_process.h"
#include "content/common/media/media_messages.h"
#include "content/public/child/child_thread.h"
#include "content/renderer/media/gstreamer/webmediaplayer_gstreamer.h"
#include "ipc/ipc_sync_message_filter.h"
#include "url/gurl.h"

using base::AutoLock;

namespace content {

// static
scoped_refptr<MediaPlayerChannelHost> MediaPlayerChannelHost::Create(
    const IPC::ChannelHandle& channel_handle,
    base::WaitableEvent* shutdown_event) {
  scoped_refptr<MediaPlayerChannelHost> dispatcher = new MediaPlayerChannelHost;
  dispatcher->Connect(channel_handle, shutdown_event);
  return dispatcher;
}

MediaPlayerChannelHost::MediaPlayerChannelHost() {}

static bool IsMainThread() {
  return !!ChildThread::Get();
}

void MediaPlayerChannelHost::Connect(const IPC::ChannelHandle& channel_handle,
                               base::WaitableEvent* shutdown_event) {
  // Open a channel to the media process. We pass NULL as the main listener here
  // since we need to filter everything to route it to the right thread.
  channel_ = IPC::SyncChannel::Create(
      channel_handle, IPC::Channel::MODE_CLIENT, NULL,
      ChildProcess::current()->io_task_runner(), true, shutdown_event);

  sync_filter_ = channel_->CreateSyncMessageFilter();

  channel_->AddFilter(sync_filter_.get());

  channel_filter_ = new MessageFilter();

  // Install the filter last, because we intercept all leftover
  // messages.
  channel_->AddFilter(channel_filter_.get());
}

bool MediaPlayerChannelHost::Send(IPC::Message* msg) {
  std::unique_ptr<IPC::Message> message(msg);
  // The media process never sends synchronous IPCs so clear the unblock flag to
  // preserve order.
  message->set_unblock(false);

  if (IsMainThread()) {
    // channel_ is only modified on the main thread, so we don't need to take a
    // lock here.
    if (!channel_) {
      DVLOG(1) << "GpuChannelHost::Send failed: Channel already destroyed";
      return false;
    }

    base::ThreadRestrictions::ScopedAllowWait allow_wait;
    bool result = channel_->Send(message.release());
    if (!result)
      DVLOG(1) << "MediaPlayerChannelHost::Send failed: Channel::Send failed";
    return result;
  }

  bool result = sync_filter_->Send(message.release());

  if (!result)
    VLOG(1) << "MediaPlayerChannelHost::Send SyncMessageFilter failed";

  return result;
}

void MediaPlayerChannelHost::DestroyChannel() {
  DCHECK(IsMainThread());
  channel_.reset();
}

void MediaPlayerChannelHost::RegisterDispatcher(
    int player_id,
    media::WebMediaPlayerMessageDispatcher* dispatcher) {
  channel_filter_->RegisterDispatcher(player_id, dispatcher);
}

void MediaPlayerChannelHost::RemoveDispatcher(int player_id) {
  channel_filter_->RemoveDispatcher(player_id);
}

MediaPlayerChannelHost::~MediaPlayerChannelHost() {
  DestroyChannel();
}

MediaPlayerChannelHost::MessageFilter::MessageFilter() : lost_(false) {}

MediaPlayerChannelHost::MessageFilter::~MessageFilter() {}

void MediaPlayerChannelHost::MessageFilter::RegisterDispatcher(
    int player_id,
    media::WebMediaPlayerMessageDispatcher* dispatcher) {
  dispatchers_[player_id] = dispatcher;
}

void MediaPlayerChannelHost::MessageFilter::RemoveDispatcher(int player_id) {
  dispatchers_.erase(player_id);
}

bool MediaPlayerChannelHost::MessageFilter::OnMessageReceived(
    const IPC::Message& message) {
  // Never handle sync message replies or we will deadlock here.
  if (message.is_reply())
    return false;

  WebMediaPlayerDispatcherMap::iterator iter =
      dispatchers_.find(message.routing_id());
  if (iter == dispatchers_.end())
    return false;

  media::WebMediaPlayerMessageDispatcher* dispatcher = iter->second;
  scoped_refptr<base::SingleThreadTaskRunner> taskRunner =
      dispatcher->GetTaskRunner();
  base::WeakPtr<IPC::Listener> listener = dispatcher->AsWeakPtr();

  taskRunner->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&IPC::Listener::OnMessageReceived),
                 listener, message));

  return true;
}

void MediaPlayerChannelHost::MessageFilter::OnChannelError() {
  // Set the lost state before signalling the proxies. That way, if they
  // themselves post a task to recreate the context, they will not try to re-use
  // this channel host.
  {
    AutoLock lock(lock_);
    lost_ = true;
  }

  dispatchers_.clear();
}

bool MediaPlayerChannelHost::MessageFilter::IsLost() const {
  AutoLock lock(lock_);
  return lost_;
}

}  // namespace content
