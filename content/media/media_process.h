// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_MEDIA_PROCESS_H_
#define CONTENT_MEDIA_MEDIA_PROCESS_H_

#include "content/child/child_process.h"

namespace content {

class MediaProcess : public ChildProcess {
 public:
  MediaProcess();
  ~MediaProcess() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MediaProcess);
};
}

#endif  // CONTENT_MEDIA_MEDIA_PROCESS_H_
