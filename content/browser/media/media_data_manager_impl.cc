// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_data_manager_impl.h"

#include "content/browser/media/media_data_manager_impl_private.h"

namespace content {

// static
MediaDataManager* MediaDataManager::GetInstance() {
  return MediaDataManagerImpl::GetInstance();
}

// static
MediaDataManagerImpl* MediaDataManagerImpl::GetInstance() {
  return base::Singleton<MediaDataManagerImpl>::get();
}

void MediaDataManagerImpl::GetMediaProcessHandles(
    const GetMediaProcessHandlesCallback& callback) const {
  base::AutoLock auto_lock(lock_);
  private_->GetMediaProcessHandles(callback);
}

void MediaDataManagerImpl::AddObserver(MediaDataManagerObserver* observer) {
  base::AutoLock auto_lock(lock_);
  private_->AddObserver(observer);
}

void MediaDataManagerImpl::RemoveObserver(MediaDataManagerObserver* observer) {
  base::AutoLock auto_lock(lock_);
  private_->RemoveObserver(observer);
}

void MediaDataManagerImpl::Initialize() {
  base::AutoLock auto_lock(lock_);
  private_->Initialize();
}

// TODO: check unused code
void MediaDataManagerImpl::AppendRendererCommandLine(
    base::CommandLine* command_line) const {
  base::AutoLock auto_lock(lock_);
  private_->AppendRendererCommandLine(command_line);
}

void MediaDataManagerImpl::AppendMediaCommandLine(
    base::CommandLine* command_line) const {
  base::AutoLock auto_lock(lock_);
  private_->AppendMediaCommandLine(command_line);
}

void MediaDataManagerImpl::AddLogMessage(int level,
                                         const std::string& header,
                                         const std::string& message) {
  base::AutoLock auto_lock(lock_);
  private_->AddLogMessage(level, header, message);
}

void MediaDataManagerImpl::ProcessCrashed(base::TerminationStatus exit_code) {
  base::AutoLock auto_lock(lock_);
  private_->ProcessCrashed(exit_code);
}

// TODO: check unused code
base::ListValue* MediaDataManagerImpl::GetLogMessages() const {
  base::AutoLock auto_lock(lock_);
  return private_->GetLogMessages();
}

MediaDataManagerImpl::MediaDataManagerImpl()
    : private_(MediaDataManagerImplPrivate::Create(this)) {}

MediaDataManagerImpl::~MediaDataManagerImpl() {}

}  // namespace content
