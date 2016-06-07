// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_data_manager_impl_private.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "base/trace_event/trace_event.h"
#include "base/version.h"
#include "cc/base/switches.h"
#include "content/browser/media/media_process_host.h"
#include "content/common/media/media_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_data_manager_observer.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/web_preferences.h"

namespace content {

void MediaDataManagerImplPrivate::GetMediaProcessHandles(
    const MediaDataManager::GetMediaProcessHandlesCallback& callback) const {
  MediaProcessHost::GetProcessHandles(callback);
}

void MediaDataManagerImplPrivate::AddObserver(
    MediaDataManagerObserver* observer) {
  MediaDataManagerImpl::UnlockedSession session(owner_);
  observer_list_->AddObserver(observer);
}

void MediaDataManagerImplPrivate::RemoveObserver(
    MediaDataManagerObserver* observer) {
  MediaDataManagerImpl::UnlockedSession session(owner_);
  observer_list_->RemoveObserver(observer);
}

void MediaDataManagerImplPrivate::Initialize() {}

void MediaDataManagerImplPrivate::AppendRendererCommandLine(
    base::CommandLine* command_line) const {
  DCHECK(command_line);
  // command_line->AppendSwitch(switches::kXXXX);
}

void MediaDataManagerImplPrivate::AppendMediaCommandLine(
    base::CommandLine* command_line) const {
  DCHECK(command_line);
  // command_line->AppendSwitch(switches::kXXXX);
}

void MediaDataManagerImplPrivate::AddLogMessage(int level,
                                                const std::string& header,
                                                const std::string& message) {
  log_messages_.push_back(LogMessage(level, header, message));
}

void MediaDataManagerImplPrivate::ProcessCrashed(
    base::TerminationStatus exit_code) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    // Unretained is ok, because it's posted to UI thread, the thread
    // where the singleton GpuDataManagerImpl lives until the end.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&MediaDataManagerImpl::ProcessCrashed,
                                       base::Unretained(owner_), exit_code));
    return;
  }
}

base::ListValue* MediaDataManagerImplPrivate::GetLogMessages() const {
  base::ListValue* value = new base::ListValue;
  for (size_t ii = 0; ii < log_messages_.size(); ++ii) {
    base::DictionaryValue* dict = new base::DictionaryValue();
    dict->SetInteger("level", log_messages_[ii].level);
    dict->SetString("header", log_messages_[ii].header);
    dict->SetString("message", log_messages_[ii].message);
    value->Append(dict);
  }
  return value;
}

// static
MediaDataManagerImplPrivate* MediaDataManagerImplPrivate::Create(
    MediaDataManagerImpl* owner) {
  return new MediaDataManagerImplPrivate(owner);
}

MediaDataManagerImplPrivate::MediaDataManagerImplPrivate(
    MediaDataManagerImpl* owner)
    : observer_list_(new MediaDataManagerObserverList),
      owner_(owner) {
  DCHECK(owner_);
  /*
    const base::CommandLine* command_line =
        base::CommandLine::ForCurrentProcess();

    if (command_line->HasSwitch(switches::kXXXX))
      DoSomething();
  */
}

MediaDataManagerImplPrivate::~MediaDataManagerImplPrivate() {}

void MediaDataManagerImplPrivate::OnMediaProcessInitFailure() {
  // TODO: implement
}

}  // namespace content
