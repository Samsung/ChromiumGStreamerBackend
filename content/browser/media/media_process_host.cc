// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_process_host.h"

#include "base/base64.h"
#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram.h"
#include "base/sha1.h"
#include "base/threading/thread.h"
#include "content/browser/appcache/chrome_appcache_service.h"
#include "content/browser/browser_child_process_host_impl.h"
#include "content/browser/fileapi/chrome_blob_storage_context.h"
#include "storage/browser/fileapi/file_system_context.h"
#include "content/browser/host_zoom_level_context.h"
#include "content/browser/loader/resource_message_filter.h"
#include "content/browser/media/media_data_manager_impl.h"
#include "content/browser/media/media_process_host_ui_shim.h"
#include "content/browser/renderer_host/gpu_message_filter.h"
#include "content/browser/renderer_host/render_widget_helper.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/in_process_child_thread_params.h"
#include "content/common/resource_messages.h"
#include "content/common/media/media_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/appcache_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_message_filter.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/resource_context.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_switches.h"
#include "ipc/message_filter.h"
#include "media/base/media_switches.h"
#include "net/url_request/url_request_context_getter.h"

namespace content {

namespace {

// Indexed by MediaProcessKind. There is one of each kind maximum.
// This array may only be accessed from the IO thread.
MediaProcessHost*
    g_media_process_hosts[MediaProcessHost::MEDIA_PROCESS_KIND_COUNT];

void SendMediaProcessMessage(MediaProcessHost::MediaProcessKind kind,
                             CauseForMediaLaunch cause,
                             IPC::Message* message) {
  MediaProcessHost* host = MediaProcessHost::Get(kind, cause);
  if (host) {
    host->Send(message);
  } else {
    delete message;
  }
}

class MediaSandboxedProcessLauncherDelegate
    : public SandboxedProcessLauncherDelegate {
 public:
  MediaSandboxedProcessLauncherDelegate(base::CommandLine* cmd_line,
                                        ChildProcessHost* host)
      : ipc_fd_(host->TakeClientFileDescriptor()) {}

  ~MediaSandboxedProcessLauncherDelegate() override {}

  base::ScopedFD TakeIpcFd() override { return std::move(ipc_fd_); }

 private:
  base::ScopedFD ipc_fd_;
};

}  // anonymous namespace

// static
bool MediaProcessHost::ValidateHost(MediaProcessHost* host) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSingleProcess) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kInProcessMedia) ||
      host->valid_) {
    return true;
  }

  host->ForceShutdown();
  return false;
}

// static
MediaProcessHost* MediaProcessHost::Get(MediaProcessKind kind,
                                        CauseForMediaLaunch cause) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Don't grant further access to media if it is not allowed.
  MediaDataManagerImpl* media_data_manager =
      MediaDataManagerImpl::GetInstance();
  DCHECK(media_data_manager);

  if (g_media_process_hosts[kind] && ValidateHost(g_media_process_hosts[kind]))
    return g_media_process_hosts[kind];

  if (cause == CAUSE_FOR_MEDIA_LAUNCH_NO_LAUNCH)
    return NULL;

  static int last_host_id = 0;
  int host_id;
  host_id = ++last_host_id;

  MediaProcessHost* host = new MediaProcessHost(host_id, kind);
  if (host->Init())
    return host;

  delete host;
  return NULL;
}

// static
void MediaProcessHost::GetProcessHandles(
    const MediaDataManager::GetMediaProcessHandlesCallback& callback) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&MediaProcessHost::GetProcessHandles, callback));
    return;
  }

  std::list<base::ProcessHandle> handles;
  for (size_t i = 0; i < arraysize(g_media_process_hosts); ++i) {
    MediaProcessHost* host = g_media_process_hosts[i];
    if (host && ValidateHost(host))
      handles.push_back(host->process_->GetProcess().Handle());
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(callback, handles));
}

// static
void MediaProcessHost::SendOnIO(MediaProcessKind kind,
                                CauseForMediaLaunch cause,
                                IPC::Message* message) {
  if (!BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          base::Bind(&SendMediaProcessMessage, kind, cause, message))) {
    delete message;
  }
}

MediaMainThreadFactoryFunction g_media_main_thread_factory = NULL;

void MediaProcessHost::RegisterMediaMainThreadFactory(
    MediaMainThreadFactoryFunction create) {
  g_media_main_thread_factory = create;
}

// static
MediaProcessHost* MediaProcessHost::FromID(int host_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  for (int i = 0; i < MEDIA_PROCESS_KIND_COUNT; ++i) {
    MediaProcessHost* host = g_media_process_hosts[i];
    if (host && host->host_id_ == host_id && ValidateHost(host))
      return host;
  }

  return NULL;
}

