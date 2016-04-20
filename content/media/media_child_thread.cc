// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/media_child_thread.h"

#include <thread>

#include "base/bind.h"
#include "base/debug/stack_trace.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_pump_glib.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/worker_pool.h"
#include "build/build_config.h"
#include "content/browser/gpu/browser_gpu_channel_host_factory.h"
#include "content/child/child_gpu_memory_buffer_manager.h"
#include "content/child/child_process.h"
#include "content/child/thread_safe_sender.h"
#include "content/common/gpu/client/context_provider_command_buffer.h"
#include "content/common/gpu/client/gpu_channel_host.h"
#include "content/common/gpu/gpu_host_messages.h"
#include "content/common/gpu/gpu_messages.h"
#include "content/common/gpu/gpu_process_launch_causes.h"
#include "content/common/media/media_messages.h"
#include "content/browser/gpu/gpu_surface_tracker.h"
#include "content/media/gstreamer/media_player_gstreamer.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "gin/public/debug.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_message_filter.h"
#include "media/base/audio_hardware_config.h"
#include "media/base/media.h"
#include "public/web/WebKit.h"

using base::ThreadRestrictions;

namespace content {
namespace {

blink::WebGraphicsContext3D::Attributes GetOffscreenAttribs() {
  blink::WebGraphicsContext3D::Attributes attributes;
  attributes.shareResources = true;
  attributes.depth = false;
  attributes.stencil = false;
  attributes.antialias = false;
  attributes.noAutomaticFlushes = true;
  return attributes;
}

static base::LazyInstance<scoped_refptr<ThreadSafeSender>>
    g_thread_safe_sender = LAZY_INSTANCE_INITIALIZER;

bool MediaProcessLogMessageHandler(int severity,
                                   const char* file,
                                   int line,
                                   size_t message_start,
                                   const std::string& str) {
  std::string header = str.substr(0, message_start);
  std::string message = str.substr(message_start);

  g_thread_safe_sender.Get()->Send(
      new MediaHostMsg_OnLogMessage(severity, header, message));

  return false;
}

ChildThreadImpl::Options GetOptions() {
  ChildThreadImpl::Options::Builder builder;

  return builder.Build();
}

}  // namespace

MediaChildThread::MediaChildThread(bool dead_on_arrival,
                                   const DeferredMessages& deferred_messages)
    : ChildThreadImpl(GetOptions()),
      dead_on_arrival_(dead_on_arrival),
      deferred_messages_(deferred_messages),
      in_browser_process_(false),
      blink_platform_(new content::BlinkPlatformImpl) {
  g_thread_safe_sender.Get() = thread_safe_sender();
  blink::initialize(blink_platform_.get());
}

MediaChildThread::MediaChildThread(const InProcessChildThreadParams& params)
    : ChildThreadImpl(
          ChildThreadImpl::Options::Builder().InBrowserProcess(params).Build()),
      dead_on_arrival_(false),
      in_browser_process_(true),
      blink_platform_(nullptr) {
  DCHECK(base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kSingleProcess) ||
         base::CommandLine::ForCurrentProcess()->HasSwitch(
             switches::kInProcessMedia));
  g_thread_safe_sender.Get() = thread_safe_sender();
}

MediaChildThread::~MediaChildThread() {
  if (blink_platform_)
    blink::shutdown();
}

void MediaChildThread::Shutdown() {
  ChildThreadImpl::Shutdown();
  logging::SetLogMessageHandler(NULL);

  gl_thread_.reset();

  if (gpu_channel_.get())
    gpu_channel_->DestroyChannel();
}

void MediaChildThread::Init(const base::Time& process_start_time) {
  process_start_time_ = process_start_time;
}

bool MediaChildThread::Send(IPC::Message* msg) {
  // The media process must never send a synchronous IPC message to the browser
  // process. This could result in deadlock.
  DCHECK(!msg->is_sync() || msg->type() == GpuHostMsg_EstablishGpuChannel::ID);
  return ChildThreadImpl::Send(msg);
}

bool MediaChildThread::OnControlMessageReceived(const IPC::Message& msg) {
  // Call from the main thread, not the io thread.

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MediaChildThread, msg)
    IPC_MESSAGE_HANDLER(MediaMsg_Initialize, OnInitialize)
    IPC_MESSAGE_HANDLER(MediaMsg_Clean, OnClean)
    IPC_MESSAGE_HANDLER(MediaMsg_Crash, OnCrash)
    IPC_MESSAGE_HANDLER(MediaMsg_Hang, OnHang)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  if (handled)
    return true;

  return media_channel_filter_.get() &&
         media_channel_filter_->OnMessageReceived(msg);
}

scoped_refptr<cc::ContextProvider>
MediaChildThread::CreateSharedContextProvider() {
  // Created in the main thread but likely to be bind in another thread.
  DCHECK(IsMainThread());

  if (!provider_.get()) {
    provider_ = NULL;

    if (!provider_.get()) {
      if (!gpu_channel_.get())
        EstablishGpuChannelSync(
            CAUSE_FOR_GPU_LAUNCH_MEDIA_GSTREAMER_CONTEXT_INITIALIZE);

      provider_ = ContextProviderCommandBuffer::Create(
          CreateOffscreenContext3d(), MEDIA_GSTREAMER_CONTEXT);
    }
  }

  return provider_;
}

