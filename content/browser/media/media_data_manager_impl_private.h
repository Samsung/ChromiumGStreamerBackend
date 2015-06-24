// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_MEDIA_DATA_MANAGER_IMPL_PRIVATE_H_
#define CONTENT_BROWSER_MEDIA_MEDIA_DATA_MANAGER_IMPL_PRIVATE_H_

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/singleton.h"
#include "base/observer_list_threadsafe.h"
#include "content/common/content_export.h"
#include "content/public/browser/media_data_manager.h"
#include "content/public/browser/media_data_manager_observer.h"
#include "content/browser/media/media_data_manager_impl.h"
#include "content/browser/media/media_data_manager_impl_private.h"

namespace base {
class CommandLine;
}

namespace content {

class CONTENT_EXPORT MediaDataManagerImplPrivate {
 public:
  static MediaDataManagerImplPrivate* Create(MediaDataManagerImpl* owner);

  void GetMediaProcessHandles(
      const MediaDataManager::GetMediaProcessHandlesCallback& callback) const;
  void AddObserver(MediaDataManagerObserver* observer);
  void RemoveObserver(MediaDataManagerObserver* observer);

  void Initialize();
  void AppendRendererCommandLine(base::CommandLine* command_line) const;
  void AppendMediaCommandLine(base::CommandLine* command_line) const;

  void AddLogMessage(int level,
                     const std::string& header,
                     const std::string& message);

  void ProcessCrashed(base::TerminationStatus exit_code);

  base::ListValue* GetLogMessages() const;
  void OnMediaProcessInitFailure();

  virtual ~MediaDataManagerImplPrivate();

 private:
  typedef base::ObserverListThreadSafe<MediaDataManagerObserver>
      MediaDataManagerObserverList;

  struct LogMessage {
    int level;
    std::string header;
    std::string message;

    LogMessage(int _level,
               const std::string& _header,
               const std::string& _message)
        : level(_level), header(_header), message(_message) {}
  };

  explicit MediaDataManagerImplPrivate(MediaDataManagerImpl* owner);
  const scoped_refptr<MediaDataManagerObserverList> observer_list_;

  std::vector<LogMessage> log_messages_;

  MediaDataManagerImpl* owner_;

  // True if all future Initialize calls should be ignored.
  bool finalized_;

  DISALLOW_COPY_AND_ASSIGN(MediaDataManagerImplPrivate);
};

}  // namespace content

#endif  // CONTENT_BROWSER_GPU_GPU_DATA_MANAGER_IMPL_PRIVATE_H_
