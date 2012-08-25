// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "CCTiledLayerImpl.h"

#include "CCLayerTestCommon.h"
#include "CCLayerTilingData.h"
#include "CCSingleThreadProxy.h"
#include "CCTileDrawQuad.h"
#include "MockCCQuadCuller.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace WebCore;
using namespace CCLayerTestCommon;

namespace {

// Create a default tiled layer with textures for all tiles and a default
// visibility of the entire layer size.
static PassOwnPtr<CCTiledLayerImpl> createLayer(const IntSize& tileSize, const IntSize& layerSize, CCLayerTilingData::BorderTexelOption borderTexels)
{
    OwnPtr<CCTiledLayerImpl> layer = CCTiledLayerImpl::create(1);
    OwnPtr<CCLayerTilingData> tiler = CCLayerTilingData::create(tileSize, borderTexels);
    tiler->setBounds(layerSize);
    layer->setTilingData(*tiler);
    layer->setSkipsDraw(false);
    layer->setVisibleContentRect(IntRect(IntPoint(), layerSize));
    layer->setDrawOpacity(1);
    layer->setBounds(layerSize);
    layer->setContentBounds(layerSize);
    layer->createRenderSurface();
    layer->setRenderTarget(layer.get());

    CCResourceProvider::ResourceId resourceId = 1;
    for (int i = 0; i < tiler->numTilesX(); ++i)
        for (int j = 0; j < tiler->numTilesY(); ++j)
            layer->pushTileProperties(i, j, resourceId++, IntRect(0, 0, 1, 1));

    return layer.release();
}

TEST(CCTiledLayerImplTest, emptyQuadList)
{
    DebugScopedSetImplThread scopedImplThread;

    const IntSize tileSize(90, 90);
    const int numTilesX = 8;
    const int numTilesY = 4;
    const IntSize layerSize(tileSize.width() * numTilesX, tileSize.height() * numTilesY);

    // Verify default layer does creates quads
    {
        OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, CCLayerTilingData::NoBorderTexels);
        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        const unsigned numTiles = numTilesX * numTilesY;
        EXPECT_EQ(quadCuller.quadList().size(), numTiles);
    }

    // Layer with empty visible layer rect produces no quads
    {
        OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, CCLayerTilingData::NoBorderTexels);
        layer->setVisibleContentRect(IntRect());

        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        EXPECT_EQ(quadCuller.quadList().size(), 0u);
    }

    // Layer with non-intersecting visible layer rect produces no quads
    {
        OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, CCLayerTilingData::NoBorderTexels);

        IntRect outsideBounds(IntPoint(-100, -100), IntSize(50, 50));
        layer->setVisibleContentRect(outsideBounds);

        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        EXPECT_EQ(quadCuller.quadList().size(), 0u);
    }

    // Layer with skips draw produces no quads
    {
        OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, CCLayerTilingData::NoBorderTexels);
        layer->setSkipsDraw(true);

        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        EXPECT_EQ(quadCuller.quadList().size(), 0u);
    }
}

TEST(CCTiledLayerImplTest, checkerboarding)
{
    DebugScopedSetImplThread scopedImplThread;

    const IntSize tileSize(10, 10);
    const int numTilesX = 2;
    const int numTilesY = 2;
    const IntSize layerSize(tileSize.width() * numTilesX, tileSize.height() * numTilesY);

    OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, CCLayerTilingData::NoBorderTexels);

    // No checkerboarding
    {
        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        EXPECT_EQ(quadCuller.quadList().size(), 4u);
        EXPECT_FALSE(hadMissingTiles);

        for (size_t i = 0; i < quadCuller.quadList().size(); ++i)
            EXPECT_EQ(quadCuller.quadList()[i]->material(), CCDrawQuad::TiledContent);
    }

    for (int i = 0; i < numTilesX; ++i)
        for (int j = 0; j < numTilesY; ++j)
            layer->pushTileProperties(i, j, 0, IntRect());

    // All checkerboarding
    {
        MockCCQuadCuller quadCuller;
        bool hadMissingTiles = false;
        layer->appendQuads(quadCuller, hadMissingTiles);
        EXPECT_TRUE(hadMissingTiles);
        EXPECT_EQ(quadCuller.quadList().size(), 4u);
        for (size_t i = 0; i < quadCuller.quadList().size(); ++i)
            EXPECT_NE(quadCuller.quadList()[i]->material(), CCDrawQuad::TiledContent);
    }
}

static void getQuads(CCQuadList& quads, CCSharedQuadStateList& sharedStates, IntSize tileSize, const IntSize& layerSize, CCLayerTilingData::BorderTexelOption borderTexelOption, const IntRect& visibleContentRect)
{
    OwnPtr<CCTiledLayerImpl> layer = createLayer(tileSize, layerSize, borderTexelOption);
    layer->setVisibleContentRect(visibleContentRect);
    layer->setBounds(layerSize);

    MockCCQuadCuller quadCuller(quads, sharedStates);
    bool hadMissingTiles = false;
    layer->appendQuads(quadCuller, hadMissingTiles);
}

