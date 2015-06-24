// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/in_process_media_thread.h"

#include "content/media/media_child_thread.h"
#include "content/media/media_process.h"

namespace content {

InProcessMediaThread::InProcessMediaThread(
    const InProcessChildThreadParams& params)
    : base::Thread("Chrome_InProcMediaThread"),
      params_(params),
      media_process_(NULL) {
}

InProcessMediaThread::~InProcessMediaThread() {
  Stop();
}

void InProcessMediaThread::Init() {
  media_process_ = new MediaProcess();
  // The process object takes ownership of the thread object, so do not
  // save and delete the pointer.
  media_process_->set_main_thread(new MediaChildThread(params_));
}

void InProcessMediaThread::CleanUp() {
  SetThreadWasQuitProperly(true);
  delete media_process_;
}

base::Thread* CreateInProcessMediaThread(
    const InProcessChildThreadParams& params) {
  return new InProcessMediaThread(params);
}

}  // namespace content
