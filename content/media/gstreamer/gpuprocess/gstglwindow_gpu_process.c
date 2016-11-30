// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "content/media/gstreamer/gpuprocess/gstglwindow_gpu_process.h"

#define GST_GL_WINDOW_GPU_PROCESS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_WINDOW_GPU_PROCESS, GstGLWindowGPUProcessPrivate))

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_gpu_process_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowGPUProcess, gst_gl_window_gpu_process,
    GST_TYPE_GL_WINDOW);

struct _GstGLWindowGPUProcessPrivate
{
  gint empty;
};

typedef struct _GstGLSyncMessage
{
  GstGLWindow *window;
  GstGLWindowCB callback;
  gpointer data;
} GstGLSyncMessage;

static void
gst_gl_window_gpu_process_callback (gpointer data)
{
  GstGLSyncMessage* msg = (GstGLSyncMessage*) data;

  GstGLContext *context = gst_gl_window_get_context (msg->window);
  if (context) {
    gst_gl_context_activate (context, TRUE);
    gst_object_unref (context);
  }
  if (msg->callback)
    msg->callback (msg->data);
}

static void
gst_gl_window_gpu_process_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data)
{
  GstGLSyncMessage msg;

  msg.window = window;
  msg.callback = callback;
  msg.data = data;

  GST_GL_WINDOW_CLASS (parent_class)->send_message (window,
      gst_gl_window_gpu_process_callback, &msg);
}

static void
gst_gl_window_gpu_process_class_init (GstGLWindowGPUProcessClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowGPUProcessPrivate));

  window_class->send_message = GST_DEBUG_FUNCPTR (
      gst_gl_window_gpu_process_send_message);
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
      g_object_new (GST_TYPE_GL_WINDOW_GPU_PROCESS, NULL);

  GST_GL_WINDOW (window)->display = gst_object_ref (display);

  return window;
}
