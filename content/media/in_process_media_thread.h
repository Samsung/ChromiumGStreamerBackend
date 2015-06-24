// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_IN_PROCESS_MEDIA_THREAD_H_
#define CONTENT_MEDIA_IN_PROCESS_MEDIA_THREAD_H_

#include "base/threading/thread.h"
#include "content/common/content_export.h"
#include "content/common/content_export.h"
#include "content/common/in_process_child_thread_params.h"

namespace content {

class MediaProcess;

// This class creates a media thread (instead of a media process), when running
// with --in-process-media or --single-process.
class InProcessMediaThread : public base::Thread {
 public:
  InProcessMediaThread(const InProcessChildThreadParams& params);
  ~InProcessMediaThread() override;

 protected:
  void Init() override;
  void CleanUp() override;

 private:
  InProcessChildThreadParams params_;

  // Deleted in CleanUp() on the media thread, so don't use smart pointers.
  MediaProcess* media_process_;

  DISALLOW_COPY_AND_ASSIGN(InProcessMediaThread);
};

CONTENT_EXPORT base::Thread* CreateInProcessMediaThread(
    const InProcessChildThreadParams& params);

}  // namespace content

#endif  // CONTENT_MEDIA_IN_PROCESS_MEDIA_THREAD_H_
