// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_MESSAGE_FILTER_H_

#include <vector>

#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner_helpers.h"
#include "content/common/media/media_process_launch_causes.h"
#include "content/public/browser/browser_message_filter.h"

class MediaProcessHost;

namespace content {
class RenderWidgetHelper;
class RenderWidgetHostViewFrameSubscriber;

// A message filter for messages from the renderer to the
// MediaProcessHost(UIShim)
// in the browser. Such messages are typically destined for the media process,
// but need to be mediated by the browser.
class MediaMessageFilter : public BrowserMessageFilter {
 public:
  MediaMessageFilter(int render_process_id);

  // BrowserMessageFilter methods:
  bool OnMessageReceived(const IPC::Message& message) override;

 private:
  friend class BrowserThread;
  friend class base::DeleteHelper<MediaMessageFilter>;

  ~MediaMessageFilter() override;

  // Message handlers called on the browser IO thread:
  void OnEstablishMediaChannel(CauseForMediaLaunch, IPC::Message* reply);

  // Helper callbacks for the message handlers.
  void EstablishChannelCallback(scoped_ptr<IPC::Message> reply,
                                const IPC::ChannelHandle& channel);

  int media_process_id_;
  int render_process_id_;

  base::WeakPtrFactory<MediaMessageFilter> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaMessageFilter);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_MESSAGE_FILTER_H_