// Test with both border texels and without.
#define WITH_AND_WITHOUT_BORDER_TEST(testFixtureName)       \
    TEST(CCTiledLayerImplTest, testFixtureName##NoBorders)  \
    {                                                       \
        testFixtureName(CCLayerTilingData::NoBorderTexels); \
    }                                                       \
    TEST(CCTiledLayerImplTest, testFixtureName##HasBorders) \
    {                                                       \
        testFixtureName(CCLayerTilingData::HasBorderTexels);\
    }

static void coverageVisibleRectOnTileBoundaries(CCLayerTilingData::BorderTexelOption borders)
{
    DebugScopedSetImplThread scopedImplThread;

    IntSize layerSize(1000, 1000);
    CCQuadList quads;
    CCSharedQuadStateList sharedStates;
    getQuads(quads, sharedStates, IntSize(100, 100), layerSize, borders, IntRect(IntPoint(), layerSize));
    verifyQuadsExactlyCoverRect(quads, IntRect(IntPoint(), layerSize));
}
WITH_AND_WITHOUT_BORDER_TEST(coverageVisibleRectOnTileBoundaries);

static void coverageVisibleRectIntersectsTiles(CCLayerTilingData::BorderTexelOption borders)
{
    DebugScopedSetImplThread scopedImplThread;

    // This rect intersects the middle 3x3 of the 5x5 tiles.
    IntPoint topLeft(65, 73);
    IntPoint bottomRight(182, 198);
    IntRect visibleContentRect(topLeft, bottomRight - topLeft);

    IntSize layerSize(250, 250);
    CCQuadList quads;
    CCSharedQuadStateList sharedStates;
    getQuads(quads, sharedStates, IntSize(50, 50), IntSize(250, 250), CCLayerTilingData::NoBorderTexels, visibleContentRect);
    verifyQuadsExactlyCoverRect(quads, visibleContentRect);
}
WITH_AND_WITHOUT_BORDER_TEST(coverageVisibleRectIntersectsTiles);

static void coverageVisibleRectIntersectsBounds(CCLayerTilingData::BorderTexelOption borders)
{
    DebugScopedSetImplThread scopedImplThread;

    IntSize layerSize(220, 210);
    IntRect visibleContentRect(IntPoint(), layerSize);
    CCQuadList quads;
    CCSharedQuadStateList sharedStates;
    getQuads(quads, sharedStates, IntSize(100, 100), layerSize, CCLayerTilingData::NoBorderTexels, visibleContentRect);
    verifyQuadsExactlyCoverRect(quads, visibleContentRect);
}
WITH_AND_WITHOUT_BORDER_TEST(coverageVisibleRectIntersectsBounds);

TEST(CCTiledLayerImplTest, textureInfoForLayerNoBorders)
{
    DebugScopedSetImplThread scopedImplThread;

    IntSize tileSize(50, 50);
    IntSize layerSize(250, 250);
    CCQuadList quads;
    CCSharedQuadStateList sharedStates;
    getQuads(quads, sharedStates, tileSize, layerSize, CCLayerTilingData::NoBorderTexels, IntRect(IntPoint(), layerSize));

    for (size_t i = 0; i < quads.size(); ++i) {
        ASSERT_EQ(quads[i]->material(), CCDrawQuad::TiledContent) << quadString << i;
        CCTileDrawQuad* quad = static_cast<CCTileDrawQuad*>(quads[i].get());

        EXPECT_NE(quad->resourceId(), 0u) << quadString << i;
        EXPECT_EQ(quad->textureOffset(), IntPoint()) << quadString << i;
        EXPECT_EQ(quad->textureSize(), tileSize) << quadString << i;
        EXPECT_EQ(IntRect(0, 0, 1, 1), quad->opaqueRect()) << quadString << i;
    }
}

TEST(CCTiledLayerImplTest, tileOpaqueRectForLayerNoBorders)
{
    DebugScopedSetImplThread scopedImplThread;

    IntSize tileSize(50, 50);
    IntSize layerSize(250, 250);
    CCQuadList quads;
    CCSharedQuadStateList sharedStates;
    getQuads(quads, sharedStates, tileSize, layerSize, CCLayerTilingData::NoBorderTexels, IntRect(IntPoint(), layerSize));

    for (size_t i = 0; i < quads.size(); ++i) {
        ASSERT_EQ(quads[i]->material(), CCDrawQuad::TiledContent) << quadString << i;
        CCTileDrawQuad* quad = static_cast<CCTileDrawQuad*>(quads[i].get());

        EXPECT_EQ(IntRect(0, 0, 1, 1), quad->opaqueRect()) << quadString << i;
    }
}

} // namespace
