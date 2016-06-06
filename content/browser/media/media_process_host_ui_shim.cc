// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_process_host_ui_shim.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/id_map.h"
#include "base/lazy_instance.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/media/media_data_manager_impl.h"
#include "content/browser/media/media_process_host.h"
#include "content/common/media/media_messages.h"
#include "content/public/browser/browser_thread.h"

namespace content {

namespace {

// One of the linux specific headers defines this as a macro.
#ifdef DestroyAll
#undef DestroyAll
#endif

base::LazyInstance<IDMap<MediaProcessHostUIShim>> g_hosts_by_id =
    LAZY_INSTANCE_INITIALIZER;

void SendOnIOThreadTask(int host_id, IPC::Message* msg) {
  MediaProcessHost* host = MediaProcessHost::FromID(host_id);
  if (host)
    host->Send(msg);
  else
    delete msg;
}

}  // namespace

void RouteToMediaProcessHostUIShimTask(int host_id, const IPC::Message& msg) {
  MediaProcessHostUIShim* ui_shim = MediaProcessHostUIShim::FromID(host_id);
  if (ui_shim)
    ui_shim->OnMessageReceived(msg);
}

MediaProcessHostUIShim::MediaProcessHostUIShim(int host_id)
    : host_id_(host_id) {
  g_hosts_by_id.Pointer()->AddWithID(this, host_id_);
}

// static
MediaProcessHostUIShim* MediaProcessHostUIShim::Create(int host_id) {
  DCHECK(!FromID(host_id));
  return new MediaProcessHostUIShim(host_id);
}

// static
void MediaProcessHostUIShim::Destroy(int host_id, const std::string& message) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  MediaDataManagerImpl::GetInstance()->AddLogMessage(
      logging::LOG_ERROR, "MediaProcessHostUIShim", message);

  delete FromID(host_id);
}

// static
void MediaProcessHostUIShim::DestroyAll() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  while (!g_hosts_by_id.Pointer()->IsEmpty()) {
    IDMap<MediaProcessHostUIShim>::iterator it(g_hosts_by_id.Pointer());
    delete it.GetCurrentValue();
  }
}

// static
MediaProcessHostUIShim* MediaProcessHostUIShim::FromID(int host_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return g_hosts_by_id.Pointer()->Lookup(host_id);
}

// static
MediaProcessHostUIShim* MediaProcessHostUIShim::GetOneInstance() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (g_hosts_by_id.Pointer()->IsEmpty())
    return NULL;
  IDMap<MediaProcessHostUIShim>::iterator it(g_hosts_by_id.Pointer());
  return it.GetCurrentValue();
}

bool MediaProcessHostUIShim::Send(IPC::Message* msg) {
  DCHECK(CalledOnValidThread());
  return BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SendOnIOThreadTask, host_id_, msg));
}

bool MediaProcessHostUIShim::OnMessageReceived(const IPC::Message& message) {
  DCHECK(CalledOnValidThread());

  if (message.routing_id() != MSG_ROUTING_CONTROL)
    return false;

  return OnControlMessageReceived(message);
}

void MediaProcessHostUIShim::SimulateClean() {
  Send(new MediaMsg_Clean());
}

void MediaProcessHostUIShim::SimulateCrash() {
  Send(new MediaMsg_Crash());
}

void MediaProcessHostUIShim::SimulateHang() {
  Send(new MediaMsg_Hang());
}

MediaProcessHostUIShim::~MediaProcessHostUIShim() {
  DCHECK(CalledOnValidThread());
  g_hosts_by_id.Pointer()->Remove(host_id_);
}

bool MediaProcessHostUIShim::OnControlMessageReceived(
    const IPC::Message& message) {
  DCHECK(CalledOnValidThread());

  IPC_BEGIN_MESSAGE_MAP(MediaProcessHostUIShim, message)
    IPC_MESSAGE_HANDLER(MediaHostMsg_OnLogMessage, OnLogMessage)

    IPC_MESSAGE_UNHANDLED_ERROR()
  IPC_END_MESSAGE_MAP()

  return true;
}

void MediaProcessHostUIShim::OnLogMessage(int level,
                                          const std::string& header,
                                          const std::string& message) {
  MediaDataManagerImpl::GetInstance()->AddLogMessage(level, header, message);
}

}  // namespace content
