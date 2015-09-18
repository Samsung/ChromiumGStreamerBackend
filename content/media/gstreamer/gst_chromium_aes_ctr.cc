// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/media/gstreamer/gst_chromium_aes_ctr.h"

#include <openssl/aes.h>
#include <string.h>
#include <gst/gst.h>

struct _AesCtrState {
  volatile gint refcount;
  AES_KEY key;
  unsigned char ivec[16];
  unsigned int num;
  unsigned char ecount[16];
};

AesCtrState* chromium_aes_ctr_decrypt_new(GBytes* key, GBytes* iv) {
  unsigned char* buf;
  gsize iv_length;
  AesCtrState* state;

  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(iv != NULL, NULL);

  state = g_slice_new(AesCtrState);
  if (!state) {
    GST_ERROR("Failed to allocate AesCtrState");
    return NULL;
  }

  g_assert(g_bytes_get_size(key) == 16);
  AES_set_encrypt_key((const unsigned char*)g_bytes_get_data(key, NULL),
                      8 * g_bytes_get_size(key), &state->key);

  buf = (unsigned char*)g_bytes_get_data(iv, &iv_length);
  state->num = 0;
  memset(state->ecount, 0, 16);
  if (iv_length == 8) {
    memset(state->ivec + 8, 0, 8);
    memcpy(state->ivec, buf, 8);
  } else {
    memcpy(state->ivec, buf, 16);
  }

  return state;
}

AesCtrState* chromium_aes_ctr_decrypt_ref(AesCtrState* state) {
  g_return_val_if_fail(state != NULL, NULL);

  g_atomic_int_inc(&state->refcount);

  return state;
}

void chromium_aes_ctr_decrypt_unref(AesCtrState* state) {
  g_return_if_fail(state != NULL);

  if (g_atomic_int_dec_and_test(&state->refcount)) {
    g_slice_free(AesCtrState, state);
  }
}

gboolean chromium_aes_ctr_decrypt_ip(AesCtrState* state,
                                     unsigned char* data,
                                     int length) {
  AES_ctr128_encrypt(data, data, length, &state->key, state->ivec,
                     state->ecount, &state->num);
  return TRUE;
}

G_DEFINE_BOXED_TYPE(AesCtrState,
                    chromium_aes_ctr,
                    (GBoxedCopyFunc)chromium_aes_ctr_decrypt_ref,
                    (GBoxedFreeFunc)chromium_aes_ctr_decrypt_unref);
