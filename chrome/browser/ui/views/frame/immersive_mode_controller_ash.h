// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_IMMERSIVE_MODE_CONTROLLER_ASH_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_IMMERSIVE_MODE_CONTROLLER_ASH_H_

#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"

#include "base/timer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/events/event_handler.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BookmarkBarView;
class TopContainerView;

namespace aura {
class Window;
}

namespace gfx {
class Transform;
}

namespace ui {
class Layer;
}

namespace views {
class View;
}

class ImmersiveModeControllerAsh : public ImmersiveModeController,
                                   public content::NotificationObserver,
                                   public ui::EventHandler,
                                   public ui::ImplicitAnimationObserver,
                                   public views::FocusChangeListener,
                                   public views::WidgetObserver {
 public:
  ImmersiveModeControllerAsh();
  virtual ~ImmersiveModeControllerAsh();

  // These methods are used to increment and decrement |revealed_lock_count_|.
  // If immersive mode is enabled, a transition from 1 to 0 in
  // |revealed_lock_count_| closes the top-of-window views and a transition
  // from 0 to 1 in |revealed_lock_count_| reveals the top-of-window views.
  void LockRevealedState(AnimateReveal animate_reveal);
  void UnlockRevealedState();

  // Shows the reveal view without any animations if immersive mode is enabled.
  void MaybeRevealWithoutAnimation();

  // ImmersiveModeController overrides:
  virtual void Init(Delegate* delegate,
                    views::Widget* widget,
                    TopContainerView* top_container) OVERRIDE;
  virtual void SetEnabled(bool enabled) OVERRIDE;
  virtual bool IsEnabled() const OVERRIDE;
  virtual bool ShouldHideTabIndicators() const OVERRIDE;
  virtual bool ShouldHideTopViews() const OVERRIDE;
  virtual bool IsRevealed() const OVERRIDE;
  virtual void MaybeStackViewAtTop() OVERRIDE;
  virtual ImmersiveRevealedLock* GetRevealedLock(
      AnimateReveal animate_reveal) OVERRIDE WARN_UNUSED_RESULT;
  virtual void AnchorWidgetToTopContainer(views::Widget* widget,
                                          int y_offset) OVERRIDE;
  virtual void UnanchorWidgetFromTopContainer(views::Widget* widget) OVERRIDE;
  virtual void OnTopContainerBoundsChanged() OVERRIDE;

  // content::NotificationObserver override:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // ui::EventHandler overrides:
  virtual void OnMouseEvent(ui::MouseEvent* event) OVERRIDE;
  virtual void OnGestureEvent(ui::GestureEvent* event) OVERRIDE;

  // views::FocusChangeObserver overrides:
  virtual void OnWillChangeFocus(views::View* focused_before,
                                 views::View* focused_now) OVERRIDE;
  virtual void OnDidChangeFocus(views::View* focused_before,
                                views::View* focused_now) OVERRIDE;

  // views::WidgetObserver overrides:
  virtual void OnWidgetDestroying(views::Widget* widget) OVERRIDE;
  virtual void OnWidgetActivationChanged(views::Widget* widget,
                                         bool active) OVERRIDE;

  // ui::ImplicitAnimationObserver override:
  virtual void OnImplicitAnimationsCompleted() OVERRIDE;

  // Testing interface.
  void SetForceHideTabIndicatorsForTest(bool force);
  void StartRevealForTest(bool hovered);
  void SetMouseHoveredForTest(bool hovered);

 private:
  friend class ImmersiveModeControllerAshTest;

  enum Animate {
    ANIMATE_NO,
    ANIMATE_SLOW,
    ANIMATE_FAST,
  };
  enum Layout {
    LAYOUT_YES,
    LAYOUT_NO
  };
  enum RevealState {
    CLOSED,          // Top container only showing tabstrip, y = 0.
    SLIDING_OPEN,    // All views showing, y animating from -height to 0.
    REVEALED,        // All views showing, y = 0.
    SLIDING_CLOSED,  // All views showing, y animating from 0 to -height.
  };
  enum TabIndicatorVisibility {
    TAB_INDICATORS_FORCE_HIDE,
    TAB_INDICATORS_HIDE,
    TAB_INDICATORS_SHOW
  };
  enum SwipeType {
    SWIPE_OPEN,
    SWIPE_CLOSE,
    SWIPE_NONE
  };

  // Enables or disables observers for mouse move, focus, and window restore.
  void EnableWindowObservers(bool enable);

  // Updates |top_edge_hover_timer_| based on a mouse |event|. If the mouse is
  // hovered at the top of the screen the timer is started. If the mouse moves
  // away from the top edge, or moves too much in the x direction, the timer is
  // stopped.
  void UpdateTopEdgeHoverTimer(ui::MouseEvent* event);

  // Updates |mouse_revealed_lock_| based on the current mouse state and the
  // currently active widget.
  // |maybe_drag| is true if the user may be in the middle of a drag.
  // |event_type| is the type of event that triggered the update or
  // ui::ET_UNKNOWN if the source event isn't known.
  void UpdateMouseRevealedLock(bool maybe_drag, ui::EventType event_type);

  // Acquires the mouse revealed lock if it is not already held.
  void AcquireMouseRevealedLock();

  // Updates |focus_revealed_lock_| based on the currently active view and the
  // currently active widget.
  void UpdateFocusRevealedLock();

  // Updates whether fullscreen uses any chrome at all. When using minimal
  // chrome, a 'light bar' is permanently visible for the launcher and possibly
  // for the tabstrip.
  void UpdateUseMinimalChrome(Layout layout);

  // Returns the animation duration given |animate|.
  int GetAnimationDuration(Animate animate) const;

  // Temporarily reveals the top-of-window views while in immersive mode,
  // hiding them when the cursor exits the area of the top views. If |animate|
  // is not ANIMATE_NO, slides in the view, otherwise shows it immediately.
  void MaybeStartReveal(Animate animate);

  // Enables or disables layer-based painting to allow smooth animations.
  void EnablePaintToLayer(bool enable);

  // Updates the browser root view's layout including window caption controls.
  void LayoutBrowserRootView();

  // Called when the animation to slide open the top-of-window views has
  // completed.
  void OnSlideOpenAnimationCompleted();

  // Hides the top-of-window views if immersive mode is enabled and nothing is
  // keeping them revealed. Optionally animates.
  void MaybeEndReveal(Animate animate);

  // Called when the animation to slide out the top-of-window views has
  // completed.
  void OnSlideClosedAnimationCompleted();

  // Starts an animation for the top-of-window views and any anchored widgets
  // of |duration_ms| to |target_transform|.
  void DoAnimation(const gfx::Transform& target_transform, int duration_ms);

  // Starts an animation for |layer| of |duration_ms| to |target_transform|.
  // If non-NULL, sets |observer| to be notified when the animation completes.
  void DoLayerAnimation(ui::Layer* layer,
                        const gfx::Transform& target_transform,
                        int duration_ms,
                        ui::ImplicitAnimationObserver* observer);

  // Returns the type of swipe given |event|.
  SwipeType GetSwipeType(ui::GestureEvent* event) const;

  // True when |location| is "near" to the top container. When the top container
  // is not closed "near" means within the displayed bounds. When the top
  // container is closed "near" means either within the displayed bounds or
  // within a few pixels of it. This allow the container to steal enough pixels
  // to detect a swipe in.
  bool IsNearTopContainer(gfx::Point location) const;

  // Injected dependencies. Not owned.
  Delegate* delegate_;
  views::Widget* widget_;
  TopContainerView* top_container_;

  // True if the window observers are enabled.
  bool observers_enabled_;

  // True when in immersive mode.
  bool enabled_;

  // State machine for the revealed/closed animations.
  RevealState reveal_state_;

  int revealed_lock_count_;

  // The visibility of the miniature "tab indicators" in the main browser view
  // when immersive mode is enabled and the top-of-window views are closed.
  TabIndicatorVisibility tab_indicator_visibility_;

  // Timer to track cursor being held at the top edge of the screen.
  base::OneShotTimer<ImmersiveModeController> top_edge_hover_timer_;

  // The cursor x position in root coordinates when the cursor first hit
  // the top edge of the screen.
  int mouse_x_when_hit_top_;

  // Lock which keeps the top-of-window views revealed based on the current
  // mouse state.
  scoped_ptr<ImmersiveRevealedLock> mouse_revealed_lock_;

  // Lock which keeps the top-of-window views revealed based on the focused view
  // and the active widget.
  scoped_ptr<ImmersiveRevealedLock> focus_revealed_lock_;

  // Native window for the browser, needed to clean up observers.
  aura::Window* native_window_;

  // Observer to disable immersive mode when window leaves the maximized state.
  class WindowObserver;
  scoped_ptr<WindowObserver> window_observer_;

  // Manages widgets which are anchored to the top-of-window views.
  class AnchoredWidgetManager;
  scoped_ptr<AnchoredWidgetManager> anchored_widget_manager_;

  content::NotificationRegistrar registrar_;

  base::WeakPtrFactory<ImmersiveModeControllerAsh> weak_ptr_factory_;

  // Tracks if the controller has seen a ET_GESTURE_SCROLL_BEGIN, without the
  // following events.
  bool gesture_begun_;

  DISALLOW_COPY_AND_ASSIGN(ImmersiveModeControllerAsh);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_IMMERSIVE_MODE_CONTROLLER_ASH_H_
