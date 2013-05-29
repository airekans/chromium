// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/immersive_mode_controller_ash.h"

#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "ash/wm/window_properties.h"
#include "base/command_line.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/immersive_fullscreen_configuration.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/transform.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

using views::View;

namespace {

// The slide open/closed animation looks better if it starts and ends just a
// few pixels before the view goes completely off the screen, which reduces
// the visual "pop" as the 2-pixel tall immersive-style tabs become visible.
const int kAnimationOffsetY = 3;

// Duration for the reveal show/hide slide animation. The slower duration is
// used for the initial slide out to give the user more change to see what
// happened.
const int kRevealSlowAnimationDurationMs = 400;
const int kRevealFastAnimationDurationMs = 200;

// How many pixels a gesture can start away from the TopContainerView when in
// closed state and still be considered near it. This is needed to overcome
// issues with poor location values near the edge of the display.
const int kNearTopContainerDistance = 5;

// Used to multiply x value of an update in check to determine if gesture is
// vertical. This is used to make sure that gesture is close to vertical instead
// of just more vertical then horizontal.
const int kSwipeVerticalThresholdMultiplier = 3;

// If |hovered| is true, moves the mouse above |view|. Moves it outside of
// |view| otherwise.
// Should not be called outside of tests.
void MoveMouse(views::View* view, bool hovered) {
  gfx::Point cursor_pos;
  if (!hovered) {
    int bottom_edge = view->bounds().bottom();
    cursor_pos = gfx::Point(0, bottom_edge + 100);
  }
  views::View::ConvertPointToScreen(view, &cursor_pos);
  aura::Env::GetInstance()->set_last_mouse_location(cursor_pos);
}

// Returns true if the currently active window is a transient child of
// |toplevel|.
bool IsActiveWindowTransientChildOf(aura::Window* toplevel) {
  if (!toplevel)
    return false;

  aura::Window* active_window = aura::client::GetActivationClient(
      toplevel->GetRootWindow())->GetActiveWindow();

  if (!active_window)
    return false;

  for (aura::Window* window = active_window; window;
       window = window->transient_parent()) {
    if (window == toplevel)
      return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////

class RevealedLockAsh : public ImmersiveRevealedLock {
 public:
  RevealedLockAsh(const base::WeakPtr<ImmersiveModeControllerAsh>& controller,
                  ImmersiveModeController::AnimateReveal animate_reveal)
      : controller_(controller) {
    DCHECK(controller_);
    controller_->LockRevealedState(animate_reveal);
  }

  virtual ~RevealedLockAsh() {
    if (controller_)
      controller_->UnlockRevealedState();
  }

 private:
  base::WeakPtr<ImmersiveModeControllerAsh> controller_;

  DISALLOW_COPY_AND_ASSIGN(RevealedLockAsh);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////

// Manages widgets which should move in sync with the top-of-window views.
class ImmersiveModeControllerAsh::AnchoredWidgetManager
    : public views::WidgetObserver {
 public:
  explicit AnchoredWidgetManager(ImmersiveModeControllerAsh* controller);
  virtual ~AnchoredWidgetManager();

  // Anchors |widget| such that it stays |y_offset| below the top-of-window
  // views. |widget| will be repositioned whenever the top-of-window views are
  // animated (top-of-window views revealing / unrevealing) or the top-of-window
  // bounds change (eg the bookmark bar is shown).
  // If the top-of-window views are revealed (or become revealed), |widget| will
  // keep the top-of-window views revealed till |widget| is hidden or
  // RemoveAnchoredWidget() is called.
  void AddAnchoredWidget(views::Widget* widget, int y_offset);

  // Stops managing |widget|'s y position.
  // Closes the top-of-window views if no locks or other anchored widgets are
  // keeping the top-of-window views revealed.
  void RemoveAnchoredWidget(views::Widget* widget);

  // Repositions the anchored widgets for the current top container bounds if
  // immersive mode is enabled.
  void MaybeRepositionAnchoredWidgets();

  // Called when immersive mode has been enabled.
  void OnImmersiveModeEnabled();

  const std::set<views::Widget*>& visible_anchored_widgets() const {
    return visible_;
  }

 private:
  // Updates |revealed_lock_| based on the visible anchored widgets.
  void UpdateRevealedLock();

  // Updates the y position of |widget| given |y_offset| and the top
  // container's target bounds.
  void UpdateWidgetBounds(views::Widget* widget, int y_offset);

  // views::WidgetObserver overrides:
  virtual void OnWidgetDestroying(views::Widget* widget) OVERRIDE;
  virtual void OnWidgetVisibilityChanged(views::Widget* widget,
                                         bool visible) OVERRIDE;

  ImmersiveModeControllerAsh* controller_;

  // Mapping of anchored widgets to the y offset below the top-of-window views
  // that they should be positioned at.
  std::map<views::Widget*, int> widgets_;

  // The subset of |widgets_| which are visible.
  std::set<views::Widget*> visible_;

  // Lock which keeps the top-of-window views revealed based on the visible
  // anchored widgets.
  scoped_ptr<ImmersiveRevealedLock> revealed_lock_;

  DISALLOW_COPY_AND_ASSIGN(AnchoredWidgetManager);
};

ImmersiveModeControllerAsh::AnchoredWidgetManager::AnchoredWidgetManager(
    ImmersiveModeControllerAsh* controller)
    : controller_(controller) {
}

ImmersiveModeControllerAsh::AnchoredWidgetManager::~AnchoredWidgetManager() {
  for (std::map<views::Widget*, int>::iterator it = widgets_.begin();
       it != widgets_.end(); ++it) {
    RemoveAnchoredWidget(it->first);
  }
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::AddAnchoredWidget(
    views::Widget* widget,
    int y_offset) {
  DCHECK(widget);
  bool already_added = widgets_.count(widget) > 0;
  widgets_[widget] = y_offset;

  if (already_added)
    return;

  widget->AddObserver(this);

  if (widget->IsVisible())
    visible_.insert(widget);

  UpdateRevealedLock();
  UpdateWidgetBounds(widget, y_offset);
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::RemoveAnchoredWidget(
    views::Widget* widget) {
  if (!widgets_.count(widget))
    return;

  widget->RemoveObserver(this);
  widgets_.erase(widget);
  visible_.erase(widget);

  UpdateRevealedLock();
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::
    MaybeRepositionAnchoredWidgets() {
  for (std::map<views::Widget*, int>::iterator it = widgets_.begin();
       it != widgets_.end(); ++it) {
    UpdateWidgetBounds(it->first, it->second);
  }

  UpdateRevealedLock();
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::
    OnImmersiveModeEnabled() {
  UpdateRevealedLock();
  // The top container bounds may have changed while immersive mode was
  // disabled.
  MaybeRepositionAnchoredWidgets();
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::UpdateRevealedLock() {
  if (visible_.empty()) {
    revealed_lock_.reset();
  } else if (controller_->IsRevealed()) {
    // It is hard to determine the required initial transforms and the required
    // durations of the animations of |visible_| such that they appear to be
    // anchored to the top-of-window views while the top-of-window views are
    // animating. Skip to the end of the reveal animation instead.
    // We do not query the controller's reveal state because we may be called
    // as a result of LayoutBrowserRootView() in MaybeStartReveal() when
    // |reveal_state_| is SLIDING_OPEN but no animation is running yet.
    ui::Layer* top_container_layer = controller_->top_container_->layer();
    if (top_container_layer &&
        top_container_layer->GetAnimator()->is_animating()) {
      controller_->MaybeRevealWithoutAnimation();
    }

    if (!revealed_lock_.get()) {
      revealed_lock_.reset(controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_YES));
    }
  }
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::UpdateWidgetBounds(
    views::Widget* widget,
    int y_offset) {
  if (!controller_->IsEnabled() || !widget->IsVisible())
    return;

  gfx::Rect top_container_target_bounds =
      controller_->top_container_->GetTargetBoundsInScreen();
  gfx::Rect bounds(widget->GetWindowBoundsInScreen());
  bounds.set_y(
      top_container_target_bounds.bottom() + y_offset);
   widget->SetBounds(bounds);
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::OnWidgetDestroying(
    views::Widget* widget) {
  RemoveAnchoredWidget(widget);
}

void ImmersiveModeControllerAsh::AnchoredWidgetManager::
    OnWidgetVisibilityChanged(
        views::Widget* widget,
        bool visible) {
  if (visible)
    visible_.insert(widget);
  else
    visible_.erase(widget);

  UpdateRevealedLock();

  std::map<views::Widget*, int>::iterator it = widgets_.find(widget);
  DCHECK(it != widgets_.end());
  UpdateWidgetBounds(it->first, it->second);
}

////////////////////////////////////////////////////////////////////////////////

ImmersiveModeControllerAsh::ImmersiveModeControllerAsh()
    : delegate_(NULL),
      widget_(NULL),
      top_container_(NULL),
      observers_enabled_(false),
      enabled_(false),
      reveal_state_(CLOSED),
      revealed_lock_count_(0),
      tab_indicator_visibility_(TAB_INDICATORS_HIDE),
      mouse_x_when_hit_top_(-1),
      native_window_(NULL),
      weak_ptr_factory_(this),
      gesture_begun_(false) {
}

ImmersiveModeControllerAsh::~ImmersiveModeControllerAsh() {
  // The browser view is being destroyed so there's no need to update its
  // layout or layers, even if the top views are revealed. But the window
  // observers still need to be removed.
  EnableWindowObservers(false);
}

void ImmersiveModeControllerAsh::LockRevealedState(
      AnimateReveal animate_reveal) {
  ++revealed_lock_count_;
  Animate animate = (animate_reveal == ANIMATE_REVEAL_YES) ?
      ANIMATE_FAST : ANIMATE_NO;
  MaybeStartReveal(animate);
}

void ImmersiveModeControllerAsh::UnlockRevealedState() {
  --revealed_lock_count_;
  DCHECK_GE(revealed_lock_count_, 0);
  if (revealed_lock_count_ == 0) {
    // Always animate ending the reveal fast.
    MaybeEndReveal(ANIMATE_FAST);
  }
}

void ImmersiveModeControllerAsh::MaybeRevealWithoutAnimation() {
  MaybeStartReveal(ANIMATE_NO);
}

void ImmersiveModeControllerAsh::Init(
    Delegate* delegate,
    views::Widget* widget,
    TopContainerView* top_container) {
  delegate_ = delegate;
  widget_ = widget;
  // Browser view is detached from its widget during destruction. Cache the
  // window pointer so |this| can stop observing during destruction.
  native_window_ = widget_->GetNativeWindow();
  top_container_ = top_container;

  // Optionally allow the tab indicators to be hidden.
  if (CommandLine::ForCurrentProcess()->
          HasSwitch(ash::switches::kAshImmersiveHideTabIndicators)) {
    tab_indicator_visibility_ = TAB_INDICATORS_FORCE_HIDE;
  }

  anchored_widget_manager_.reset(new AnchoredWidgetManager(this));
}

void ImmersiveModeControllerAsh::SetEnabled(bool enabled) {
  DCHECK(native_window_) << "Must initialize before enabling";
  if (enabled_ == enabled)
    return;
  enabled_ = enabled;

  // Delay the initialization of the window observers till the first call to
  // SetEnabled(true) because FullscreenController is not yet initialized when
  // Init() is called.
  EnableWindowObservers(true);

  UpdateUseMinimalChrome(LAYOUT_NO);

  if (enabled_) {
    // Animate enabling immersive mode by sliding out the top-of-window views.
    // No animation occurs if a lock is holding the top-of-window views open.

    // Do a reveal to set the initial state for the animation. (And any
    // required state in case the animation cannot run because of a lock holding
    // the top-of-window views open.) This call has the side effect of relaying
    // out |browser_view_|'s root view.
    MaybeStartReveal(ANIMATE_NO);

    // Reset the mouse and the focus revealed locks so that they do not affect
    // whether the top-of-window views are hidden.
    mouse_revealed_lock_.reset();
    focus_revealed_lock_.reset();

    // Try doing the animation.
    MaybeEndReveal(ANIMATE_SLOW);

    if (reveal_state_ == REVEALED) {
      // Reveal was unsuccessful. Reacquire the revealed locks if appropriate.
      UpdateMouseRevealedLock(true, ui::ET_UNKNOWN);
      UpdateFocusRevealedLock();
    }
    anchored_widget_manager_->OnImmersiveModeEnabled();
  } else {
    // Stop cursor-at-top tracking.
    top_edge_hover_timer_.Stop();
    // Snap immediately to the closed state.
    reveal_state_ = CLOSED;
    EnablePaintToLayer(false);
    delegate_->SetImmersiveStyle(false);

    // Relayout the root view because disabling immersive fullscreen may have
    // changed the result of NonClientFrameView::GetBoundsForClientView().
    LayoutBrowserRootView();
  }
}

bool ImmersiveModeControllerAsh::IsEnabled() const {
  return enabled_;
}

bool ImmersiveModeControllerAsh::ShouldHideTabIndicators() const {
  return tab_indicator_visibility_ != TAB_INDICATORS_SHOW;
}

bool ImmersiveModeControllerAsh::ShouldHideTopViews() const {
  return enabled_ && reveal_state_ == CLOSED;
}

bool ImmersiveModeControllerAsh::IsRevealed() const {
  return enabled_ && reveal_state_ != CLOSED;
}

void ImmersiveModeControllerAsh::MaybeStackViewAtTop() {
  if (enabled_ && reveal_state_ != CLOSED) {
    ui::Layer* reveal_layer = top_container_->layer();
    if (reveal_layer)
      reveal_layer->parent()->StackAtTop(reveal_layer);
  }
}

ImmersiveRevealedLock* ImmersiveModeControllerAsh::GetRevealedLock(
    AnimateReveal animate_reveal) {
  return new RevealedLockAsh(weak_ptr_factory_.GetWeakPtr(), animate_reveal);
}

void ImmersiveModeControllerAsh::AnchorWidgetToTopContainer(
    views::Widget* widget,
    int y_offset) {
  anchored_widget_manager_->AddAnchoredWidget(widget, y_offset);
}

void ImmersiveModeControllerAsh::UnanchorWidgetFromTopContainer(
    views::Widget* widget) {
  anchored_widget_manager_->RemoveAnchoredWidget(widget);
}

void ImmersiveModeControllerAsh::OnTopContainerBoundsChanged() {
  anchored_widget_manager_->MaybeRepositionAnchoredWidgets();
}

////////////////////////////////////////////////////////////////////////////////
// Observers:

void ImmersiveModeControllerAsh::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_FULLSCREEN_CHANGED, type);
  if (enabled_)
    UpdateUseMinimalChrome(LAYOUT_YES);
}

void ImmersiveModeControllerAsh::OnMouseEvent(ui::MouseEvent* event) {
  if (!enabled_)
    return;

  if (event->flags() & ui::EF_IS_SYNTHESIZED)
    return;

  // Handle ET_MOUSE_PRESSED and ET_MOUSE_RELEASED so that we get the updated
  // mouse position ASAP once a nested message loop finishes running.
  if (event->type() != ui::ET_MOUSE_MOVED &&
      event->type() != ui::ET_MOUSE_PRESSED &&
      event->type() != ui::ET_MOUSE_RELEASED) {
    return;
  }

  // Mouse hover should not initiate revealing the top-of-window views while
  // |native_window_| is inactive.
  if (!views::Widget::GetWidgetForNativeWindow(native_window_)->IsActive())
    return;

  // Mouse hover might trigger a reveal if the cursor pauses at the top of the
  // screen for a while.
  UpdateTopEdgeHoverTimer(event);

  UpdateMouseRevealedLock(false, event->type());
  // Pass along event for further handling.
}

void ImmersiveModeControllerAsh::OnGestureEvent(ui::GestureEvent* event) {
  if (!enabled_)
    return;

  // Touch gestures should not initiate revealing the top-of-window views while
  // |native_window_| is inactive.
  if (!views::Widget::GetWidgetForNativeWindow(native_window_)->IsActive())
    return;

  switch (event->type()) {
    case ui::ET_GESTURE_SCROLL_BEGIN:
      if (ShouldHandleEvent(event->location())) {
        gesture_begun_ = true;
        event->SetHandled();
      }
      break;
    case ui::ET_GESTURE_SCROLL_UPDATE:
      if (gesture_begun_) {
        SwipeType swipe_type = GetSwipeType(event);
        if ((reveal_state_ == SLIDING_CLOSED || reveal_state_ == CLOSED) &&
            swipe_type == SWIPE_OPEN) {
          delegate_->FocusLocationBar();
          event->SetHandled();
        } else if ((reveal_state_ == SLIDING_OPEN ||
                    reveal_state_ == REVEALED) &&
                   swipe_type == SWIPE_CLOSE) {
          views::FocusManager* focus_manager = widget_->GetFocusManager();
          DCHECK(focus_manager);
          focus_manager->ClearFocus();
          event->SetHandled();
        }
        gesture_begun_ = false;
      }
      break;
    case ui::ET_GESTURE_SCROLL_END:
    case ui::ET_SCROLL_FLING_START:
      gesture_begun_ = false;
      break;
    default:
      break;
  }
}

void ImmersiveModeControllerAsh::OnWillChangeFocus(views::View* focused_before,
                                                   views::View* focused_now) {
}

void ImmersiveModeControllerAsh::OnDidChangeFocus(views::View* focused_before,
                                                  views::View* focused_now) {
  UpdateMouseRevealedLock(true, ui::ET_UNKNOWN);
  UpdateFocusRevealedLock();
}

void ImmersiveModeControllerAsh::OnWidgetDestroying(views::Widget* widget) {
  EnableWindowObservers(false);
  native_window_ = NULL;

  // Set |enabled_| to false such that any calls to MaybeStartReveal() and
  // MaybeEndReveal() have no effect.
  enabled_ = false;
}

void ImmersiveModeControllerAsh::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  // Mouse hover should not initiate revealing the top-of-window views while
  // |native_window_| is inactive.
  top_edge_hover_timer_.Stop();

  UpdateMouseRevealedLock(true, ui::ET_UNKNOWN);
  UpdateFocusRevealedLock();
}

////////////////////////////////////////////////////////////////////////////////
// Animation observer:

void ImmersiveModeControllerAsh::OnImplicitAnimationsCompleted() {
  if (reveal_state_ == SLIDING_OPEN)
    OnSlideOpenAnimationCompleted();
  else if (reveal_state_ == SLIDING_CLOSED)
    OnSlideClosedAnimationCompleted();
}

////////////////////////////////////////////////////////////////////////////////
// aura::WindowObserver overrides:
void ImmersiveModeControllerAsh::OnWindowPropertyChanged(aura::Window* window,
                                                         const void* key,
                                                         intptr_t old) {
  using aura::client::kShowStateKey;
  if (key == kShowStateKey) {
    // Disable immersive mode when leaving the fullscreen state.
    ui::WindowShowState show_state = static_cast<ui::WindowShowState>(
        window->GetProperty(kShowStateKey));
    if (IsEnabled() &&
        show_state != ui::SHOW_STATE_FULLSCREEN &&
        show_state != ui::SHOW_STATE_MINIMIZED) {
      delegate_->FullscreenStateChanged();
    }
  }
}

void ImmersiveModeControllerAsh::OnWindowAddedToRootWindow(
    aura::Window* window) {
  DCHECK_EQ(window, native_window_);
  UpdatePreTargetHandler();
}

void ImmersiveModeControllerAsh::OnWindowRemovingFromRootWindow(
    aura::Window* window) {
  DCHECK_EQ(window, native_window_);
  UpdatePreTargetHandler();
}

////////////////////////////////////////////////////////////////////////////////
// Testing interface:

void ImmersiveModeControllerAsh::SetForceHideTabIndicatorsForTest(bool force) {
  if (force)
    tab_indicator_visibility_ = TAB_INDICATORS_FORCE_HIDE;
  else if (tab_indicator_visibility_ == TAB_INDICATORS_FORCE_HIDE)
    tab_indicator_visibility_ = TAB_INDICATORS_HIDE;
  UpdateUseMinimalChrome(LAYOUT_YES);
}

void ImmersiveModeControllerAsh::StartRevealForTest(bool hovered) {
  MaybeStartReveal(ANIMATE_NO);
  MoveMouse(top_container_, hovered);
  UpdateMouseRevealedLock(false, ui::ET_UNKNOWN);
}

void ImmersiveModeControllerAsh::SetMouseHoveredForTest(bool hovered) {
  MoveMouse(top_container_, hovered);
  UpdateMouseRevealedLock(false, ui::ET_UNKNOWN);
}

////////////////////////////////////////////////////////////////////////////////
// private:

void ImmersiveModeControllerAsh::EnableWindowObservers(bool enable) {
  if (observers_enabled_ == enable)
    return;
  observers_enabled_ = enable;

  if (!native_window_) {
    NOTREACHED() << "ImmersiveModeControllerAsh not initialized";
    return;
  }

  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(native_window_);
  views::FocusManager* focus_manager = widget->GetFocusManager();
  if (enable) {
    widget->AddObserver(this);
    focus_manager->AddFocusChangeListener(this);
  } else {
    widget->RemoveObserver(this);
    focus_manager->RemoveFocusChangeListener(this);
  }

  UpdatePreTargetHandler();

  if (enable) {
    native_window_->AddObserver(this);
  } else {
    native_window_->RemoveObserver(this);
  }

  if (enable) {
    registrar_.Add(
        this,
        chrome::NOTIFICATION_FULLSCREEN_CHANGED,
        content::Source<FullscreenController>(
            delegate_->GetFullscreenController()));
  } else {
    registrar_.Remove(
        this,
        chrome::NOTIFICATION_FULLSCREEN_CHANGED,
        content::Source<FullscreenController>(
            delegate_->GetFullscreenController()));
  }

  if (!enable)
    StopObservingImplicitAnimations();
}

void ImmersiveModeControllerAsh::UpdateTopEdgeHoverTimer(
    ui::MouseEvent* event) {
  DCHECK(enabled_);
  // If the top-of-window views are already revealed or the cursor left the
  // top edge we don't need to trigger based on a timer anymore.
  if (reveal_state_ == SLIDING_OPEN ||
      reveal_state_ == REVEALED ||
      event->root_location().y() != 0) {
    top_edge_hover_timer_.Stop();
    return;
  }
  // The cursor is now at the top of the screen. Consider the cursor "not
  // moving" even if it moves a little bit in x, because users don't have
  // perfect pointing precision.
  int mouse_x = event->root_location().x();
  if (top_edge_hover_timer_.IsRunning() &&
      abs(mouse_x - mouse_x_when_hit_top_) <=
          ImmersiveFullscreenConfiguration::
              immersive_mode_reveal_x_threshold_pixels())
    return;

  // Start the reveal if the cursor doesn't move for some amount of time.
  mouse_x_when_hit_top_ = mouse_x;
  top_edge_hover_timer_.Stop();
  // Timer is stopped when |this| is destroyed, hence Unretained() is safe.
  top_edge_hover_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromMilliseconds(
          ImmersiveFullscreenConfiguration::immersive_mode_reveal_delay_ms()),
      base::Bind(&ImmersiveModeControllerAsh::AcquireMouseRevealedLock,
                 base::Unretained(this)));
}

void ImmersiveModeControllerAsh::UpdateMouseRevealedLock(
    bool maybe_drag,
    ui::EventType event_type) {
  if (!enabled_)
    return;

  // Hover cannot initiate a reveal when the top-of-window views are sliding
  // closed or are closed. (With the exception of hovering at y = 0 which is
  // handled in OnMouseEvent() ).
  if (reveal_state_ == SLIDING_CLOSED || reveal_state_ == CLOSED)
    return;

  // Mouse hover should not keep the top-of-window views revealed if
  // |native_window_| is not active.
  if (!views::Widget::GetWidgetForNativeWindow(native_window_)->IsActive()) {
    mouse_revealed_lock_.reset();
    return;
  }

  // If a window has capture, we may be in the middle of a drag. Delay updating
  // the revealed lock till we get more specifics via OnMouseEvent().
  if (maybe_drag && aura::client::GetCaptureWindow(native_window_))
    return;

  gfx::Point cursor_pos = gfx::Screen::GetScreenFor(
      native_window_)->GetCursorScreenPoint();
  // Transform to the parent of |top_container|. This avoids problems with
  // coordinate conversion while |top_container|'s layer has an animating
  // transform and also works properly if |top_container| is not at 0, 0.
  views::View::ConvertPointFromScreen(top_container_->parent(), &cursor_pos);
  // Allow the cursor to move slightly below the top container's bottom edge
  // before sliding closed. This helps when the user is attempting to click on
  // the bookmark bar and overshoots slightly.
  gfx::Rect hover_bounds = top_container_->bounds();
  if (event_type == ui::ET_MOUSE_MOVED) {
    const int kBoundsOffsetY = 8;
    hover_bounds.Inset(0, -kBoundsOffsetY);
  }
  if (hover_bounds.Contains(cursor_pos))
    AcquireMouseRevealedLock();
  else
    mouse_revealed_lock_.reset();
}

void ImmersiveModeControllerAsh::AcquireMouseRevealedLock() {
  if (!mouse_revealed_lock_.get())
    mouse_revealed_lock_.reset(GetRevealedLock(ANIMATE_REVEAL_YES));
}

void ImmersiveModeControllerAsh::UpdateFocusRevealedLock() {
  if (!enabled_)
    return;

  bool hold_lock = false;
  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(native_window_);
  if (widget->IsActive()) {
    views::View* focused_view = widget->GetFocusManager()->GetFocusedView();
    if (top_container_->Contains(focused_view))
      hold_lock = true;
  } else {
    // If the currently active window is not |native_window_|, the top-of-window
    // views should be revealed if:
    // 1) The newly active window is a transient child of |native_window_|.
    // 2) The top-of-window views are already revealed. This restriction
    //    prevents a transient window opened by the web contents while the
    //    top-of-window views are hidden from from initiating a reveal.
    // The top-of-window views will stay revealed till |native_window_| is
    // reactivated.
    if (IsRevealed() && IsActiveWindowTransientChildOf(native_window_))
      hold_lock = true;
  }

  if (hold_lock) {
    if (!focus_revealed_lock_.get())
      focus_revealed_lock_.reset(GetRevealedLock(ANIMATE_REVEAL_YES));
  } else {
    focus_revealed_lock_.reset();
  }
}

void ImmersiveModeControllerAsh::UpdateUseMinimalChrome(Layout layout) {
  // May be NULL in tests.
  FullscreenController* fullscreen_controller =
      delegate_->GetFullscreenController();
  bool in_tab_fullscreen = fullscreen_controller ?
      fullscreen_controller->IsFullscreenForTabOrPending() : false;
  bool use_minimal_chrome = !in_tab_fullscreen && enabled_;
  native_window_->SetProperty(ash::internal::kFullscreenUsesMinimalChromeKey,
                              use_minimal_chrome);

  TabIndicatorVisibility previous_tab_indicator_visibility =
      tab_indicator_visibility_;
  if (tab_indicator_visibility_ != TAB_INDICATORS_FORCE_HIDE) {
    tab_indicator_visibility_ = use_minimal_chrome ?
        TAB_INDICATORS_SHOW : TAB_INDICATORS_HIDE;
  }

  // Ash on Windows may not have a shell.
  if (ash::Shell::HasInstance()) {
    // When using minimal chrome, the shelf is auto-hidden. The auto-hidden
    // shelf displays a 3px 'light bar' when it is closed.
    ash::Shell::GetInstance()->UpdateShelfVisibility();
  }

  if (tab_indicator_visibility_ != previous_tab_indicator_visibility) {
    // If the top-of-window views are revealed or animating, the change will
    // take effect with the layout once the top-of-window views are closed.
    if (layout == LAYOUT_YES && reveal_state_ == CLOSED)
      LayoutBrowserRootView();
  }
}

int ImmersiveModeControllerAsh::GetAnimationDuration(Animate animate) const {
  switch (animate) {
    case ANIMATE_NO:
      return 0;
    case ANIMATE_SLOW:
      return kRevealSlowAnimationDurationMs;
    case ANIMATE_FAST:
      return kRevealFastAnimationDurationMs;
  }
  NOTREACHED();
  return 0;
}

void ImmersiveModeControllerAsh::MaybeStartReveal(Animate animate) {
  if (!enabled_)
    return;

  // Callers with ANIMATE_NO expect this function to synchronously reveal the
  // top-of-window views. In particular, this property is used to terminate the
  // reveal animation if an equivalent animation for the anchored widgets
  // cannot be created.
  if (reveal_state_ == REVEALED ||
      (reveal_state_ == SLIDING_OPEN && animate != ANIMATE_NO)) {
    return;
  }

  RevealState previous_reveal_state = reveal_state_;
  reveal_state_ = SLIDING_OPEN;
  if (previous_reveal_state == CLOSED) {
    // Turn on layer painting so we can smoothly animate.
    EnablePaintToLayer(true);

    // Ensure window caption buttons are updated and the view bounds are
    // computed at normal (non-immersive-style) size.
    delegate_->SetImmersiveStyle(false);
    LayoutBrowserRootView();

    // Do not do any more processing if LayoutBrowserView() changed
    // |reveal_state_|.
    if (reveal_state_ != SLIDING_OPEN)
      return;

    if (animate != ANIMATE_NO) {
      // Now that we have a layer, move it to the initial offscreen position.
      ui::Layer* layer = top_container_->layer();
      gfx::Transform transform;
      transform.Translate(0, -layer->bounds().height() + kAnimationOffsetY);
      layer->SetTransform(transform);

      typedef std::set<views::Widget*> WidgetSet;
      const WidgetSet& visible_widgets =
          anchored_widget_manager_->visible_anchored_widgets();
      for (WidgetSet::const_iterator it = visible_widgets.begin();
           it != visible_widgets.end(); ++it) {
        (*it)->GetNativeWindow()->SetTransform(transform);
      }
    }
  }
  // Slide in the reveal view.
  DoAnimation(gfx::Transform(), GetAnimationDuration(animate));
}

void ImmersiveModeControllerAsh::EnablePaintToLayer(bool enable) {
  top_container_->SetPaintToLayer(enable);

  // Views software compositing is not fully layer aware. If the bookmark bar
  // is detached while the top container layer slides on or off the screen,
  // the pixels that become exposed are the remnants of the last software
  // composite of the BrowserView, not the freshly-exposed bookmark bar.
  // Force the bookmark bar to paint to a layer so the views composite
  // properly. The infobar container does not need this treatment because
  // BrowserView::PaintChildren() always draws it last when it is visible.
  BookmarkBarView* bookmark_bar = delegate_->GetBookmarkBar();
  if (!bookmark_bar)
    return;
  if (enable && bookmark_bar->IsDetached())
    bookmark_bar->SetPaintToLayer(true);
  else
    bookmark_bar->SetPaintToLayer(false);
}

void ImmersiveModeControllerAsh::LayoutBrowserRootView() {
  // Update the window caption buttons.
  widget_->non_client_view()->frame_view()->ResetWindowControls();
  // Layout all views, including BrowserView.
  widget_->GetRootView()->Layout();
}

void ImmersiveModeControllerAsh::OnSlideOpenAnimationCompleted() {
  DCHECK_EQ(SLIDING_OPEN, reveal_state_);
  reveal_state_ = REVEALED;

  // The user may not have moved the mouse since the reveal was initiated.
  // Update the revealed lock to reflect the mouse's current state.
  UpdateMouseRevealedLock(true, ui::ET_UNKNOWN);
}

void ImmersiveModeControllerAsh::MaybeEndReveal(Animate animate) {
  if (!enabled_ || revealed_lock_count_ != 0)
    return;

  // Callers with ANIMATE_NO expect this function to synchronously close the
  // top-of-window views.
  if (reveal_state_ == CLOSED ||
      (reveal_state_ == SLIDING_CLOSED && animate != ANIMATE_NO)) {
    return;
  }

  // Visible anchored widgets keep the top-of-window views revealed.
  DCHECK(anchored_widget_manager_->visible_anchored_widgets().empty());

  reveal_state_ = SLIDING_CLOSED;
  int duration_ms = GetAnimationDuration(animate);
  if (duration_ms > 0) {
    // The bookmark bar may have become detached during the reveal so ensure
    // layers are available. This is a no-op for the top container.
    EnablePaintToLayer(true);

    ui::Layer* top_container_layer = top_container_->layer();
    gfx::Transform target_transform;
    target_transform.Translate(0,
        -top_container_layer->bounds().height() + kAnimationOffsetY);

    DoAnimation(target_transform, duration_ms);
  } else {
    OnSlideClosedAnimationCompleted();
  }
}

void ImmersiveModeControllerAsh::OnSlideClosedAnimationCompleted() {
  DCHECK_EQ(SLIDING_CLOSED, reveal_state_);
  reveal_state_ = CLOSED;
  // Layers aren't needed after animation completes.
  EnablePaintToLayer(false);
  // Update tabstrip for closed state.
  delegate_->SetImmersiveStyle(true);
  LayoutBrowserRootView();
}

void ImmersiveModeControllerAsh::DoAnimation(
    const gfx::Transform& target_transform,
    int duration_ms) {
  StopObservingImplicitAnimations();
  DoLayerAnimation(
      top_container_->layer(), target_transform, duration_ms, this);

  typedef std::set<views::Widget*> WidgetSet;
  const WidgetSet& visible_widgets =
      anchored_widget_manager_->visible_anchored_widgets();
  for (WidgetSet::const_iterator it = visible_widgets.begin();
       it != visible_widgets.end(); ++it) {
    // The anchored widget's bounds are set to the target bounds right when the
    // animation starts. The transform is used to animate the widget's position.
    // Using the target bounds allows us to "stay anchored" if other code
    // changes the widget bounds in the middle of the animation. (This is the
    // case if the fullscreen exit bubble type is changed during the immersive
    // reveal animation).
    DoLayerAnimation((*it)->GetNativeWindow()->layer(), gfx::Transform(),
        duration_ms, NULL);
  }
}

void ImmersiveModeControllerAsh::DoLayerAnimation(
    ui::Layer* layer,
    const gfx::Transform& target_transform,
    int duration_ms,
    ui::ImplicitAnimationObserver* observer) {
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTweenType(ui::Tween::EASE_OUT);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(duration_ms));
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  if (observer)
    settings.AddObserver(observer);
  layer->SetTransform(target_transform);
}


ImmersiveModeControllerAsh::SwipeType ImmersiveModeControllerAsh::GetSwipeType(
    ui::GestureEvent* event) const {
  if (event->type() != ui::ET_GESTURE_SCROLL_UPDATE)
    return SWIPE_NONE;
  // Make sure that it is a clear vertical gesture.
  if (abs(event->details().scroll_y()) <=
      kSwipeVerticalThresholdMultiplier * abs(event->details().scroll_x()))
    return SWIPE_NONE;
  if (event->details().scroll_y() < 0)
    return SWIPE_CLOSE;
  else if (event->details().scroll_y() > 0)
    return SWIPE_OPEN;
  return SWIPE_NONE;
}

bool ImmersiveModeControllerAsh::ShouldHandleEvent(
    const gfx::Point& location) const {
  // All of the gestures that are of interest start in a region with left &
  // right edges agreeing with |top_container_|. When CLOSED it is difficult to
  // hit the bounds due to small size of the tab strip, so the hit target needs
  // to be extended on the bottom, thus the inset call. Finally there may be a
  // bezel sensor off screen logically above |top_container_| thus the test
  // needs to include gestures starting above.
  gfx::Rect near_bounds = top_container_->GetTargetBoundsInScreen();
  if (reveal_state_ == CLOSED)
    near_bounds.Inset(gfx::Insets(0, 0, -kNearTopContainerDistance, 0));
  return near_bounds.Contains(location) ||
      ((location.y() < near_bounds.y()) &&
       (location.x() >= near_bounds.x()) &&
       (location.x() <= near_bounds.right()));
}

void ImmersiveModeControllerAsh::UpdatePreTargetHandler() {
  if (!native_window_)
    return;
  aura::RootWindow* root_window = native_window_->GetRootWindow();
  if (!root_window)
    return;
  if (observers_enabled_)
    root_window->AddPreTargetHandler(this);
  else
    root_window->RemovePreTargetHandler(this);
}