MediaProcessHost::MediaProcessHost(int host_id, MediaProcessKind kind)
    : host_id_(host_id),
      valid_(true),
      in_process_(false),
      kind_(kind),
      process_launched_(false),
      initialized_(false),
      gpu_client_id_(ChildProcessHostImpl::GenerateChildProcessUniqueId()),
      gpu_message_filter_(nullptr),
      weak_factory_ui_(this),
      weak_factory_io_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  widget_helper_ = new RenderWidgetHelper();

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kSingleProcess) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kInProcessMedia)) {
    in_process_ = true;
  }

  // If the 'single Media process' policy ever changes, we still want to
  // maintain
  // it for 'gpu thread' mode and only create one instance of host and thread.
  DCHECK(!in_process_ || g_media_process_hosts[kind] == NULL);

  g_media_process_hosts[kind] = this;

  // Post a task to create the corresponding MediaProcessHostUIShim.  The
  // MediaProcessHostUIShim will be destroyed if either the browser exits,
  // in which case it calls MediaProcessHostUIShim::DestroyAll, or the
  // MediaProcessHost is destroyed, which happens when the corresponding GPU
  // process terminates or fails to launch.
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(base::IgnoreResult(&MediaProcessHostUIShim::Create), host_id));

  process_.reset(new BrowserChildProcessHostImpl(PROCESS_TYPE_MEDIA, this));
}

MediaProcessHost::~MediaProcessHost() {
  DCHECK(CalledOnValidThread());

  SendOutstandingReplies();

  // In case we never started, clean up.
  while (!queued_messages_.empty()) {
    delete queued_messages_.front();
    queued_messages_.pop();
  }

  // This is only called on the IO thread so no race against the constructor
  // for another MediaProcessHost.
  if (g_media_process_hosts[kind_] == this)
    g_media_process_hosts[kind_] = NULL;

  std::string message;
  if (!in_process_) {
    int exit_code;
    base::TerminationStatus status =
        process_->GetTerminationStatus(false /* known_dead */, &exit_code);

    if (status == base::TERMINATION_STATUS_NORMAL_TERMINATION ||
        status == base::TERMINATION_STATUS_ABNORMAL_TERMINATION) {
      UMA_HISTOGRAM_ENUMERATION("Media.MediaProcessExitCode", exit_code,
                                RESULT_CODE_LAST_CODE);
    }

    switch (status) {
      case base::TERMINATION_STATUS_NORMAL_TERMINATION:
        message = "The media process exited normally.";
        break;
      case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
        message = base::StringPrintf("The media process exited with code %d.",
                                     exit_code);
        break;
      case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
        message = "The media process is killed.";
        break;
      case base::TERMINATION_STATUS_PROCESS_CRASHED:
        message = "The media process crashed.";
        break;
      default:
        break;
    }
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&MediaProcessHostUIShim::Destroy, host_id_, message));
}

net::URLRequestContext* GetRequestContext(
    scoped_refptr<net::URLRequestContextGetter> request_context,
    scoped_refptr<net::URLRequestContextGetter> media_request_context,
    ResourceType resource_type) {
  // If the request has resource type of RESOURCE_TYPE_MEDIA, we use a request
  // context specific to media for handling it because these resources have
  // specific needs for caching.
  if (resource_type == RESOURCE_TYPE_MEDIA)
    return media_request_context->GetURLRequestContext();
  return request_context->GetURLRequestContext();
}

void GetContexts(
    ResourceContext* resource_context,
    scoped_refptr<net::URLRequestContextGetter> request_context,
    scoped_refptr<net::URLRequestContextGetter> media_request_context,
    ResourceType resource_type,
    int origin_pid,
    ResourceContext** resource_context_out,
    net::URLRequestContext** request_context_out) {
  *resource_context_out = resource_context;
  *request_context_out =
      GetRequestContext(request_context, media_request_context, resource_type);
}

void MediaProcessHost::CreateGpuMessageFilter() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!gpu_message_filter_) {
    gpu_message_filter_ = new GpuMessageFilter(gpu_client_id_);
    process_->AddFilter(gpu_message_filter_);
  }
}

