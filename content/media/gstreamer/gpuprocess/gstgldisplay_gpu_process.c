// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "content/media/gstreamer/gpuprocess/gstgldisplay_gpu_process.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

G_DEFINE_TYPE (GstGLDisplayGPUProcess, gst_gl_display_gpu_process, GST_TYPE_GL_DISPLAY);

static void gst_gl_display_gpu_process_finalize (GObject * object);

static void
gst_gl_display_gpu_process_class_init (GstGLDisplayGPUProcessClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_gpu_process_finalize;
}

static void
gst_gl_display_gpu_process_init (GstGLDisplayGPUProcess * display_gpu_process)
{
  GstGLDisplay *display = (GstGLDisplay *) display_gpu_process;

  display->type = GST_GL_DISPLAY_TYPE_EGL;
}

static void
gst_gl_display_gpu_process_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_gl_display_gpu_process_parent_class)->finalize (object);
}

GstGLDisplayGPUProcess *
gst_gl_display_gpu_process_new (void)
{
  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  return g_object_new (GST_TYPE_GL_DISPLAY_GPU_PROCESS, NULL);
}
