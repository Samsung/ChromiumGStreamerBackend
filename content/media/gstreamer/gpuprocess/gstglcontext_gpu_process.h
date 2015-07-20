// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __GST_GL_CONTEXT_GPU_PROCESS_H__
#define __GST_GL_CONTEXT_GPU_PROCESS_H__

#include <gst/gst.h>
#include <gst/gl/gstglconfig.h>

#if !GST_GL_HAVE_GLES2
#error "GstGL with GLES2 support is required"
#endif

#undef GST_GL_HAVE_OPENGL
#define GST_GL_HAVE_OPENGL 0

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_CONTEXT_GPU_PROCESS (gst_gl_context_gpu_process_get_type())
#define GST_GL_CONTEXT_GPU_PROCESS(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, \
                              GstGLContextGPUProcess))
#define GST_GL_CONTEXT_GPU_PROCESS_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), GST_GL_TYPE_CONTEXT_GPU_PROCESS, \
                           GstGLContextGPUProcessClass))
#define GST_GL_IS_CONTEXT_GPU_PROCESS(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS))
#define GST_GL_IS_CONTEXT_GPU_PROCESS_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_CONTEXT_GPU_PROCESS))
#define GST_GL_CONTEXT_GPU_PROCESS_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_CONTEXT_GPU_PROCESS, \
                             GstGLContextGPUProcessClass))

typedef struct _GstGLContextGPUProcess GstGLContextGPUProcess;
typedef struct _GstGLContextGPUProcessPrivate GstGLContextGPUProcessPrivate;
typedef struct _GstGLContextGPUProcessClass GstGLContextGPUProcessClass;

typedef gpointer (*GstGLProcAddrFunc)(GstGLAPI gl_api, const gchar* name);

/**
 * GstGLContextGPUProcess:
 *
 * Opaque #GstGLContextGPUProcess object
 */
struct _GstGLContextGPUProcess {
  GstGLContext parent;

  /*< private >*/
  GstGLContextGPUProcessPrivate* priv;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

struct _GstGLContextGPUProcessClass {
  GstGLContextClass parent;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_context_gpu_process_get_type(void);

GstGLContext* gst_gl_context_gpu_process_new(GstGLDisplay* display,
                                             GstGLAPI gl_api,
                                             GstGLProcAddrFunc proc_addr);

G_END_DECLS

#endif /* __GST_GL_CONTEXT_GPU_PROCESS_H__ */
