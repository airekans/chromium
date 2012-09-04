// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_WINDOW_DELEGATE_H_
#define UI_AURA_WINDOW_DELEGATE_H_

#include "base/compiler_specific.h"
#include "ui/aura/aura_export.h"
#include "ui/base/events.h"
#include "ui/base/events/event_handler.h"
#include "ui/gfx/native_widget_types.h"

namespace gfx {
class Canvas;
class Path;
class Point;
class Rect;
class Size;
}

namespace ui {
class GestureEvent;
class KeyEvent;
class MouseEvent;
class TouchEvent;
}

namespace aura {

// Delegate interface for aura::Window.
class AURA_EXPORT WindowDelegate : public ui::EventHandler {
 public:
  // Returns the window's minimum size, or size 0,0 if there is no limit.
  virtual gfx::Size GetMinimumSize() const = 0;

  // Called when the Window's position and/or size changes.
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) = 0;

  // Sent to the Window's delegate when the Window gains or loses focus.
  virtual void OnFocus(aura::Window* old_focused_window) = 0;
  virtual void OnBlur() = 0;

  virtual bool OnKeyEvent(ui::KeyEvent* event) = 0;

  // Returns the native cursor for the specified point, in window coordinates,
  // or NULL for the default cursor.
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) = 0;

  // Returns the non-client component (see hit_test.h) containing |point|, in
  // window coordinates.
  virtual int GetNonClientComponent(const gfx::Point& point) const = 0;

  // Returns true if event handling should descend into |child|. |location| is
  // in terms of the Window.
  virtual bool ShouldDescendIntoChildForEventHandling(
      Window* child,
      const gfx::Point& location) = 0;

  virtual bool OnMouseEvent(ui::MouseEvent* event) = 0;

  virtual ui::TouchStatus OnTouchEvent(ui::TouchEvent* event) = 0;

  virtual ui::EventResult OnGestureEvent(ui::GestureEvent* event) = 0;

  // Returns true of the window can be focused.
  virtual bool CanFocus() = 0;

  // Invoked when mouse capture is lost on the window.
  virtual void OnCaptureLost() = 0;

  // Asks the delegate to paint window contents into the supplied canvas.
  virtual void OnPaint(gfx::Canvas* canvas) = 0;

  // Called when the window's device scale factor has changed.
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) = 0;

  // Called from Window's destructor before OnWindowDestroyed and before the
  // children have been destroyed and the window has been removed from its
  // parent.
  virtual void OnWindowDestroying() = 0;

  // Called when the Window has been destroyed (i.e. from its destructor). This
  // is called after OnWindowDestroying and after the children have been
  // deleted and the window has been removed from its parent.
  // The delegate can use this as an opportunity to delete itself if necessary.
  virtual void OnWindowDestroyed() = 0;

  // Called when the TargetVisibility() of a Window changes. |visible|
  // corresponds to the target visibility of the window. See
  // Window::TargetVisibility() for details.
  virtual void OnWindowTargetVisibilityChanged(bool visible) = 0;

  // Called from Window::HitTest to check if the window has a custom hit test
  // mask. It works similar to the views counterparts. That is, if the function
  // returns true, GetHitTestMask below will be called to get the mask.
  // Otherwise, Window will hit-test against its bounds.
  virtual bool HasHitTestMask() const = 0;

  // Called from Window::HitTest to retrieve hit test mask when HasHitTestMask
  // above returns true.
  virtual void GetHitTestMask(gfx::Path* mask) const = 0;

 protected:
  virtual ~WindowDelegate() {}

  virtual ui::EventResult OnKeyEvent(ui::EventTarget* target,
                                     ui::KeyEvent* event) OVERRIDE;
  virtual ui::EventResult OnMouseEvent(ui::EventTarget* target,
                                       ui::MouseEvent* event) OVERRIDE;
  virtual ui::EventResult OnScrollEvent(ui::EventTarget* target,
                                        ui::ScrollEvent* event) OVERRIDE;
  virtual ui::TouchStatus OnTouchEvent(ui::EventTarget* target,
                                       ui::TouchEvent* event) OVERRIDE;
  virtual ui::EventResult OnGestureEvent(ui::EventTarget* target,
                                         ui::GestureEvent* event) OVERRIDE;
};

}  // namespace aura

#endif  // UI_AURA_WINDOW_DELEGATE_H_
