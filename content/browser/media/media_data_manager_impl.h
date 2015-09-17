// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_MEDIA_DATA_MANAGER_IMPL_H_
#define CONTENT_BROWSER_MEDIA_MEDIA_DATA_MANAGER_IMPL_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/process/kill.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/media_data_manager.h"

namespace base {
class CommandLine;
}

namespace content {

class MediaDataManagerImplPrivate;
struct WebPreferences;

class CONTENT_EXPORT MediaDataManagerImpl
    : public NON_EXPORTED_BASE(MediaDataManager) {
 public:
  // Getter for the singleton. This will return NULL on failure.
  static MediaDataManagerImpl* GetInstance();

  // MediaDataManager implementation.
  void GetMediaProcessHandles(
      const GetMediaProcessHandlesCallback& callback) const override;

  // TODO: useful for stats but unused for now.
  void AddObserver(MediaDataManagerObserver* observer) override;
  void RemoveObserver(MediaDataManagerObserver* observer) override;

  void Initialize();

  // Insert disable-feature switches corresponding to preliminary media feature
  // flags into the renderer process command line.
  void AppendRendererCommandLine(base::CommandLine* command_line) const;

  // Insert switches into media process command line: XXXX, etc.
  void AppendMediaCommandLine(base::CommandLine* command_line) const;

  void AddLogMessage(int level,
                     const std::string& header,
                     const std::string& message);

  void ProcessCrashed(base::TerminationStatus exit_code);

  // Returns a new copy of the ListValue.  Caller is responsible to release
  // the returned value.
  base::ListValue* GetLogMessages() const;

 private:
  friend class MediaDataManagerImplPrivate;
  friend struct base::DefaultSingletonTraits<MediaDataManagerImpl>;

  class UnlockedSession {
   public:
    explicit UnlockedSession(MediaDataManagerImpl* owner) : owner_(owner) {
      DCHECK(owner_);
      owner_->lock_.AssertAcquired();
      owner_->lock_.Release();
    }

    ~UnlockedSession() {
      DCHECK(owner_);
      owner_->lock_.Acquire();
    }

   private:
    MediaDataManagerImpl* owner_;
    DISALLOW_COPY_AND_ASSIGN(UnlockedSession);
  };

  MediaDataManagerImpl();
  ~MediaDataManagerImpl() override;

  mutable base::Lock lock_;
  scoped_ptr<MediaDataManagerImplPrivate> private_;

  DISALLOW_COPY_AND_ASSIGN(MediaDataManagerImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_MEDIA_DATA_MANAGER_IMPL_H_
