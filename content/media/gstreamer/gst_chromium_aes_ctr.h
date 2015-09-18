// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_AES_H_
#define CONTENT_MEDIA_GSTREAMER_AES_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _AesCtrState AesCtrState;

AesCtrState* chromium_aes_ctr_decrypt_new(GBytes* key, GBytes* iv);
AesCtrState* chromium_aes_ctr_decrypt_ref(AesCtrState* state);
void chromium_aes_ctr_decrypt_unref(AesCtrState* state);

gboolean chromium_aes_ctr_decrypt_ip(AesCtrState* state,
                                     unsigned char* data,
                                     int length);

G_END_DECLS

#endif  // CONTENT_MEDIA_GSTREAMER_AES_H_
