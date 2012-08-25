// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#if USE(ACCELERATED_COMPOSITING)

#include "CCRenderSurface.h"

#include "CCDamageTracker.h"
#include "CCDebugBorderDrawQuad.h"
#include "CCLayerImpl.h"
#include "CCMathUtil.h"
#include "CCQuadSink.h"
#include "CCRenderPassDrawQuad.h"
#include "CCSharedQuadState.h"
#include "TextStream.h"
#include <public/WebTransformationMatrix.h>
#include <wtf/text/CString.h>

using WebKit::WebTransformationMatrix;

namespace WebCore {

static const int debugSurfaceBorderWidth = 2;
static const int debugSurfaceBorderAlpha = 100;
static const int debugSurfaceBorderColorRed = 0;
static const int debugSurfaceBorderColorGreen = 0;
static const int debugSurfaceBorderColorBlue = 255;
static const int debugReplicaBorderColorRed = 160;
static const int debugReplicaBorderColorGreen = 0;
static const int debugReplicaBorderColorBlue = 255;

CCRenderSurface::CCRenderSurface(CCLayerImpl* owningLayer)
    : m_owningLayer(owningLayer)
    , m_surfacePropertyChanged(false)
    , m_drawOpacity(1)
    , m_drawOpacityIsAnimating(false)
    , m_targetSurfaceTransformsAreAnimating(false)
    , m_screenSpaceTransformsAreAnimating(false)
    , m_nearestAncestorThatMovesPixels(0)
    , m_targetRenderSurfaceLayerIndexHistory(0)
    , m_currentLayerIndexHistory(0)
{
    m_damageTracker = CCDamageTracker::create();
}

CCRenderSurface::~CCRenderSurface()
{
}

FloatRect CCRenderSurface::drawableContentRect() const
{
    FloatRect drawableContentRect = CCMathUtil::mapClippedRect(m_drawTransform, m_contentRect);
    if (m_owningLayer->hasReplica())
        drawableContentRect.unite(CCMathUtil::mapClippedRect(m_replicaDrawTransform, m_contentRect));

    return drawableContentRect;
}

String CCRenderSurface::name() const
{
    return String::format("RenderSurface(id=%i,owner=%s)", m_owningLayer->id(), m_owningLayer->debugName().utf8().data());
}

static void writeIndent(TextStream& ts, int indent)
{
    for (int i = 0; i != indent; ++i)
        ts << "  ";
}

void CCRenderSurface::dumpSurface(TextStream& ts, int indent) const
{
    writeIndent(ts, indent);
    ts << name() << "\n";

    writeIndent(ts, indent+1);
    ts << "contentRect: (" << m_contentRect.x() << ", " << m_contentRect.y() << ", " << m_contentRect.width() << ", " << m_contentRect.height() << "\n";

    writeIndent(ts, indent+1);
    ts << "drawTransform: ";
    ts << m_drawTransform.m11() << ", " << m_drawTransform.m12() << ", " << m_drawTransform.m13() << ", " << m_drawTransform.m14() << "  //  ";
    ts << m_drawTransform.m21() << ", " << m_drawTransform.m22() << ", " << m_drawTransform.m23() << ", " << m_drawTransform.m24() << "  //  ";
    ts << m_drawTransform.m31() << ", " << m_drawTransform.m32() << ", " << m_drawTransform.m33() << ", " << m_drawTransform.m34() << "  //  ";
    ts << m_drawTransform.m41() << ", " << m_drawTransform.m42() << ", " << m_drawTransform.m43() << ", " << m_drawTransform.m44() << "\n";

    writeIndent(ts, indent+1);
    ts << "damageRect is pos(" << m_damageTracker->currentDamageRect().x() << "," << m_damageTracker->currentDamageRect().y() << "), ";
    ts << "size(" << m_damageTracker->currentDamageRect().width() << "," << m_damageTracker->currentDamageRect().height() << ")\n";
}

int CCRenderSurface::owningLayerId() const
{
    return m_owningLayer ? m_owningLayer->id() : 0;
}


void CCRenderSurface::setClipRect(const IntRect& clipRect)
{
    if (m_clipRect == clipRect)
        return;

    m_surfacePropertyChanged = true;
    m_clipRect = clipRect;
}

bool CCRenderSurface::contentsChanged() const
{
    return !m_damageTracker->currentDamageRect().isEmpty();
}

void CCRenderSurface::setContentRect(const IntRect& contentRect)
{
    if (m_contentRect == contentRect)
        return;

    m_surfacePropertyChanged = true;
    m_contentRect = contentRect;
}

bool CCRenderSurface::surfacePropertyChanged() const
{
    // Surface property changes are tracked as follows:
    //
    // - m_surfacePropertyChanged is flagged when the clipRect or contentRect change. As
    //   of now, these are the only two properties that can be affected by descendant layers.
    //
    // - all other property changes come from the owning layer (or some ancestor layer
    //   that propagates its change to the owning layer).
    //
    ASSERT(m_owningLayer);
    return m_surfacePropertyChanged || m_owningLayer->layerPropertyChanged();
}

bool CCRenderSurface::surfacePropertyChangedOnlyFromDescendant() const
{
    return m_surfacePropertyChanged && !m_owningLayer->layerPropertyChanged();
}

static inline IntRect computeClippedRectInTarget(const CCLayerImpl* owningLayer)
{
    ASSERT(owningLayer->parent());

    const CCLayerImpl* renderTarget = owningLayer->parent()->renderTarget();
    const CCRenderSurface* self = owningLayer->renderSurface();

    IntRect clippedRectInTarget = self->clipRect();
    if (owningLayer->backgroundFilters().hasFilterThatMovesPixels()) {
        // If the layer has background filters that move pixels, we cannot scissor as tightly.
        // FIXME: this should be able to be a tighter scissor, perhaps expanded by the filter outsets?
        clippedRectInTarget = renderTarget->renderSurface()->contentRect();
    } else if (clippedRectInTarget.isEmpty()) {
        // For surfaces, empty clipRect means that the surface does not clip anything.
        clippedRectInTarget = enclosingIntRect(intersection(renderTarget->renderSurface()->contentRect(), self->drawableContentRect()));
    } else
        clippedRectInTarget.intersect(enclosingIntRect(self->drawableContentRect()));
    return clippedRectInTarget;
}

void CCRenderSurface::appendQuads(CCQuadSink& quadSink, bool forReplica, int renderPassId)
{
    ASSERT(!forReplica || m_owningLayer->hasReplica());

    IntRect clippedRectInTarget = computeClippedRectInTarget(m_owningLayer);
    bool isOpaque = false;
    const WebTransformationMatrix& drawTransform = forReplica ? m_replicaDrawTransform : m_drawTransform;
    CCSharedQuadState* sharedQuadState = quadSink.useSharedQuadState(CCSharedQuadState::create(drawTransform, m_contentRect, clippedRectInTarget, m_drawOpacity, isOpaque));

    if (m_owningLayer->hasDebugBorders()) {
        int red = forReplica ? debugReplicaBorderColorRed : debugSurfaceBorderColorRed;
        int green = forReplica ?  debugReplicaBorderColorGreen : debugSurfaceBorderColorGreen;
        int blue = forReplica ? debugReplicaBorderColorBlue : debugSurfaceBorderColorBlue;
        SkColor color = SkColorSetARGB(debugSurfaceBorderAlpha, red, green, blue);
        quadSink.append(CCDebugBorderDrawQuad::create(sharedQuadState, contentRect(), color, debugSurfaceBorderWidth));
    }

    // FIXME: By using the same RenderSurface for both the content and its reflection,
    // it's currently not possible to apply a separate mask to the reflection layer
    // or correctly handle opacity in reflections (opacity must be applied after drawing
    // both the layer and its reflection). The solution is to introduce yet another RenderSurface
    // to draw the layer and its reflection in. For now we only apply a separate reflection
    // mask if the contents don't have a mask of their own.
    CCLayerImpl* maskLayer = m_owningLayer->maskLayer();
    if (maskLayer && (!maskLayer->drawsContent() || maskLayer->bounds().isEmpty()))
        maskLayer = 0;

    if (!maskLayer && forReplica) {
        maskLayer = m_owningLayer->replicaLayer()->maskLayer();
        if (maskLayer && (!maskLayer->drawsContent() || maskLayer->bounds().isEmpty()))
            maskLayer = 0;
    }

    float maskTexCoordScaleX = 1;
    float maskTexCoordScaleY = 1;
    float maskTexCoordOffsetX = 1;
    float maskTexCoordOffsetY = 1;
    if (maskLayer) {
        maskTexCoordScaleX = static_cast<float>(contentRect().width()) / maskLayer->contentBounds().width();
        maskTexCoordScaleY = static_cast<float>(contentRect().height()) / maskLayer->contentBounds().height();
        maskTexCoordOffsetX = static_cast<float>(contentRect().x()) / contentRect().width() * maskTexCoordScaleX;
        maskTexCoordOffsetY = static_cast<float>(contentRect().y()) / contentRect().height() * maskTexCoordScaleY;
    }

    CCResourceProvider::ResourceId maskResourceId = maskLayer ? maskLayer->contentsResourceId() : 0;
    IntRect contentsChangedSinceLastFrame = contentsChanged() ? m_contentRect : IntRect();

    quadSink.append(CCRenderPassDrawQuad::create(sharedQuadState, contentRect(), renderPassId, forReplica, maskResourceId, contentsChangedSinceLastFrame,
                                                 maskTexCoordScaleX, maskTexCoordScaleY, maskTexCoordOffsetX, maskTexCoordOffsetY));
}

}
#endif // USE(ACCELERATED_COMPOSITING)
