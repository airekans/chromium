// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_ANDROID_SYNCHRONOUS_COMPOSITOR_H_
#define CONTENT_PUBLIC_BROWSER_ANDROID_SYNCHRONOUS_COMPOSITOR_H_

#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"

class SkCanvas;

namespace gfx {
class Transform;
};

namespace content {

class WebContents;

class SynchronousCompositorClient;

// Interface for embedders that wish to direct compositing operations
// synchronously under their own control. Only meaningful when the
// kEnableSyncrhonousRendererCompositor flag is specified.
class SynchronousCompositor {
 public:
  // Must be called once per WebContents instance. Will create the compositor
  // instance as needed, but only if |client| is non-NULL.
  static void SetClientForWebContents(WebContents* contents,
                                      SynchronousCompositorClient* client);

  // Allows changing or resetting the client to NULL (this must be used if
  // the client is being deleted prior to the DidDestroyCompositor() call
  // being received by the client). Ownership of |client| remains with
  // the caller.
  virtual void SetClient(SynchronousCompositorClient* client) = 0;

  // Returns true if the compositor is fully initialized and ready to receive
  // calls to DemandDrawHw().
  virtual bool IsHwReady() = 0;

  // "On demand" SW draw, into the supplied canvas (observing the transform
  // and clip set there-in).
  virtual bool DemandDrawSw(SkCanvas* canvas) = 0;

  // "On demand" hardware draw. The content is first clipped to |damage_area|,
  // then transformed through |transform|, and finally clipped to |view_size|.
  virtual bool DemandDrawHw(
      gfx::Size view_size,
      const gfx::Transform& transform,
      gfx::Rect damage_area) = 0;

 protected:
  virtual ~SynchronousCompositor() {}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_ANDROID_SYNCHRONOUS_COMPOSITOR_H_
