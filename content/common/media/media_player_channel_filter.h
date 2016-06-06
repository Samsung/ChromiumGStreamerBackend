// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_FILTER_H_
#define CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_FILTER_H_

#include <deque>
#include <string>
#include <vector>

#include "base/containers/scoped_ptr_hash_map.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/common/content_param_traits.h"
#include "content/media/gstreamer/media_player_gstreamer.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"
#include "ipc/message_filter.h"

namespace base {
class WaitableEvent;
}

namespace IPC {
struct ChannelHandle;
class MessageRouter;
class SyncChannel;
}

namespace content {
class MediaPlayerChannel;
class MediaPlayerGStreamerFactory;

// A MediaChannelManager is responsible for managing the lifetimes of media
// channels and forwarding IPC requests from the
// browser process to them based on the corresponding renderer ID.
class CONTENT_EXPORT MediaPlayerChannelFilter : public IPC::MessageFilter {
 public:
  MediaPlayerChannelFilter(IPC::MessageRouter* router,
                     base::SingleThreadTaskRunner* io_task_runner,
                     base::WaitableEvent* shutdown_event,
                     IPC::SyncChannel* channel,
                     MediaPlayerGStreamerFactory* media_player_factory);
  ~MediaPlayerChannelFilter() override;

  // Remove the channel for a particular renderer.
  void RemoveChannel(int client_id);

  // Listener overrides.
  bool OnMessageReceived(const IPC::Message& msg) override;

  // Sender overrides.
  bool Send(IPC::Message* msg);

  bool HandleMessagesScheduled();
  uint64_t MessagesProcessed();

  MediaPlayerChannel* LookupChannel(int32_t client_id);
  MediaPlayerGStreamerFactory* GetMediaPlayerFactory() {
    return media_player_factory_.get();
  }

 private:
  typedef base::ScopedPtrHashMap<int, std::unique_ptr<MediaPlayerChannel>> MediaChannelMap;

  // Message handlers.
  void OnEstablishChannel(int client_id);
  void OnCloseChannel(const IPC::ChannelHandle& channel_handle);

  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  base::WaitableEvent* shutdown_event_;

  // Used to send and receive IPC messages from the browser process.
  IPC::MessageRouter* const router_;

  // These objects manage channels to individual renderer processes there is
  // one channel for each renderer process that has connected to this media
  // process.
  MediaChannelMap media_channels_;
  IPC::SyncChannel* channel_;
  scoped_refptr<IPC::MessageFilter> filter_;
  std::unique_ptr<MediaPlayerGStreamerFactory> media_player_factory_;

  // Member variables should appear before the WeakPtrFactory, to ensure
  // that any WeakPtrs to Controller are invalidated before its members
  // variable's destructors are executed, rendering them invalid.
  base::WeakPtrFactory<MediaPlayerChannelFilter> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaPlayerChannelFilter);
};

}  // namespace content

#endif  // CONTENT_COMMON_MEDIA_MEDIA_CHANNEL_FILTER_H_
