// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/favicon/select_favicon_frames.h"

#include "skia/ext/image_operations.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace {

size_t BiggestCandidate(const std::vector<SkBitmap>& bitmaps) {
  size_t max_index = 0;
  int max_area = bitmaps[0].width() * bitmaps[0].height();
  for (size_t i = 1; i < bitmaps.size(); ++i) {
    int area = bitmaps[i].width() * bitmaps[i].height();
    if (area > max_area) {
      max_area = area;
      max_index = i;
    }
  }
  return max_index;
}

SkBitmap SelectCandidate(const std::vector<SkBitmap>& bitmaps,
                         int desired_size) {
  // Try to find an exact match.
  for (size_t i = 0; i < bitmaps.size(); ++i) {
    if (bitmaps[i].width() == desired_size &&
        bitmaps[i].height() == desired_size) {
      return bitmaps[i];
    }
  }

  // If that failed, the following special rules apply:
  // 1. Integer multiples are built using nearest neighbor sampling.
  // TODO(thakis): Implement.

  // 2. 24px images are built from 16px images by adding a transparent border.
  if (desired_size == 24 || desired_size == 48) {
    int source_size = desired_size == 24 ? 16 : 32;
    for (size_t i = 0; i < bitmaps.size(); ++i) {
      if (bitmaps[i].width() == source_size &&
          bitmaps[i].height() == source_size) {
        SkBitmap bitmap;
        bitmap.setConfig(
            SkBitmap::kARGB_8888_Config, desired_size, desired_size);
        bitmap.allocPixels();
        bitmap.eraseARGB(0, 0, 0, 0);

        {
          SkCanvas canvas(bitmap);
          canvas.drawBitmap(bitmaps[i],
                            SkIntToScalar(source_size / 4),
                            SkIntToScalar(source_size / 4));
        }

        return bitmap;
      }
    }
  }

  // 3. Else, use Lancosz scaling:
  //    a) If available, from the next bigger integer multiple variant.
  //       TODO(thakis): Implement.
  //    b) Else, from the next bigger variant.
  int lancosz_candidate = -1;
  int min_area = INT_MAX;
  for (size_t i = 0; i < bitmaps.size(); ++i) {
    int area = bitmaps[i].width() * bitmaps[i].height();
    if (bitmaps[i].width() > desired_size &&
        bitmaps[i].height() > desired_size &&
        (lancosz_candidate == -1 || area < min_area)) {
      lancosz_candidate = i;
      min_area = area;
    }
  }
  //    c) Else, from the biggest smaller variant.
  if (lancosz_candidate == -1)
    lancosz_candidate = BiggestCandidate(bitmaps);

  return skia::ImageOperations::Resize(
      bitmaps[lancosz_candidate], skia::ImageOperations::RESIZE_LANCZOS3,
      desired_size, desired_size);
}

}  // namespace

gfx::ImageSkia SelectFaviconFrames(
    const std::vector<SkBitmap>& bitmaps,
    const std::vector<ui::ScaleFactor>& scale_factors,
    int desired_size) {
  gfx::ImageSkia multi_image;
  if (bitmaps.empty())
    return multi_image;

  if (desired_size == 0) {
    // Just return the biggest image available.
    size_t max_index = BiggestCandidate(bitmaps);
    multi_image.AddRepresentation(
        gfx::ImageSkiaRep(bitmaps[max_index], ui::SCALE_FACTOR_100P));
    return multi_image;
  }

  for (size_t i = 0; i < scale_factors.size(); ++i) {
    int size = static_cast<int>(
        desired_size * GetScaleFactorScale(scale_factors[i]) + 0.5f);
    multi_image.AddRepresentation(gfx::ImageSkiaRep(
          SelectCandidate(bitmaps, size), scale_factors[i]));
  }

  return multi_image;
}
