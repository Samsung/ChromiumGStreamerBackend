// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FrameConsumerProxy is used to allow a FrameConsumer on the UI thread to be
// invoked by a Decoder on the decoder thread.  The Detach() method is used by
// the proxy's owner before tearing down the FrameConsumer, to prevent any
// further invokations reaching it.

#ifndef REMOTING_CLIENT_FRAME_CONSUMER_PROXY_H_
#define REMOTING_CLIENT_FRAME_CONSUMER_PROXY_H_

#include "base/memory/weak_ptr.h"
#include "remoting/client/frame_consumer.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace remoting {

class FrameConsumerProxy : public FrameConsumer {
 public:
  // Constructs a FrameConsumer proxy which can be passed to another thread,
  // and will direct calls to |frame_consumer| on the thread from which the
  // proxy was constructed.
  FrameConsumerProxy(const base::WeakPtr<FrameConsumer>& frame_consumer);
  ~FrameConsumerProxy() override;

  // FrameConsumer implementation.
  void ApplyBuffer(const webrtc::DesktopSize& view_size,
                   const webrtc::DesktopRect& clip_area,
                   webrtc::DesktopFrame* buffer,
                   const webrtc::DesktopRegion& region,
                   const webrtc::DesktopRegion* shape) override;
  void ReturnBuffer(webrtc::DesktopFrame* buffer) override;
  void SetSourceSize(const webrtc::DesktopSize& source_size,
                     const webrtc::DesktopVector& dpi) override;
  PixelFormat GetPixelFormat() override;

 private:
  base::WeakPtr<FrameConsumer> frame_consumer_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  PixelFormat pixel_format_;

  DISALLOW_COPY_AND_ASSIGN(FrameConsumerProxy);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_FRAME_CONSUMER_PROXY_H_
