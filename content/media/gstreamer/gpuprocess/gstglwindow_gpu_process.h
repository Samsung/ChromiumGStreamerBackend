// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __GST_GL_WINDOW_GPU_PROCESS_H__
#define __GST_GL_WINDOW_GPU_PROCESS_H__

#include <gst/gst.h>
#include <gst/gl/gstglconfig.h>

#if !GST_GL_HAVE_GLES2
#error "GstGL with GLES2 support is required"
#endif

#undef GST_GL_HAVE_OPENGL
#define GST_GL_HAVE_OPENGL 0

#include <gst/gl/gl.h>
#include "gstglcontext_gpu_process.h"

G_BEGIN_DECLS

#define GST_GL_TYPE_WINDOW_GPU_PROCESS (gst_gl_window_gpu_process_get_type())
#define GST_GL_WINDOW_GPU_PROCESS(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_WINDOW_GPU_PROCESS, \
                              GstGLWindowGPUProcess))
#define GST_GL_WINDOW_GPU_PROCESS_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), GST_GL_TYPE_WINDOW_GPU_PROCESS, \
                           GstGLWindowGPUProcessClass))
#define GST_GL_IS_WINDOW_GPU_PROCESS(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_WINDOW_GPU_PROCESS))
#define GST_GL_IS_WINDOW_GPU_PROCESS_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_WINDOW_GPU_PROCESS))
#define GST_GL_WINDOW_GPU_PROCESS_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_WINDOW_GPU_PROCESS, \
                             GstGLWindowGPUProcessClass))

typedef struct _GstGLWindowGPUProcess GstGLWindowGPUProcess;
typedef struct _GstGLWindowGPUProcessPrivate GstGLWindowGPUProcessPrivate;
typedef struct _GstGLWindowGPUProcessClass GstGLWindowGPUProcessClass;

/**
 * GstGLWindowGPUProcess:
 *
 * Opaque #GstGLWindowGPUProcess object
 */
struct _GstGLWindowGPUProcess {
  GstGLWindow parent;

  /*< private >*/
  GstGLWindowGPUProcessPrivate* priv;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

/**
 * GstGLWindowGPUProcessClass:
 *
 * Opaque #GstGLWindowGPUProcessClass object
 */
struct _GstGLWindowGPUProcessClass {
  GstGLWindowClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_window_gpu_process_get_type(void);

GstGLWindowGPUProcess* gst_gl_window_gpu_process_new(GstGLDisplay* display);

G_END_DECLS

#endif /* __GST_GL_WINDOW_GPU_PROCESS_H__ */
