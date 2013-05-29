// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/mirror_window_controller.h"

#if defined(USE_X11)
#include <X11/Xlib.h>

// Xlib.h defines RootWindow.
#undef RootWindow
#endif

#include "ash/display/display_info.h"
#include "ash/display/display_manager.h"
#include "ash/display/root_window_transformers.h"
#include "ash/host/root_window_host_factory.h"
#include "ash/shell.h"
#include "base/stringprintf.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_transformer.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/cursor/cursors_aura.h"
#include "ui/base/hit_test.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/compositor/compositor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_widget_types.h"

namespace ash {
namespace internal {
namespace {

#if defined(USE_X11)
// Mirror window shouldn't handle input events.
void DisableInput(XID window) {
  long event_mask = ExposureMask | VisibilityChangeMask |
      StructureNotifyMask | PropertyChangeMask;
  XSelectInput(base::MessagePumpAuraX11::GetDefaultXDisplay(),
               window, event_mask);
}
#endif

class NoneCaptureClient : public aura::client::CaptureClient {
 public:
  NoneCaptureClient() {}
  virtual ~NoneCaptureClient() {}

 private:
  // Does a capture on the |window|.
  virtual void SetCapture(aura::Window* window) OVERRIDE {}

  // Releases a capture from the |window|.
  virtual void ReleaseCapture(aura::Window* window) OVERRIDE {}

  // Returns the current capture window.
  virtual aura::Window* GetCaptureWindow() OVERRIDE {
    return NULL;
  }

  DISALLOW_COPY_AND_ASSIGN(NoneCaptureClient);
};

}  // namespace

class CursorWindowDelegate : public aura::WindowDelegate {
 public:
  CursorWindowDelegate() {}
  virtual ~CursorWindowDelegate() {}

  // aura::WindowDelegate overrides:
  virtual gfx::Size GetMinimumSize() const OVERRIDE {
    return size_;
  }
  virtual gfx::Size GetMaximumSize() const OVERRIDE {
    return size_;
  }
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) OVERRIDE {
  }
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE {
    return gfx::kNullCursor;
  }
  virtual int GetNonClientComponent(
      const gfx::Point& point) const OVERRIDE {
    return HTNOWHERE;
  }
  virtual bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) OVERRIDE {
    return false;
  }
  virtual bool CanFocus() OVERRIDE {
    return false;
  }
  virtual void OnCaptureLost() OVERRIDE {
  }
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {
    canvas->DrawImageInt(cursor_image_, 0, 0);
  }
  virtual void OnDeviceScaleFactorChanged(
      float device_scale_factor) OVERRIDE {
  }
  virtual void OnWindowDestroying() OVERRIDE {}
  virtual void OnWindowDestroyed() OVERRIDE {}
  virtual void OnWindowTargetVisibilityChanged(bool visible) OVERRIDE {
  }
  virtual bool HasHitTestMask() const OVERRIDE {
    return false;
  }
  virtual void GetHitTestMask(gfx::Path* mask) const OVERRIDE {}
  virtual scoped_refptr<ui::Texture> CopyTexture() OVERRIDE {
    NOTREACHED();
    return scoped_refptr<ui::Texture>();
  }

  // Set the cursor image for the |display|'s scale factor.  Note that
  // mirror window's scale factor is always 1.0f, therefore we need to
  // take 2x's image and paint as if it's 1x image.
  void SetCursorImage(const gfx::ImageSkia& image,
                      const gfx::Display& display) {
    device_scale_factor_ =
        ui::GetScaleFactorFromScale(display.device_scale_factor());
    const gfx::ImageSkiaRep& image_rep =
        image.GetRepresentation(device_scale_factor_);
    size_ = image_rep.pixel_size();
    cursor_image_ = gfx::ImageSkia::CreateFrom1xBitmap(image_rep.sk_bitmap());
  }

  const gfx::Size size() const { return size_; }

 private:
  gfx::ImageSkia cursor_image_;
  ui::ScaleFactor device_scale_factor_;
  gfx::Size size_;

  DISALLOW_COPY_AND_ASSIGN(CursorWindowDelegate);
};

MirrorWindowController::MirrorWindowController()
    : current_cursor_type_(ui::kCursorNone),
      cursor_window_(NULL),
      cursor_window_delegate_(new CursorWindowDelegate) {
}

MirrorWindowController::~MirrorWindowController() {
  // Make sure the root window gets deleted before cursor_window_delegate.
  Close();
}

