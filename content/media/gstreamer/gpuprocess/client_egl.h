// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_MEDIA_GSTREAMER_GPUPROCESS_CLIENT_EGL_H_
#define CONTENT_MEDIA_GSTREAMER_GPUPROCESS_CLIENT_EGL_H_

namespace content {

typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef void* EGLDisplay;
typedef void* EGLContext;
typedef void* EGLClientBuffer;
typedef int EGLint;
typedef void* EGLImageKHR;

EGLImageKHR CreateEGLImageKHR(EGLDisplay dpy,
                              EGLContext ctx,
                              EGLenum target,
                              EGLClientBuffer buffer,
                              const EGLint* attrib_list);

EGLBoolean DestroyEGLImageKHR(EGLDisplay dpy, EGLImageKHR img);

}  // namespace content

#endif  // CONTENT_MEDIA_GSTREAMER_GPUPROCESS_CLIENT_EGL_H_
