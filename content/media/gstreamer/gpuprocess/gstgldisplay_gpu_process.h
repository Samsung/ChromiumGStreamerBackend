// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __GST_GL_DISPLAY_GPU_PROCESS_H__
#define __GST_GL_DISPLAY_GPU_PROCESS_H__

#include <gst/gst.h>
#include <gst/gl/gstglconfig.h>

#if !GST_GL_HAVE_GLES2
#error "GstGL with GLES2 support is required"
#endif

#undef GST_GL_HAVE_OPENGL
#define GST_GL_HAVE_OPENGL 0

#include <gst/gl/gl.h>

G_BEGIN_DECLS

GType gst_gl_display_gpu_process_get_type(void);

#define GST_TYPE_GL_DISPLAY_GPU_PROCESS (gst_gl_display_gpu_process_get_type())
#define GST_GL_DISPLAY_GPU_PROCESS(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GL_DISPLAY_GPU_PROCESS, \
                              GstGLDisplayGPUProcess))
#define GST_GL_DISPLAY_GPU_PROCESS_CLASS(klass)                      \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GL_DISPLAY_GPU_PROCESS, \
                           GstGLDisplayGPUProcessClass))
#define GST_IS_GL_DISPLAY_GPU_PROCESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GL_DISPLAY_GPU_PROCESS))
#define GST_IS_GL_DISPLAY_GPU_PROCESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GL_DISPLAY_GPU_PROCESS))
#define GST_GL_DISPLAY_GPU_PROCESS_CAST(obj) ((GstGLDisplayGPUProcess*)(obj))

typedef struct _GstGLDisplayGPUProcess GstGLDisplayGPUProcess;
typedef struct _GstGLDisplayGPUProcessClass GstGLDisplayGPUProcessClass;

struct _GstGLDisplayGPUProcess {
  GstGLDisplay parent;
};

struct _GstGLDisplayGPUProcessClass {
  GstGLDisplayClass object_class;
};

GstGLDisplayGPUProcess* gst_gl_display_gpu_process_new(void);

G_END_DECLS

#endif /* __GST_GL_DISPLAY_GPU_PROCESS_H__ */