void MirrorWindowController::UpdateWindow(const DisplayInfo& display_info) {
  static int mirror_root_window_count = 0;
  DisplayManager* display_manager = Shell::GetInstance()->display_manager();

  if (!root_window_.get()) {
    const gfx::Rect& bounds_in_pixel = display_info.bounds_in_pixel();
    aura::RootWindow::CreateParams params(bounds_in_pixel);
    params.host = Shell::GetInstance()->root_window_host_factory()->
        CreateRootWindowHost(bounds_in_pixel);
    root_window_.reset(new aura::RootWindow(params));
    root_window_->SetName(
        base::StringPrintf("MirrorRootWindow-%d", mirror_root_window_count++));
    root_window_->compositor()->SetBackgroundColor(SK_ColorBLACK);
    // No need to remove RootWindowObserver because
    // the DisplayManager object outlives RootWindow objects.
    root_window_->AddRootWindowObserver(display_manager);
    // TODO(oshima): TouchHUD is using idkey.
    root_window_->SetProperty(internal::kDisplayIdKey, display_info.id());
    root_window_->Init();
#if defined(USE_X11)
    DisableInput(root_window_->GetAcceleratedWidget());
#endif

    aura::client::SetCaptureClient(root_window_.get(), new NoneCaptureClient());
    root_window_->ShowRootWindow();

    // TODO(oshima): Start mirroring.

    cursor_window_ = new aura::Window(cursor_window_delegate_.get());
    cursor_window_->SetTransparent(true);
    cursor_window_->Init(ui::LAYER_TEXTURED);
    root_window_->AddChild(cursor_window_);
    cursor_window_->Show();
  } else {
    root_window_->SetProperty(internal::kDisplayIdKey, display_info.id());
    root_window_->SetHostBounds(display_info.bounds_in_pixel());
  }

  const DisplayInfo& source_display_info = display_manager->GetDisplayInfo(
      Shell::GetScreen()->GetPrimaryDisplay().id());
  DCHECK(display_manager->mirrored_display().is_valid());
  scoped_ptr<aura::RootWindowTransformer> transformer(
      internal::CreateRootWindowTransformerForMirroredDisplay(
          source_display_info,
          display_info));
  root_window_->SetRootWindowTransformer(transformer.Pass());

  UpdateCursorLocation();
}

void MirrorWindowController::UpdateWindow() {
  if (root_window_.get()) {
    DisplayManager* display_manager = Shell::GetInstance()->display_manager();
    const DisplayInfo& mirror_display_info = display_manager->GetDisplayInfo(
        display_manager->mirrored_display().id());
    UpdateWindow(mirror_display_info);
  }
}

void MirrorWindowController::Close() {
  if (root_window_.get()) {
    root_window_->RemoveRootWindowObserver(
        Shell::GetInstance()->display_manager());
    NoneCaptureClient* capture_client = static_cast<NoneCaptureClient*>(
        aura::client::GetCaptureClient(root_window_.get()));
    delete capture_client;
    root_window_.reset();
    cursor_window_ = NULL;
  }
}

void MirrorWindowController::UpdateCursorLocation() {
  if (cursor_window_) {
    // TODO(oshima): Rotate cursor image (including hotpoint).
    gfx::Point point = aura::Env::GetInstance()->last_mouse_location();
    Shell::GetPrimaryRootWindow()->ConvertPointToHost(&point);
    point.Offset(-hot_point_.x(), -hot_point_.y());
    gfx::Rect bounds = cursor_window_->bounds();
    bounds.set_origin(point);
    cursor_window_->SetBounds(bounds);
  }
}

void MirrorWindowController::SetMirroredCursor(gfx::NativeCursor cursor) {
  if (current_cursor_type_ == cursor.native_type())
    return;
  current_cursor_type_ = cursor.native_type();
  int resource_id;
  const gfx::Display& display = Shell::GetScreen()->GetPrimaryDisplay();
  bool success = ui::GetCursorDataFor(
      current_cursor_type_,
      display.device_scale_factor(),
      &resource_id,
      &hot_point_);
  if (!success)
    return;
  const gfx::ImageSkia* image =
      ResourceBundle::GetSharedInstance().GetImageSkiaNamed(resource_id);
  cursor_window_delegate_->SetCursorImage(*image, display);
  if (cursor_window_) {
    cursor_window_->SetBounds(gfx::Rect(cursor_window_delegate_->size()));
    cursor_window_->SchedulePaintInRect(
        gfx::Rect(cursor_window_->bounds().size()));
    UpdateCursorLocation();
  }
}

void MirrorWindowController::SetMirroredCursorVisibility(bool visible) {
  if (cursor_window_)
    visible ? cursor_window_->Show() : cursor_window_->Hide();
}

}  // namespace internal
}  // namespace ash
