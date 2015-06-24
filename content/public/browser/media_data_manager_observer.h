// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_OBSERVER_H_
#define CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_OBSERVER_H_

#include "base/process/kill.h"
#include "content/common/content_export.h"

namespace content {

// Observers can register themselves via MediaDataManager::AddObserver, and
// can un-register with MediaDataManager::RemoveObserver.
class CONTENT_EXPORT MediaDataManagerObserver {
 public:
  // Called for any observer when the media process crashed.
  virtual void OnMediaProcessCrashed(base::TerminationStatus exit_code) {}

 protected:
  virtual ~MediaDataManagerObserver() {}
};

};  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_OBSERVER_H_
