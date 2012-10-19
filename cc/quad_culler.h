// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCQuadCuller_h
#define CCQuadCuller_h

#include "CCQuadSink.h"
#include "CCRenderPass.h"

namespace cc {
class LayerImpl;
class RenderSurfaceImpl;
template<typename LayerType, typename SurfaceType>
class OcclusionTrackerBase;

class QuadCuller : public QuadSink {
public:
    QuadCuller(QuadList&, SharedQuadStateList&, LayerImpl*, const OcclusionTrackerBase<LayerImpl, RenderSurfaceImpl>*, bool showCullingWithDebugBorderQuads, bool forSurface);
    virtual ~QuadCuller() { }

    // QuadSink implementation.
    virtual SharedQuadState* useSharedQuadState(scoped_ptr<SharedQuadState>) OVERRIDE;
    virtual bool append(scoped_ptr<DrawQuad>, AppendQuadsData&) OVERRIDE;

private:
    QuadList& m_quadList;
    SharedQuadStateList& m_sharedQuadStateList;
    SharedQuadState* m_currentSharedQuadState;
    LayerImpl* m_layer;
    const OcclusionTrackerBase<LayerImpl, RenderSurfaceImpl>* m_occlusionTracker;
    bool m_showCullingWithDebugBorderQuads;
    bool m_forSurface;
};

}
#endif // CCQuadCuller_h
