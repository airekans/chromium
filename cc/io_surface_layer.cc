// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

#include "cc/io_surface_layer.h"

#include "CCIOSurfaceLayerImpl.h"

namespace cc {

scoped_refptr<IOSurfaceLayer> IOSurfaceLayer::create()
{
    return make_scoped_refptr(new IOSurfaceLayer());
}

IOSurfaceLayer::IOSurfaceLayer()
    : Layer()
    , m_ioSurfaceId(0)
{
}

IOSurfaceLayer::~IOSurfaceLayer()
{
}

void IOSurfaceLayer::setIOSurfaceProperties(uint32_t ioSurfaceId, const IntSize& size)
{
    m_ioSurfaceId = ioSurfaceId;
    m_ioSurfaceSize = size;
    setNeedsCommit();
}

scoped_ptr<LayerImpl> IOSurfaceLayer::createLayerImpl()
{
    return IOSurfaceLayerImpl::create(m_layerId).PassAs<LayerImpl>();
}

bool IOSurfaceLayer::drawsContent() const
{
    return m_ioSurfaceId && Layer::drawsContent();
}

void IOSurfaceLayer::pushPropertiesTo(LayerImpl* layer)
{
    Layer::pushPropertiesTo(layer);

    IOSurfaceLayerImpl* textureLayer = static_cast<IOSurfaceLayerImpl*>(layer);
    textureLayer->setIOSurfaceProperties(m_ioSurfaceId, m_ioSurfaceSize);
}

}
