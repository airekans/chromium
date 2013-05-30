// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_PICTURE_LAYER_IMPL_H_
#define CC_TEST_FAKE_PICTURE_LAYER_IMPL_H_

#include "base/memory/scoped_ptr.h"
#include "cc/layers/picture_layer_impl.h"

namespace cc {

class FakePictureLayerImpl : public PictureLayerImpl {
 public:
  static scoped_ptr<FakePictureLayerImpl> Create(
      LayerTreeImpl* tree_impl, int id) {
    return make_scoped_ptr(new FakePictureLayerImpl(tree_impl, id));
  }

  static scoped_ptr<FakePictureLayerImpl> CreateWithPile(
      LayerTreeImpl* tree_impl, int id, scoped_refptr<PicturePileImpl> pile) {
    return make_scoped_ptr(new FakePictureLayerImpl(tree_impl, id, pile));
  }

  virtual scoped_ptr<LayerImpl> CreateLayerImpl(LayerTreeImpl* tree_impl)
      OVERRIDE;
  virtual gfx::Size CalculateTileSize(gfx::Size content_bounds) OVERRIDE;

  using PictureLayerImpl::AddTiling;
  using PictureLayerImpl::CleanUpTilingsOnActiveLayer;
  using PictureLayerImpl::CanHaveTilings;
  using PictureLayerImpl::MarkVisibleResourcesAsRequired;

  PictureLayerImpl* twin_layer() { return twin_layer_; }
  PictureLayerTilingSet* tilings() { return tilings_.get(); }

  const Region& invalidation() const { return invalidation_; }
  void set_invalidation(const Region& region) { invalidation_ = region; }

  void set_fixed_tile_size(gfx::Size size) { fixed_tile_size_ = size; }

 protected:
  FakePictureLayerImpl(
      LayerTreeImpl* tree_impl,
      int id,
      scoped_refptr<PicturePileImpl> pile);
  FakePictureLayerImpl(LayerTreeImpl* tree_impl, int id);

 private:
  gfx::Size fixed_tile_size_;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_PICTURE_LAYER_IMPL_H_
