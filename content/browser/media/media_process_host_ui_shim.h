// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_UI_SHIM_H_
#define CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_UI_SHIM_H_

// This class lives on the UI thread and supports classes
// which must live on the UI thread. The IO thread
// portion of this class, the MediaProcessHost, is responsible for
// shuttling messages between the browser and media processes.

#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "content/common/content_export.h"
#include "content/common/message_router.h"
#include "ipc/ipc_listener.h"
#include "ipc/ipc_sender.h"

namespace IPC {
class Message;
}

namespace content {
void RouteToMediaProcessHostUIShimTask(int host_id, const IPC::Message& msg);

class MediaProcessHostUIShim : public IPC::Listener,
                               public IPC::Sender,
                               public base::NonThreadSafe {
 public:
  // Create a MediaProcessHostUIShim with the given ID.  The object can be found
  // using FromID with the same id.
  static MediaProcessHostUIShim* Create(int host_id);

  // Destroy the MediaProcessHostUIShim with the given host ID. This can only
  // be called on the UI thread. Only the GpuProcessHost should destroy the
  // UI shim.
  static void Destroy(int host_id, const std::string& message);

  // Destroy all remaining MediaProcessHostUIShims.
  CONTENT_EXPORT static void DestroyAll();

  CONTENT_EXPORT static MediaProcessHostUIShim* FromID(int host_id);

  // Get a MediaProcessHostUIShim instance; it doesn't matter which one.
  // Return NULL if none has been created.
  CONTENT_EXPORT static MediaProcessHostUIShim* GetOneInstance();

  // IPC::Sender implementation.
  bool Send(IPC::Message* msg) override;

  // IPC::Listener implementation.
  // The GpuProcessHost causes this to be called on the UI thread to
  // dispatch the incoming messages from the GPU process, which are
  // actually received on the IO thread.
  bool OnMessageReceived(const IPC::Message& message) override;

  CONTENT_EXPORT void SimulateClean();
  CONTENT_EXPORT void SimulateCrash();
  CONTENT_EXPORT void SimulateHang();

 private:
  explicit MediaProcessHostUIShim(int host_id);
  ~MediaProcessHostUIShim() override;

  // Message handlers.
  bool OnControlMessageReceived(const IPC::Message& message);

  void OnLogMessage(int level,
                    const std::string& header,
                    const std::string& message);

  // The serial number of the MediaProcessHost / MediaProcessHostUIShim pair.
  int host_id_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_MEDIA_PROCESS_HOST_UI_SHIM_H_
