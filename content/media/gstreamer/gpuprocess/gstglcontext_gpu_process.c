// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "content/media/gstreamer/gpuprocess/gstglcontext_gpu_process.h"
#include "content/media/gstreamer/gpuprocess/gstglwindow_gpu_process.h"

#define GST_GL_CONTEXT_GPU_PROCESS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, GstGLContextGPUProcessPrivate))

#define GST_CAT_DEFAULT gst_gl_context_debug

#define gst_gl_context_gpu_process_parent_class parent_class
G_DEFINE_TYPE (GstGLContextGPUProcess, gst_gl_context_gpu_process,
    GST_GL_TYPE_CONTEXT_EGL);

struct _GstGLContextGPUProcessPrivate
{
  // Nothing for now;
  gint empty;
};

static guintptr
gst_gl_context_gpu_process_get_gl_context (GstGLContext * context)
{
  return 0;
}

static GstGLAPI
gst_gl_context_gpu_process_get_gl_api (GstGLContext * context)
{
  return GST_GL_CONTEXT_EGL (context)->gl_api;
}

static GstGLPlatform
gst_gl_context_gpu_process_get_gl_platform (GstGLContext * context)
{
  return 0;
}

static gboolean
gst_gl_context_gpu_process_activate (GstGLContext * context, gboolean activate)
{
  return TRUE;
}

static void
gst_gl_context_gpu_process_finalize (GObject * object)
{
  GstGLContext *context = GST_GL_CONTEXT (object);

  if (context->window)
    GST_GL_WINDOW_GET_CLASS (context->window)->close (context->window);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_context_gpu_process_class_init (GstGLContextGPUProcessClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLContextGPUProcessPrivate));

  obj_class->finalize = gst_gl_context_gpu_process_finalize;

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_context);
  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_get_gl_platform);
  context_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_context_gpu_process_activate);
}

static void
gst_gl_context_gpu_process_init (GstGLContextGPUProcess * context)
{
  context->priv = GST_GL_CONTEXT_GPU_PROCESS_GET_PRIVATE (context);
}

GstGLContext *
gst_gl_context_gpu_process_new (GstGLDisplay * display,
    GstGLAPI gl_api, GstGLProcAddrFunc proc_addr)
{
  GstGLContext *context = NULL;
  GstGLContextEGL *egl_context = NULL;
  GstGLContextGPUProcess *gpu_context = NULL;
  GstGLContextClass *context_class = NULL;
  GstGLWindow *window = NULL;
  g_return_val_if_fail ((gst_gl_display_get_gl_api (display) & gl_api) !=
      GST_GL_API_NONE, NULL);

  gpu_context = g_object_new (GST_GL_TYPE_CONTEXT_GPU_PROCESS, NULL);

  egl_context = GST_GL_CONTEXT_EGL (gpu_context);
  egl_context->gl_api = gl_api;

  context = GST_GL_CONTEXT (gpu_context);

  context->display = display;

  context_class = GST_GL_CONTEXT_GET_CLASS (context);

  context_class->get_current_context = NULL;
  context_class->get_proc_address = GST_DEBUG_FUNCPTR (proc_addr);

  egl_context->egl_major = 1;
  egl_context->egl_minor = 4;
  egl_context->egl_exts = "EGL_EXT_image_dma_buf_import EGL_KHR_image_base";

  egl_context->eglCreateImageKHR = gst_gl_context_get_proc_address (context,
    "eglCreateImageKHR");
  egl_context->eglDestroyImage = gst_gl_context_get_proc_address (context,
    "eglDestroyImage");

  window = GST_GL_WINDOW (gst_gl_window_gpu_process_new (display));
  gst_gl_context_set_window (context, window);
  GST_GL_WINDOW_GET_CLASS (window)->open (window, NULL);
  gst_object_unref (window);

  return context;
}

gboolean
gst_gl_context_gpu_process_create(GstGLContext* context)
{;
  GError *error = NULL;

  gst_gl_context_activate (context, TRUE);
  gst_gl_context_fill_info (context, &error);

  if (error) {
    GST_ERROR_OBJECT (context, "Failed to create gpu process context: %s",
        error->message);
    g_error_free (error);
    gst_object_unref (context);
    return FALSE;
  }

  context->gl_vtable->EGLImageTargetTexture2D =
      gst_gl_context_get_proc_address (context, "glEGLImageTargetTexture2D");

  if (!context->gl_vtable->EGLImageTargetTexture2D) {
    GST_ERROR_OBJECT (context, "Cannot find glEGLImageTargetTexture2D");
    gst_object_unref (context);
    return FALSE;
  }

  return TRUE;
}

