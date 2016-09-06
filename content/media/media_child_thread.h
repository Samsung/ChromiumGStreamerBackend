// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_MEDIA_CHILD_THREAD_H_
#define CONTENT_MEDIA_MEDIA_CHILD_THREAD_H_

#include <queue>
#include <string>

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "content/child/blink_platform_impl.h"
#include "content/child/child_thread_impl.h"
#include "content/common/gpu_process_launch_causes.h"
#include "content/common/media/media_config.h"
#include "content/common/media/media_player_channel.h"
#include "content/common/media/media_player_channel_filter.h"
#include "content/common/url_loader_factory.mojom.h"
#include "content/renderer/mojo/blink_interface_provider_impl.h"
#include "gpu/ipc/client/gpu_channel_host.h"

namespace base {
class MessageLoopProxy;
class Thread;
}

namespace cc {
class ContextProvider;
}

namespace cc_blink {
class ContextProviderWebContext;
}

namespace sandbox {
class TargetServices;
}

namespace content {

class CompositorForwardingMessageFilter;
class ContextProviderCommandBuffer;
class GpuChannelHost;
class MediaPlayerGStreamer;

// The main thread of the media process. There will only ever be one of
// these per process. It does process initialization and shutdown,
// sends commands to the media.
class MediaChildThread : public ChildThreadImpl, public gpu::GpuChannelHostFactory {
 public:
  typedef std::queue<IPC::Message*> DeferredMessages;

  explicit MediaChildThread(bool dead_on_arrival,
                            const DeferredMessages& deferred_messages);

  explicit MediaChildThread(const InProcessChildThreadParams& params);

  ~MediaChildThread() override;

  void Shutdown() override;

  void Init(const base::Time& process_start_time);
  void StopWatchdog();

  // ChildThread overrides.
  bool Send(IPC::Message* msg) override;
  bool OnControlMessageReceived(const IPC::Message& msg) override;

  scoped_refptr<cc::ContextProvider> CreateSharedContextProvider();

  mojom::URLLoaderFactory* url_loader_factory() {
    return url_loader_factory_.get();
  }

 private:
  // GpuChannelHostFactory implementation:
  bool IsMainThread() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetIOThreadTaskRunner() override;
  std::unique_ptr<base::SharedMemory> AllocateSharedMemory(size_t size) override;

  // Synchronously establish a channel to the GPU plugin if not previously
  // established or if it has been lost (for example if the GPU plugin crashed).
  // If there is a pending asynchronous request, it will be completed by the
  // time this routine returns.
  scoped_refptr<gpu::GpuChannelHost> EstablishGpuChannelSync();

  // Message handlers.
  void OnInitialize();

  void OnClean();
  void OnCrash();
  void OnHang();

  // Set this flag to true if a fatal error occurred before we receive the
  // OnInitialize message, in which case we just declare ourselves DOA.
  bool dead_on_arrival_;

  // Error messages collected in media_main() before the thread is created.
  DeferredMessages deferred_messages_;

  // Whether the media thread is running in the browser process.
  bool in_browser_process_;

  base::Time process_start_time_;

  scoped_refptr<MediaPlayerChannelFilter> media_channel_filter_;

  // The channel from the media process to the GPU process.
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_;

  // Cache of variables that are needed on the compositor thread by
  // GpuChannelHostFactory methods.
  scoped_refptr<base::SingleThreadTaskRunner> io_thread_task_runner_;

  std::unique_ptr<base::Thread> gl_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> gl_task_runner_;

  scoped_refptr<cc::ContextProvider> provider_;

  std::unique_ptr<BlinkInterfaceProviderImpl> blink_interface_provider_;

  std::unique_ptr<content::BlinkPlatformImpl> blink_platform_;

  mojom::URLLoaderFactoryPtr url_loader_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaChildThread);
};

}  // namespace content

#endif  // CONTENT_MEDIA_MEDIA_CHILD_THREAD_H_
