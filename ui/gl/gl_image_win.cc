// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_image.h"

#include "base/debug/trace_event.h"
#include "ui/gl/gl_image_stub.h"
#include "ui/gl/gl_implementation.h"

namespace gfx {

scoped_refptr<GLImage> GLImage::CreateGLImage(gfx::PluginWindowHandle window) {
  TRACE_EVENT0("gpu", "GLImage::CreateGLImage");
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
    case kGLImplementationDesktopGL:
    case kGLImplementationEGLGLES2: {
      return NULL;
    }
    case kGLImplementationMockGL:
      return new GLImageStub;
    default:
      NOTREACHED();
      return NULL;
  }
}

}  // namespace gfx
