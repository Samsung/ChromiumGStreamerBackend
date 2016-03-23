// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/decryptor.h"

namespace media {

Decryptor::Decryptor() {}

Decryptor::~Decryptor() {}

#if defined(USE_GSTREAMER)
void Decryptor::EnableDecryptionProxy(const KeysChangeCB&) {}
bool Decryptor::IsDecryptorProxy() { return false; }
#endif

}  // namespace media