void MediaProcessHost::CreateResourceMessageFilter(
    int renderer_id,
    const EstablishChannelCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Resource and Gpu message filters add is in sync with EstablishMediaChannel.
  // Add GpuMessageFilter if not done already.
  CreateGpuMessageFilter();

  // For now the storage of corresponding render process is used.
  // Maybe it is correct to create our own storage associated to the media
  // process.
  // But we suspect that the storage contains some required information to
  // create url connection properly like credentials.
  // If credentials are contained in the global browser context then we could
  // even setup only one ResourceMessageFilter as done for GpuMessageFilter.
  content::RenderProcessHost* rph =
      content::RenderProcessHost::FromID(renderer_id);
  content::StoragePartition* storage_partition = rph->GetStoragePartition();
  scoped_refptr<net::URLRequestContextGetter> request_context(
      storage_partition->GetURLRequestContext());
  scoped_refptr<net::URLRequestContextGetter> media_request_context(
      storage_partition->GetMediaURLRequestContext());
  BrowserContext* browser_context = rph->GetBrowserContext();

  ResourceMessageFilter::GetContextsCallback get_contexts_callback(
      base::Bind(&GetContexts, browser_context->GetResourceContext(),
                 request_context, media_request_context));

  ResourceMessageFilter* resource_message_filter = new ResourceMessageFilter(
      ChildProcessHostImpl::GenerateChildProcessUniqueId(), PROCESS_TYPE_MEDIA,
      static_cast<ChromeAppCacheService*>(
          storage_partition->GetAppCacheService()),
      ChromeBlobStorageContext::GetFor(browser_context),
      storage_partition->GetFileSystemContext(),
      static_cast<ServiceWorkerContextWrapper*>(
          storage_partition->GetServiceWorkerContext()),
      storage_partition->GetHostZoomLevelContext(), get_contexts_callback);
  process_->AddFilter(resource_message_filter);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&MediaProcessHost::SendEstablishMediaChannel,
                 weak_factory_io_.GetWeakPtr(), renderer_id, callback));
}

bool MediaProcessHost::Init() {
  TRACE_EVENT_INSTANT0("media", "Init", TRACE_EVENT_SCOPE_THREAD);

  std::string channel_id = process_->GetHost()->CreateChannel();
  if (channel_id.empty())
    return false;

  if (in_process_) {
    DCHECK_CURRENTLY_ON(BrowserThread::IO);
    DCHECK(g_media_main_thread_factory);
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

    MediaDataManagerImpl* media_data_manager =
        MediaDataManagerImpl::GetInstance();
    DCHECK(media_data_manager);
    media_data_manager->AppendMediaCommandLine(command_line);
    in_process_media_thread_.reset(
        g_media_main_thread_factory(InProcessChildThreadParams(
            channel_id, base::MessageLoop::current()->task_runner())));
    in_process_media_thread_->Start();

    OnProcessLaunched();  // Fake a callback that the process is ready.
  } else if (!LaunchMediaProcess(channel_id)) {
    return false;
  }

  if (!Send(new MediaMsg_Initialize()))
    return false;

  return true;
}

void MediaProcessHost::RouteOnUIThread(const IPC::Message& message) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&RouteToMediaProcessHostUIShimTask, host_id_, message));
}

bool MediaProcessHost::Send(IPC::Message* msg) {
  DCHECK(CalledOnValidThread());
  if (process_->GetHost()->IsChannelOpening()) {
    queued_messages_.push(msg);
    return true;
  }

  bool result = process_->Send(msg);
  if (!result) {
    // Channel is hosed, but we may not get destroyed for a while. Send
    // outstanding channel creation failures now so that the caller can restart
    // with a new process/channel without waiting.
    SendOutstandingReplies();
  }
  return result;
}

void MediaProcessHost::AddFilter(IPC::MessageFilter* filter) {
  DCHECK(CalledOnValidThread());
  process_->GetHost()->AddFilter(filter);
}

bool MediaProcessHost::OnMessageReceived(const IPC::Message& message) {
  DCHECK(CalledOnValidThread());
  IPC_BEGIN_MESSAGE_MAP(MediaProcessHost, message)
    IPC_MESSAGE_HANDLER(MediaHostMsg_Initialized, OnInitialized)
    IPC_MESSAGE_HANDLER(MediaHostMsg_ChannelEstablished, OnChannelEstablished)
    IPC_MESSAGE_UNHANDLED(RouteOnUIThread(message))
  IPC_END_MESSAGE_MAP()

  return true;
}

void MediaProcessHost::OnChannelConnected(int32_t peer_pid) {
  TRACE_EVENT0("media", "MediaProcessHost::OnChannelConnected");

  while (!queued_messages_.empty()) {
    Send(queued_messages_.front());
    queued_messages_.pop();
  }
}

void MediaProcessHost::SendEstablishMediaChannel(
    int render_id,
    const EstablishChannelCallback& callback) {
  if (Send(new MediaMsg_EstablishChannel(render_id))) {
    channel_requests_.push(callback);
  } else {
    VLOG(1) << "Failed to send MediaMsg_EstablishChannel.";
    callback.Run(IPC::ChannelHandle());
  }
}

