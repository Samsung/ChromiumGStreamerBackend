// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_MEDIA_MESSAGE_DISPATCHER_HOST_H_
#define CONTENT_COMMON_MEDIA_MESSAGE_DISPATCHER_HOST_H_

#include <string>
#include <vector>

#include "base/atomic_sequence_num.h"
#include "base/containers/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/synchronization/lock.h"
#include "content/common/content_export.h"
#include "content/common/message_router.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_channel.h"
#include "ipc/message_filter.h"
#include "ui/events/latency_info.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gpu_preference.h"

class GURL;

namespace base {
class MessageLoop;
class WaitableEvent;
}

namespace IPC {
class SyncMessageFilter;
}

namespace media {
class WebMediaPlayerMessageDispatcher;
}

namespace content {

// Encapsulates an IPC channel between the client and one media process.
// On the media process side there's a corresponding channel.
// Every method can be called on any thread with a message loop, except for the
// IO thread.
class MediaChannelHost : public IPC::Sender,
                         public base::RefCountedThreadSafe<MediaChannelHost> {
 public:
  // Must be called on the main thread
  static scoped_refptr<MediaChannelHost> Create(
      const IPC::ChannelHandle& channel_handle,
      base::WaitableEvent* shutdown_event);

  bool IsLost() const {
    DCHECK(channel_filter_.get());
    return channel_filter_->IsLost();
  }

  // IPC::Sender implementation:
  bool Send(IPC::Message* msg) override;

  void RegisterDispatcher(int, media::WebMediaPlayerMessageDispatcher*);
  void RemoveDispatcher(int);

  // Destroy this channel.
  void DestroyChannel();

 private:
  friend class base::RefCountedThreadSafe<MediaChannelHost>;
  MediaChannelHost();
  ~MediaChannelHost() override;
  void Connect(const IPC::ChannelHandle& channel_handle,
               base::WaitableEvent* shutdown_event);
  bool InternalSend(IPC::Message* msg);
  void InternalFlush();

  // A filter used internally to route incoming messages from the IO thread
  // to the correct message loop. It also maintains some shared state between
  // all the contexts.
  class MessageFilter : public IPC::MessageFilter {
   public:
    MessageFilter();

    void RegisterDispatcher(int, media::WebMediaPlayerMessageDispatcher*);
    void RemoveDispatcher(int);

    // IPC::MessageFilter implementation
    // (called on the IO thread):
    bool OnMessageReceived(const IPC::Message& msg) override;
    void OnChannelError() override;

    // The following methods can be called on any thread.

    // Whether the channel is lost.
    bool IsLost() const;

   private:
    ~MessageFilter() override;

    typedef base::hash_map<int, media::WebMediaPlayerMessageDispatcher*>
        WebMediaPlayerDispatcherMap;

    // Threading notes: |dispatchers_| is only accessed on the IO thread. Every
    // other field is protected by |lock_|.
    WebMediaPlayerDispatcherMap dispatchers_;

    // Protects all fields below this one.
    mutable base::Lock lock_;

    // Whether the channel has been lost.
    bool lost_;
  };

  scoped_ptr<IPC::SyncChannel> channel_;
  scoped_refptr<MessageFilter> channel_filter_;

  // A filter for sending messages from thread other than the main thread.
  scoped_refptr<IPC::SyncMessageFilter> sync_filter_;

  DISALLOW_COPY_AND_ASSIGN(MediaChannelHost);
};

}  // namespace content

#endif  // CONTENT_COMMON_MEDIA_MESSAGE_DISPATCHER_HOST_H_
