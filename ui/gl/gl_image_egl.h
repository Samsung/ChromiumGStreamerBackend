// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GL_GL_IMAGE_EGL_H_
#define UI_GL_GL_IMAGE_EGL_H_

#include "base/threading/thread_checker.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_image.h"

#if defined(USE_GSTREAMER)
#include "base/files/scoped_file.h"
#include "base/memory/scoped_vector.h"
#endif

namespace gfx {

class GL_EXPORT GLImageEGL : public GLImage {
 public:
  explicit GLImageEGL(const Size& size);

  bool Initialize(EGLenum target, EGLClientBuffer buffer, const EGLint* attrs);

  // Overridden from GLImage:
  void Destroy(bool have_context) override;
  Size GetSize() override;
  unsigned GetInternalFormat() override;
  bool BindTexImage(unsigned target) override;
  void ReleaseTexImage(unsigned target) override {}
  bool CopyTexImage(unsigned target) override;
  bool CopyTexSubImage(unsigned target,
                       const Point& offset,
                       const Rect& rect) override;
  bool ScheduleOverlayPlane(AcceleratedWidget widget,
                            int z_order,
                            OverlayTransform transform,
                            const Rect& bounds_rect,
                            const RectF& crop_rect) override;

#if defined(USE_GSTREAMER)
  void OnMemoryDump(base::trace_event::ProcessMemoryDump* pmd,
                    uint64_t process_tracing_id,
                    const std::string& dump_name) override {}
#endif

 protected:
  ~GLImageEGL() override;

  EGLImageKHR egl_image_;
  const Size size_;
  base::ThreadChecker thread_checker_;

#if defined(USE_GSTREAMER)
  ScopedVector<base::ScopedFD> dmabuf_fds_;
#endif

 private:
  DISALLOW_COPY_AND_ASSIGN(GLImageEGL);
};

}  // namespace gfx

#endif  // UI_GL_GL_IMAGE_EGL_H_
