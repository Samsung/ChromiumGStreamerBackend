// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_H_
#define CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_H_

#include <list>
#include <string>

#include "base/callback_forward.h"
#include "base/process/process.h"
#include "content/common/content_export.h"

namespace content {

class MediaDataManagerObserver;

// This class is fully thread-safe.
class MediaDataManager {
 public:
  typedef base::Callback<void(const std::list<base::ProcessHandle>&)>
      GetMediaProcessHandlesCallback;

  // Getter for the singleton.
  CONTENT_EXPORT static MediaDataManager* GetInstance();

  // Retrieves a list of process handles for all media processes.
  virtual void GetMediaProcessHandles(
      const GetMediaProcessHandlesCallback& callback) const = 0;

  // Registers/unregister |observer|.
  // TODO: useful for stats but unused for now.
  virtual void AddObserver(MediaDataManagerObserver* observer) = 0;
  virtual void RemoveObserver(MediaDataManagerObserver* observer) = 0;

 protected:
  virtual ~MediaDataManager() {}
};

};  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_MEDIA_DATA_MANAGER_H_