GpuChannelHost* MediaChildThread::EstablishGpuChannelSync(
    CauseForGpuLaunch cause_for_gpu_launch) {
  TRACE_EVENT0("media", "MediaChildThread::EstablishGpuChannelSync");

  if (gpu_channel_.get()) {
    // Do nothing if we already have a GPU channel or are already
    // establishing one.
    if (!gpu_channel_->IsLost())
      return gpu_channel_.get();

    // Recreate the channel if it has been lost.
    gpu_channel_->DestroyChannel();
    gpu_channel_ = NULL;
  }

  // Ask the browser for the channel name.
  int client_id = 0;
  IPC::ChannelHandle channel_handle;
  gpu::GPUInfo gpu_info;
  if (!Send(new GpuHostMsg_EstablishGpuChannel(cause_for_gpu_launch, &client_id,
                                               &channel_handle, &gpu_info)) ||
#if defined(OS_POSIX)
      channel_handle.socket.fd == -1 ||
#endif
      channel_handle.name.empty()) {
    // Otherwise cancel the connection.
    return NULL;
  }

  GetContentClient()->SetGpuInfo(gpu_info);

  io_thread_task_runner_ = ChildProcess::current()->io_task_runner();

  gpu_channel_ = GpuChannelHost::Create(
      this, client_id, gpu_info, channel_handle,
      ChildProcess::current()->GetShutDownEvent(), gpu_memory_buffer_manager());
  return gpu_channel_.get();
}

scoped_ptr<WebGraphicsContext3DCommandBufferImpl>
MediaChildThread::CreateOffscreenContext3d() {
  blink::WebGraphicsContext3D::Attributes attributes(GetOffscreenAttribs());
  bool lose_context_when_out_of_memory = true;
  return make_scoped_ptr(
      WebGraphicsContext3DCommandBufferImpl::CreateOffscreenContext(
          gpu_channel_.get(), attributes, lose_context_when_out_of_memory,
          GURL("chrome://gpu/MediaChildThread::CreateOffscreenContext3d"),
          WebGraphicsContext3DCommandBufferImpl::SharedMemoryLimits(), NULL));
}

bool MediaChildThread::IsMainThread() {
  return !!current();
}

scoped_refptr<base::SingleThreadTaskRunner>
MediaChildThread::GetIOThreadTaskRunner() {
  return io_thread_task_runner_;
}

scoped_ptr<base::SharedMemory> MediaChildThread::AllocateSharedMemory(
    size_t size) {
  return scoped_ptr<base::SharedMemory>(
      ChildThreadImpl::AllocateSharedMemory(size, thread_safe_sender()));
}

gfx::GLSurfaceHandle MediaChildThread::GetSurfaceHandle(
    int32_t surface_id) {
  return GpuSurfaceTracker::Get()->GetSurfaceHandle(surface_id);
}

class MessagePumpGlibLocal : public base::MessagePumpGlib {
 public:
  explicit MessagePumpGlibLocal(GMainContext* context) : context_(context) {}
  ~MessagePumpGlibLocal() override {
    g_main_context_pop_thread_default(context_);
  }

 private:
  GMainContext* context_;
};

static scoped_ptr<base::MessagePump> CreateMessagePumpGlibLocal() {
  GMainContext* context = g_main_context_new();
  g_main_context_push_thread_default(context);
  return scoped_ptr<base::MessagePump>(new MessagePumpGlibLocal(context));
}

void MediaChildThread::OnInitialize() {
  DCHECK_NE(ChildProcess::current()->io_task_runner(),
            base::ThreadTaskRunnerHandle::Get().get());
  VLOG(1) << "Media: Initializing";

  Send(new MediaHostMsg_Initialized(!dead_on_arrival_));
  while (!deferred_messages_.empty()) {
    Send(deferred_messages_.front());
    deferred_messages_.pop();
  }

  if (dead_on_arrival_) {
    LOG(ERROR) << "Exiting media process due to errors during initialization";
    base::MessageLoop::current()->QuitWhenIdle();
    return;
  }

  // We don't need to pipe log messages if we are running the GPU thread in
  // the browser process.
  if (!in_browser_process_)
    logging::SetLogMessageHandler(MediaProcessLogMessageHandler);

  gl_thread_.reset(new base::Thread("GLThread"));
  base::Thread::Options options;
  options.message_pump_factory = base::Bind(&CreateMessagePumpGlibLocal);

  gl_thread_->StartWithOptions(options);
  gl_task_runner_ = gl_thread_->task_runner();

  scoped_refptr<media::MediaLog> media_log(
      new media::MediaLog());  // TODO: new RenderMediaLog

  GStreamerBufferedDataSourceFactory::Init(media_log.get(),
                                           resource_dispatcher(),
                                           base::ThreadTaskRunnerHandle::Get());

  media_channel_filter_ = new MediaPlayerChannelFilter(
      GetRouter(), ChildProcess::current()->io_task_runner(),
      ChildProcess::current()->GetShutDownEvent(), channel(),
      new MediaPlayerGStreamerFactory(media_log.get(), resource_dispatcher(),
                                      base::ThreadTaskRunnerHandle::Get(),
                                      gl_task_runner_));
}

void MediaChildThread::OnClean() {
  VLOG(1) << "Media: Cleaning all resources";
}

void MediaChildThread::OnCrash() {
  VLOG(1) << "Media: Simulating media process crash";
  // Good bye, cruel world.
  volatile int* it_s_the_end_of_the_world_as_we_know_it = NULL;
  *it_s_the_end_of_the_world_as_we_know_it = 0xdead;
}

void MediaChildThread::OnHang() {
  VLOG(1) << "Media: Simulating media process hang";
  for (;;) {
    // TODO: Implement media process watchdog?
  }
}

}  // namespace content
