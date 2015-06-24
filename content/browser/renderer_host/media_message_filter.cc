// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media_message_filter.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "content/browser/media/media_process_host.h"
#include "content/common/media/media_messages.h"
#include "content/public/common/content_switches.h"

namespace content {

MediaMessageFilter::MediaMessageFilter(int render_process_id)
    : BrowserMessageFilter(MediaMsgStart),
      media_process_id_(0),  // There is only one media process.
      render_process_id_(render_process_id),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  printf("%s:%d\n", __PRETTY_FUNCTION__, __LINE__);
}

MediaMessageFilter::~MediaMessageFilter() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
}

bool MediaMessageFilter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MediaMessageFilter, message)
  IPC_MESSAGE_HANDLER_DELAY_REPLY(MediaHostMsg_EstablishMediaChannel,
                                  OnEstablishMediaChannel)
  IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MediaMessageFilter::OnEstablishMediaChannel(
    CauseForMediaLaunch cause_for_media_launch,
    IPC::Message* reply_ptr) {
  printf("%s:%d\n", __PRETTY_FUNCTION__, __LINE__);
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  scoped_ptr<IPC::Message> reply(reply_ptr);

  MediaProcessHost* host = MediaProcessHost::FromID(media_process_id_);
  if (!host) {
    host = MediaProcessHost::Get(MediaProcessHost::MEDIA_PROCESS_KIND_SANDBOXED,
                                 cause_for_media_launch);
    if (!host) {
      reply->set_reply_error();
      Send(reply.release());
      return;
    }

    media_process_id_ = host->host_id();
  }

  printf("%s:%d(%d)\n", __PRETTY_FUNCTION__, __LINE__, media_process_id_);

  host->EstablishMediaChannel(
      render_process_id_,
      base::Bind(&MediaMessageFilter::EstablishChannelCallback,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&reply)));
}

void MediaMessageFilter::EstablishChannelCallback(
    scoped_ptr<IPC::Message> reply,
    const IPC::ChannelHandle& channel) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  printf("%s:%d\n", __PRETTY_FUNCTION__, __LINE__);
  MediaHostMsg_EstablishMediaChannel::WriteReplyParams(
      reply.get(), render_process_id_, channel);
  Send(reply.release());
  printf("%s:%d\n", __PRETTY_FUNCTION__, __LINE__);
}

}  // namespace content
