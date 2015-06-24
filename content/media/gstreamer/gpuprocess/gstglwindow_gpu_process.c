// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "content/media/gstreamer/gpuprocess/gstglwindow_gpu_process.h"

#define GST_GL_WINDOW_GPU_PROCESS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_GPU_PROCESS, GstGLWindowGPUProcessPrivate))

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_gpu_process_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowGPUProcess, gst_gl_window_gpu_process,
    GST_GL_TYPE_WINDOW);

struct _GstGLWindowGPUProcessPrivate
{
  int empty;
};

static void
gst_gl_window_gpu_process_class_init (GstGLWindowGPUProcessClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLWindowGPUProcessPrivate));
}

static void
gst_gl_window_gpu_process_init (GstGLWindowGPUProcess * window)
{
  window->priv = GST_GL_WINDOW_GPU_PROCESS_GET_PRIVATE (window);
}

GstGLWindowGPUProcess *
gst_gl_window_gpu_process_new (GstGLDisplay * display)
{
  GstGLWindowGPUProcess *window =
      g_object_new (GST_GL_TYPE_WINDOW_GPU_PROCESS, NULL);

  GST_GL_WINDOW (window)->display = gst_object_ref (display);

  return window;
}
