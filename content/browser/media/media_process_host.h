// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_H_
#define CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_H_

#include <map>
#include <queue>
#include <set>
#include <string>

#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "content/public/browser/media_data_manager.h"
#include "content/common/media/media_process_launch_causes.h"
#include "ipc/ipc_sender.h"
#include "ipc/message_filter.h"

namespace IPC {
struct ChannelHandle;
}

namespace content {
class BrowserChildProcessHostImpl;
class GpuMessageFilter;
class MediaMainThread;
class RenderWidgetHelper;
class InProcessChildThreadParams;

typedef base::Thread* (*MediaMainThreadFactoryFunction)(
    const InProcessChildThreadParams&);

class MediaProcessHost : public BrowserChildProcessHostDelegate,
                         public IPC::Sender,
                         public base::NonThreadSafe {
 public:
  enum MediaProcessKind {
    MEDIA_PROCESS_KIND_UNSANDBOXED,
    MEDIA_PROCESS_KIND_SANDBOXED,
    MEDIA_PROCESS_KIND_COUNT
  };

  typedef base::Callback<void(const IPC::ChannelHandle&)>
      EstablishChannelCallback;

  // Creates a new MediaProcessHost or gets an existing one, resulting in the
  // launching of a media process if required.  Returns null on failure. It
  // is not safe to store the pointer once control has returned to the message
  // loop as it can be destroyed. Instead store the associated GPU host ID.
  // This could return NULL if media access is not allowed (blacklisted).
  CONTENT_EXPORT static MediaProcessHost* Get(MediaProcessKind kind,
                                              CauseForMediaLaunch cause);

  // Retrieves a list of process handles for all media processes.
  static void GetProcessHandles(
      const MediaDataManager::GetMediaProcessHandlesCallback& callback);

  static void SendOnIO(MediaProcessKind kind,
                       CauseForMediaLaunch cause,
                       IPC::Message* message);

  CONTENT_EXPORT static void RegisterMediaMainThreadFactory(
      MediaMainThreadFactoryFunction create);

  // Get the media process host for the media process with the given ID. Returns
  // null if the process no longer exists.
  static MediaProcessHost* FromID(int host_id);
  int host_id() const { return host_id_; }

  // IPC::Sender implementation.
  bool Send(IPC::Message* msg) override;

  // Adds a message filter to the MediaProcessHost's channel.
  void AddFilter(IPC::MessageFilter* filter);

  // Tells the media process to create a new channel for communication with a
  // client. Once the Media process responds asynchronously with the IPC handle
  // and MediaInfo, we call the callback.
  void EstablishMediaChannel(int render_id,
                             const EstablishChannelCallback& callback);

  // What kind of media process, e.g. sandboxed or unsandboxed.
  MediaProcessKind kind();

  void ForceShutdown();

 private:
  static bool ValidateHost(MediaProcessHost* host);

  MediaProcessHost(int host_id, MediaProcessKind kind);
  ~MediaProcessHost() override;

  bool Init();

  void CreateGpuMessageFilter();
  void CreateResourceMessageFilter(int renderer_id,
                                   const EstablishChannelCallback& callback);
  void SendEstablishMediaChannel(int render_id,
                                 const EstablishChannelCallback& callback);

  // Post an IPC message to the UI shim's message handler on the UI thread.
  void RouteOnUIThread(const IPC::Message& message);

  // BrowserChildProcessHostDelegate implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void OnChannelConnected(int32 peer_pid) override;
  void OnProcessLaunched() override;
  void OnProcessCrashed(int exit_code) override;

  // Message handlers.
  void OnInitialized(bool result);
  void OnChannelEstablished(const IPC::ChannelHandle& channel_handle);

  bool LaunchMediaProcess(const std::string& channel_id);

  void SendOutstandingReplies();

  // The serial number of the MediaProcessHost / MediaProcessHostUIShim pair.
  int host_id_;

  // These are the channel requests that we have already sent to
  // the Media process, but haven't heard back about yet.
  std::queue<EstablishChannelCallback> channel_requests_;

  // Queued messages to send when the process launches.
  std::queue<IPC::Message*> queued_messages_;

  // Whether the Media process is valid, set to false after Send() failed.
  bool valid_;

  // Whether we are running a Media thread inside the browser process instead
  // of a separate Media process.
  bool in_process_;

  MediaProcessKind kind_;

  scoped_ptr<base::Thread> in_process_media_thread_;

  // Whether we actually launched a Media process.
  bool process_launched_;

  // Whether the Media process successfully initialized.
  bool initialized_;

  int gpu_client_id_;

  scoped_ptr<BrowserChildProcessHostImpl> process_;

  // Used to allow a RenderWidgetHost to intercept various messages on the
  // IO thread.
  scoped_refptr<RenderWidgetHelper> widget_helper_;

  // The filter for GPU-related messages coming from the renderer.
  // Thread safety note: this field is to be accessed from the UI thread.
  // We don't keep a reference to it, to avoid it being destroyed on the UI
  // thread, but we clear this field when we clear channel_. When channel_ goes
  // away, it posts a task to the IO thread to destroy it there, so we know that
  // it's valid if non-NULL.
  GpuMessageFilter* gpu_message_filter_;

  // TODO: because of assert in debug mode we had to split weak factory for UI
  // and IO threads.
  // We should find a better solution.
  base::WeakPtrFactory<MediaProcessHost> weak_factory_ui_;
  base::WeakPtrFactory<MediaProcessHost> weak_factory_io_;

  DISALLOW_COPY_AND_ASSIGN(MediaProcessHost);
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_H_
