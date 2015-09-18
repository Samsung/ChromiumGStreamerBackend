// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_COMMON_ENCRYPTION_DECRYPTOR_H_
#define CONTENT_MEDIA_GSTREAMER_COMMON_ENCRYPTION_DECRYPTOR_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/base.h>

#include <string>

G_BEGIN_DECLS

#define CHROMIUM_TYPE_MEDIA_CENC_DECRYPT \
  (chromium_common_encryption_decrypt_get_type())
#define CHROMIUM_MEDIA_CENC_DECRYPT(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), CHROMIUM_TYPE_MEDIA_CENC_DECRYPT, \
                              ChromiumCommonEncryptionDecrypt))
#define CHROMIUM_MEDIA_CENC_DECRYPT_CLASS(klass)                      \
  (G_TYPE_CHECK_CLASS_CAST((klass), CHROMIUM_TYPE_MEDIA_CENC_DECRYPT, \
                           ChromiumCommonEncryptionDecryptClass))
#define CHROMIUM_IS_MEDIA_CENC_DECRYPT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), CHROMIUM_TYPE_MEDIA_CENC_DECRYPT))
#define CHROMIUM_IS_MEDIA_CENC_DECRYPT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), CHROMIUM_TYPE_MEDIA_CENC_DECRYPT))

typedef struct _ChromiumCommonEncryptionDecrypt ChromiumCommonEncryptionDecrypt;
typedef struct _ChromiumCommonEncryptionDecryptClass
    ChromiumCommonEncryptionDecryptClass;

GType chromium_common_encryption_decrypt_get_type(void);
void chromiumCommonEncryptionDecryptAddKey(GstElement* pipeline,
                                           const std::string& key);

G_END_DECLS

#endif  // CONTENT_MEDIA_GSTREAMER_COMMON_ENCRYPTION_DECRYPTOR_H_