void MediaProcessHost::EstablishMediaChannel(
    int render_id,
    const EstablishChannelCallback& callback) {
  DCHECK(CalledOnValidThread());
  TRACE_EVENT0("media", "MediaProcessHost::EstablishMediaChannel");

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&MediaProcessHost::CreateResourceMessageFilter,
                 weak_factory_ui_.GetWeakPtr(), render_id, callback));
}

void MediaProcessHost::OnInitialized(bool result) {
  initialized_ = result;
  TRACE_EVENT0("media", "MediaProcessHost::OnInitialized");

  /* TODO: implement
    if (!initialized_)
      MediaDataManagerImpl::GetInstance()->OnMediaProcessInitFailure();
  */
}

void MediaProcessHost::OnChannelEstablished(
    const IPC::ChannelHandle& channel_handle) {
  TRACE_EVENT0("media", "MediaProcessHost::OnChannelEstablished");

  if (channel_requests_.empty()) {
    // This happens when media process is compromised.
    RouteOnUIThread(MediaHostMsg_OnLogMessage(
        logging::LOG_WARNING, "WARNING",
        "Received a ChannelEstablished message but no requests in queue."));
    return;
  }
  EstablishChannelCallback callback = channel_requests_.front();
  channel_requests_.pop();

  callback.Run(channel_handle);
}

void MediaProcessHost::OnProcessLaunched() {
  TRACE_EVENT0("media", "MediaProcessHost::OnProcessLaunched");
}

void MediaProcessHost::OnProcessCrashed(int exit_code) {
  TRACE_EVENT0("media", "MediaProcessHost::OnProcessCrashed");
  SendOutstandingReplies();
  MediaDataManagerImpl::GetInstance()->ProcessCrashed(
      process_->GetTerminationStatus(true /* known_dead */, NULL));
}

MediaProcessHost::MediaProcessKind MediaProcessHost::kind() {
  return kind_;
}

void MediaProcessHost::ForceShutdown() {
  // This is only called on the IO thread so no race against the constructor
  // for another MediaProcessHost.
  if (g_media_process_hosts[kind_] == this)
    g_media_process_hosts[kind_] = NULL;

  process_->ForceShutdown();
}

bool MediaProcessHost::LaunchMediaProcess(const std::string& channel_id) {
  const base::CommandLine& browser_command_line =
      *base::CommandLine::ForCurrentProcess();

  base::CommandLine::StringType media_launcher =
      browser_command_line.GetSwitchValueNative(switches::kMediaLauncher);

#if defined(OS_LINUX)
  int child_flags = media_launcher.empty() ? ChildProcessHost::CHILD_ALLOW_SELF
                                           : ChildProcessHost::CHILD_NORMAL;
#else
  int child_flags = ChildProcessHost::CHILD_NORMAL;
#endif

  base::FilePath exe_path = ChildProcessHost::GetChildPath(child_flags);
  if (exe_path.empty())
    return false;

  base::CommandLine* cmd_line = new base::CommandLine(exe_path);
  cmd_line->AppendSwitchASCII(switches::kProcessType, switches::kMediaProcess);
  cmd_line->AppendSwitchASCII(switches::kProcessChannelID, channel_id);

  if (kind_ == MEDIA_PROCESS_KIND_UNSANDBOXED)
    cmd_line->AppendSwitch(switches::kDisableMediaSandbox);

  // Propagate relevant command line switches.
  static const char* const kSwitchNames[] = {
      switches::kAllowSandboxDebugging,
      switches::kDisableMediaSandbox,
      switches::kDisableLogging,
      switches::kDisableSeccompFilterSandbox,
      switches::kEnableLogging,
      switches::kMediaStartupDialog,
      switches::kNoSandbox,
      switches::kV,
      switches::kVModule,
  };

  cmd_line->CopySwitchesFrom(browser_command_line, kSwitchNames,
                             arraysize(kSwitchNames));
  cmd_line->CopySwitchesFrom(browser_command_line, switches::kMediaSwitches,
                             switches::kNumMediaSwitches);

  GetContentClient()->browser()->AppendExtraCommandLineSwitches(
      cmd_line, process_->GetData().id);

  MediaDataManagerImpl::GetInstance()->AppendMediaCommandLine(cmd_line);

  // If specified, prepend a launcher program to the command line.
  if (!media_launcher.empty())
    cmd_line->PrependWrapper(media_launcher);

  process_->Launch(
      new MediaSandboxedProcessLauncherDelegate(cmd_line, process_->GetHost()),
      cmd_line, true);

  process_launched_ = true;
  return true;
}

void MediaProcessHost::SendOutstandingReplies() {
  valid_ = false;
  // First send empty channel handles for all EstablishChannel requests.
  while (!channel_requests_.empty()) {
    EstablishChannelCallback callback = channel_requests_.front();
    channel_requests_.pop();
    callback.Run(IPC::ChannelHandle());
  }
}

}  // namespace content
