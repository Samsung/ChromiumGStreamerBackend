// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BLINK_ACTIVE_LOADER_H_
#define MEDIA_BLINK_ACTIVE_LOADER_H_

#include <memory>

#include "base/macros.h"
#include "media/blink/media_blink_export.h"

namespace blink {
class WebAssociatedURLLoader;

#if defined(USE_GSTREAMER)
class WebURLLoader;
#endif

}

namespace media {

// Wraps an active WebURLLoader with some additional state.
//
// Handles deferring and deletion of loaders.
class MEDIA_BLINK_EXPORT ActiveLoader {
 public:
  // Creates an ActiveLoader with the given loader. It is assumed that the
  // initial state of |loader| is loading and not deferred.
  explicit ActiveLoader(std::unique_ptr<blink::WebAssociatedURLLoader> loader);

#if defined(USE_GSTREAMER)
  explicit ActiveLoader(std::unique_ptr<blink::WebURLLoader> loader);
#endif

  ~ActiveLoader();

  // Starts or stops deferring the resource load.
  void SetDeferred(bool deferred);
  bool deferred() { return deferred_; }

 private:
  friend class MultibufferDataSourceTest;

  std::unique_ptr<blink::WebAssociatedURLLoader> loader_;
  bool deferred_;

#if defined(USE_GSTREAMER)
  std::unique_ptr<blink::WebURLLoader> url_loader_;
#endif

  DISALLOW_IMPLICIT_CONSTRUCTORS(ActiveLoader);
};

}  // namespace media

#endif  // MEDIA_BLINK_ACTIVE_LOADER_H_
