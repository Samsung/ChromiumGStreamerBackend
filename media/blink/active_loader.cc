// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/blink/active_loader.h"

#include <utility>

#include "third_party/WebKit/public/web/WebAssociatedURLLoader.h"

#if defined(USE_GSTREAMER)
#include "third_party/WebKit/public/platform/WebURLLoader.h"
#endif

namespace media {

ActiveLoader::ActiveLoader(
    std::unique_ptr<blink::WebAssociatedURLLoader> loader)
    : loader_(std::move(loader)), deferred_(false) {}


#if defined(USE_GSTREAMER)
ActiveLoader::ActiveLoader(
    std::unique_ptr<blink::WebURLLoader> loader)
    : loader_(nullptr), deferred_(false), url_loader_(std::move(loader)) {}
#endif

ActiveLoader::~ActiveLoader() {
#if defined(USE_GSTREAMER)
  if (loader_)
    loader_->cancel();
  if (url_loader_)
    url_loader_->cancel();
#else
  loader_->cancel();
#endif
}

void ActiveLoader::SetDeferred(bool deferred) {
  deferred_ = deferred;
#if defined(USE_GSTREAMER)
  if (loader_)
    loader_->setDefersLoading(deferred);
  if (url_loader_)
    url_loader_->setDefersLoading(deferred);
#else
  loader_->setDefersLoading(deferred);
#endif
}

}  // namespace media
