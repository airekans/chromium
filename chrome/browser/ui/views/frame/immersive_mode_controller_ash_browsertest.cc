// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/immersive_mode_controller_ash.h"

#include "ash/ash_switches.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_types.h"
#include "ash/shell.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/ui/app_modal_dialogs/app_modal_dialog_queue.h"
#include "chrome/browser/ui/app_modal_dialogs/javascript_dialog_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"
#include "chrome/browser/ui/fullscreen/fullscreen_controller_test.h"
#include "chrome/browser/ui/immersive_fullscreen_configuration.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/browser_dialogs.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "ui/aura/env.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/rect.h"
#include "ui/views/view.h"

using ui::ScopedAnimationDurationScaleMode;

namespace {

// Returns the bounds of |view| in widget coordinates.
gfx::Rect GetRectInWidget(views::View* view) {
  return view->ConvertRectToWidget(view->GetLocalBounds());
}

}  // namespace

// TODO(jamescook): If immersive mode becomes popular on CrOS, consider porting
// it to other Aura platforms (win_aura, linux_aura).  http://crbug.com/163931
#if defined(OS_CHROMEOS)

class ImmersiveModeControllerAshTest : public InProcessBrowserTest {
 public:
  ImmersiveModeControllerAshTest() : browser_view_(NULL), controller_(NULL) {}
  virtual ~ImmersiveModeControllerAshTest() {}

  BrowserView* browser_view() { return browser_view_; }
  ImmersiveModeControllerAsh* controller() { return controller_; }

  // Access to private data from the controller.
  bool top_edge_hover_timer_running() const {
    return controller_->top_edge_hover_timer_.IsRunning();
  }
  int mouse_x_when_hit_top() const {
    return controller_->mouse_x_when_hit_top_;
  }

  // Callback for when the onbeforeunload dialog closes for the sake of testing
  // the dialog with immersive mode.
  void OnBeforeUnloadJavaScriptDialogClosed(
      bool success,
      const string16& user_input) {
  }

  // content::BrowserTestBase overrides:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ImmersiveFullscreenConfiguration::EnableImmersiveFullscreenForTest();
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    ASSERT_TRUE(ImmersiveFullscreenConfiguration::UseImmersiveFullscreen());
    browser_view_ = static_cast<BrowserView*>(browser()->window());
    controller_ = static_cast<ImmersiveModeControllerAsh*>(
        browser_view_->immersive_mode_controller());
    zero_duration_mode_.reset(new ScopedAnimationDurationScaleMode(
        ScopedAnimationDurationScaleMode::ZERO_DURATION));
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    zero_duration_mode_.reset();
  }

 private:
  BrowserView* browser_view_;
  ImmersiveModeControllerAsh* controller_;
  scoped_ptr<ScopedAnimationDurationScaleMode> zero_duration_mode_;

  DISALLOW_COPY_AND_ASSIGN(ImmersiveModeControllerAshTest);
};

IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest, ImmersiveMode) {
  views::View* contents_view = browser_view()->GetTabContentsContainerView();

  // Immersive mode is not on by default.
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->ShouldHideTopViews());

  // Top-of-window views are visible.
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_TRUE(browser_view()->IsToolbarVisible());

  // Usual commands are enabled.
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_OPEN_CURRENT_URL));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ABOUT));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FULLSCREEN));

  // Turning immersive mode on sets the toolbar to immersive style and hides
  // the top-of-window views while leaving the tab strip visible.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_TRUE(controller()->ShouldHideTopViews());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_TRUE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_FALSE(browser_view()->IsToolbarVisible());
  // Content area is immediately below the tab indicators.
  EXPECT_EQ(GetRectInWidget(browser_view()).y() + Tab::GetImmersiveHeight(),
            GetRectInWidget(contents_view).y());

  // Commands are still enabled (usually fullscreen disables these).
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_OPEN_CURRENT_URL));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ABOUT));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FULLSCREEN));

  // Trigger a reveal keeps us in immersive mode, but top-of-window views
  // become visible.
  controller()->StartRevealForTest(true);
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->ShouldHideTopViews());
  EXPECT_TRUE(controller()->IsRevealed());
  EXPECT_FALSE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_TRUE(browser_view()->IsToolbarVisible());
  // Shelf hide triggered by enabling immersive mode eventually changes the
  // widget bounds and causes a Layout(). Force it to happen early for test.
  browser_view()->parent()->Layout();
  // Content area is still immediately below the tab indicators.
  EXPECT_EQ(GetRectInWidget(browser_view()).y() + Tab::GetImmersiveHeight(),
            GetRectInWidget(contents_view).y());

  // End reveal by moving the mouse off the top-of-window views. We
  // should stay in immersive mode, but the toolbar should go invisible.
  controller()->SetMouseHoveredForTest(false);
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_TRUE(controller()->ShouldHideTopViews());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_TRUE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_FALSE(browser_view()->IsToolbarVisible());

  // Disabling immersive mode puts us back to the beginning.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(browser_view()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->ShouldHideTopViews());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_TRUE(browser_view()->IsToolbarVisible());

  // Disabling immersive mode while we are revealed should take us back to
  // the beginning.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  controller()->StartRevealForTest(true);

  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(browser_view()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->ShouldHideTopViews());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_TRUE(browser_view()->IsToolbarVisible());

  // When hiding the tab indicators, content is at the top of the browser view
  // both before and during reveal.
  controller()->SetForceHideTabIndicatorsForTest(true);
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_FALSE(browser_view()->IsTabStripVisible());
  EXPECT_EQ(GetRectInWidget(browser_view()).y(),
            GetRectInWidget(contents_view).y());
  controller()->StartRevealForTest(true);
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  // Shelf hide triggered by enabling immersive mode eventually changes the
  // widget bounds and causes a Layout(). Force it to happen early for test.
  browser_view()->parent()->Layout();
  EXPECT_EQ(GetRectInWidget(browser_view()).y(),
            GetRectInWidget(contents_view).y());
  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(browser_view()->IsFullscreen());
  controller()->SetForceHideTabIndicatorsForTest(false);

  // Reveal ends when the mouse moves out of the reveal view.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  controller()->StartRevealForTest(true);
  controller()->SetMouseHoveredForTest(false);
  EXPECT_FALSE(controller()->IsRevealed());

  // Window restore tracking is only implemented in the Aura port.
  // Also, Windows Aura does not trigger maximize/restore notifications.
#if !defined(OS_WIN)
  // Restoring the window exits immersive mode.
  browser_view()->GetWidget()->Restore();
  ASSERT_FALSE(browser_view()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->ShouldHideTopViews());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(browser_view()->tabstrip()->IsImmersiveStyle());
  EXPECT_TRUE(browser_view()->IsTabStripVisible());
  EXPECT_TRUE(browser_view()->IsToolbarVisible());
#endif  // !defined(OS_WIN)

  // Don't crash if we exit the test during a reveal.
  if (!browser_view()->IsFullscreen())
    chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  ASSERT_TRUE(controller()->IsEnabled());
  controller()->StartRevealForTest(true);
}

// Test how focus affects whether the top-of-window views are revealed.
// Do not test under windows because focus testing is not reliable on
// Windows, crbug.com/79493
#if !defined(OS_WIN)
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest, Focus) {
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());

  // 1) Test that focusing the location bar automatically reveals the
  // top-of-window views.
  //
  // Move the mouse of the way.
  controller()->SetMouseHoveredForTest(false);
  browser_view()->SetFocusToLocationBar(false);
  EXPECT_TRUE(controller()->IsRevealed());
  browser_view()->GetFocusManager()->ClearFocus();
  EXPECT_FALSE(controller()->IsRevealed());

  // 2) Test that the top-of-window views stay revealed as long as either the
  // location bar has focus or the mouse is hovered above the top-of-window
  // views.
  controller()->StartRevealForTest(true);
  browser_view()->SetFocusToLocationBar(false);
  browser_view()->GetFocusManager()->ClearFocus();
  EXPECT_TRUE(controller()->IsRevealed());
  browser_view()->SetFocusToLocationBar(false);
  controller()->SetMouseHoveredForTest(false);
  EXPECT_TRUE(controller()->IsRevealed());
  browser_view()->GetFocusManager()->ClearFocus();
  EXPECT_FALSE(controller()->IsRevealed());

  // 3) Test that a bubble keeps the top-of-window views revealed for the
  // duration of its visibity.
  //
  // Setup so that the bookmark bubble actually shows.
  ui_test_utils::WaitForBookmarkModelToLoad(
      BookmarkModelFactory::GetForProfile(browser()->profile()));
  browser_view()->Activate();
  EXPECT_TRUE(browser_view()->IsActive());

  controller()->StartRevealForTest(false);
  chrome::ExecuteCommand(browser(), IDC_BOOKMARK_PAGE_FROM_STAR);
  EXPECT_TRUE(chrome::IsBookmarkBubbleViewShowing());
  EXPECT_TRUE(controller()->IsRevealed());
  chrome::HideBookmarkBubbleView();
  EXPECT_FALSE(controller()->IsRevealed());

  // 4) Test that focusing the web contents hides the top-of-window views.
  browser_view()->SetFocusToLocationBar(false);
  EXPECT_TRUE(controller()->IsRevealed());
  browser_view()->GetTabContentsContainerView()->RequestFocus();
  EXPECT_FALSE(controller()->IsRevealed());

  // 5) Test that a loss of focus of the location bar to the web contents
  // while immersive mode is disabled is properly registered.
  browser_view()->SetFocusToLocationBar(false);
  EXPECT_TRUE(controller()->IsRevealed());
  chrome::ToggleFullscreenMode(browser());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  browser_view()->GetTabContentsContainerView()->RequestFocus();
  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());

  // Repeat test but with a revealed lock acquired when immersive mode is
  // enabled because the code path is different.
  browser_view()->SetFocusToLocationBar(false);
  EXPECT_TRUE(controller()->IsRevealed());
  chrome::ToggleFullscreenMode(browser());
  scoped_ptr<ImmersiveRevealedLock> lock(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_FALSE(controller()->IsRevealed());
  browser_view()->GetTabContentsContainerView()->RequestFocus();
  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(controller()->IsRevealed());
  lock.reset();
  EXPECT_FALSE(controller()->IsRevealed());

  // 6) Test that a dialog opened by the web contents does not initiate a
  // reveal.
  AppModalDialogQueue* queue = AppModalDialogQueue::GetInstance();
  EXPECT_FALSE(queue->HasActiveDialog());
  content::WebContents* web_contents = browser_view()->GetActiveWebContents();
  GetJavaScriptDialogManagerInstance()->RunBeforeUnloadDialog(
      web_contents,
      string16(),
      false,
      base::Bind(
          &ImmersiveModeControllerAshTest::OnBeforeUnloadJavaScriptDialogClosed,
          base::Unretained(this)));
  EXPECT_TRUE(queue->HasActiveDialog());
  EXPECT_FALSE(controller()->IsRevealed());
}
#endif  // OS_WIN

// Test mouse event processing for top-of-screen reveal triggering.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest,
                       OnMouseEvent) {
  // Set up initial state.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());
  ASSERT_FALSE(controller()->IsRevealed());

  // Mouse wheel event does nothing.
  ui::MouseEvent wheel(
      ui::ET_MOUSEWHEEL, gfx::Point(), gfx::Point(), ui::EF_NONE);
  controller()->OnMouseEvent(&wheel);
  EXPECT_FALSE(top_edge_hover_timer_running());

  // Move to top edge of screen starts hover timer running.
  ui::MouseEvent move(
      ui::ET_MOUSE_MOVED, gfx::Point(), gfx::Point(100, 0), ui::EF_NONE);
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(top_edge_hover_timer_running());
  EXPECT_EQ(100, mouse_x_when_hit_top());

  // Moving off the top edge stops it.
  move.set_root_location(gfx::Point(100, 1));
  controller()->OnMouseEvent(&move);
  EXPECT_FALSE(top_edge_hover_timer_running());

  // Moving back to the top starts the timer again.
  move.set_root_location(gfx::Point(100, 0));
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(top_edge_hover_timer_running());
  EXPECT_EQ(100, mouse_x_when_hit_top());

  // Slight move to the right keeps the timer running for the same hit point.
  move.set_root_location(gfx::Point(101, 0));
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(top_edge_hover_timer_running());
  EXPECT_EQ(100, mouse_x_when_hit_top());

  // Moving back to the left also keeps the timer running.
  move.set_root_location(gfx::Point(100, 0));
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(top_edge_hover_timer_running());
  EXPECT_EQ(100, mouse_x_when_hit_top());

  // Large move right restarts the timer (so it is still running) and considers
  // this a new hit at the top.
  move.set_root_location(gfx::Point(150, 0));
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(top_edge_hover_timer_running());
  EXPECT_EQ(150, mouse_x_when_hit_top());

  // Moving off the top edge stops the timer, which also means we won't leak
  // a posted task from the test.
  move.set_root_location(gfx::Point(150, 1));
  controller()->OnMouseEvent(&move);
  EXPECT_FALSE(top_edge_hover_timer_running());

  // Once revealed, a move just a little below the top container doesn't end a
  // reveal.
  EXPECT_FALSE(controller()->IsRevealed());
  controller()->StartRevealForTest(true);
  gfx::Point just_below(0, browser_view()->top_container()->height() + 1);
  views::View::ConvertPointToScreen(browser_view()->top_container(),
                                    &just_below);
  aura::Env::GetInstance()->set_last_mouse_location(just_below);
  move.set_root_location(just_below);
  controller()->OnMouseEvent(&move);
  EXPECT_TRUE(controller()->IsRevealed());

  // Once revealed, clicking just below the top container ends the reveal.
  controller()->StartRevealForTest(true);
  ui::MouseEvent click(
      ui::ET_MOUSE_PRESSED, gfx::Point(), just_below, ui::EF_NONE);
  aura::Env::GetInstance()->set_last_mouse_location(just_below);
  controller()->OnMouseEvent(&click);
  EXPECT_FALSE(controller()->IsRevealed());

  // Moving a lot below the top container ends a reveal.
  controller()->StartRevealForTest(true);
  gfx::Point far_below(0, browser_view()->top_container()->height() + 50);
  views::View::ConvertPointToScreen(browser_view()->top_container(),
                                    &far_below);
  aura::Env::GetInstance()->set_last_mouse_location(far_below);
  move.set_root_location(far_below);
  controller()->OnMouseEvent(&move);
  EXPECT_FALSE(controller()->IsRevealed());
}

// Test behavior when the mouse becomes hovered without moving.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest,
                       MouseHoveredWithoutMoving) {
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());

  scoped_ptr<ImmersiveRevealedLock> lock(NULL);

  // 1) Test that if the mouse becomes hovered without the mouse moving due to a
  // lock causing the top-of-window views to be revealed (and the mouse
  // happening to be near the top of the screen), the top-of-window views do not
  // hide till the mouse moves off of the top-of-window views.
  controller()->SetMouseHoveredForTest(true);
  EXPECT_FALSE(controller()->IsRevealed());
  lock.reset(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_TRUE(controller()->IsRevealed());
  lock.reset();
  EXPECT_TRUE(controller()->IsRevealed());
  controller()->SetMouseHoveredForTest(false);
  EXPECT_FALSE(controller()->IsRevealed());

  // 2) Test that if the mouse becomes hovered without moving because of a
  // reveal in ImmersiveModeController::SetEnabled(true) and there are no locks
  // keeping the top-of-window views revealed, that mouse hover does not prevent
  // the top-of-window views from closing.
  chrome::ToggleFullscreenMode(browser());
  controller()->SetMouseHoveredForTest(true);
  EXPECT_FALSE(controller()->IsRevealed());
  chrome::ToggleFullscreenMode(browser());
  EXPECT_FALSE(controller()->IsRevealed());

  // 3) Test that if the mouse becomes hovered without moving because of a
  // reveal in ImmersiveModeController::SetEnabled(true) and there is a lock
  // keeping the top-of-window views revealed, that the top-of-window views do
  // not hide till the mouse moves off of the top-of-window views.
  chrome::ToggleFullscreenMode(browser());
  controller()->SetMouseHoveredForTest(true);
  lock.reset(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_FALSE(controller()->IsRevealed());
  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(controller()->IsRevealed());
  lock.reset();
  EXPECT_TRUE(controller()->IsRevealed());
  controller()->SetMouseHoveredForTest(false);
  EXPECT_FALSE(controller()->IsRevealed());
}

// GetRevealedLock() specific tests.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest, RevealedLock) {
  scoped_ptr<ImmersiveRevealedLock> lock1(NULL);
  scoped_ptr<ImmersiveRevealedLock> lock2(NULL);

  // Immersive mode is not on by default.
  EXPECT_FALSE(controller()->IsEnabled());

  // Move the mouse out of the way.
  controller()->SetMouseHoveredForTest(false);

  // 1) Test acquiring and releasing a revealed state lock while immersive mode
  // is disabled. Acquiring or releasing the lock should have no effect till
  // immersive mode is enabled.
  lock1.reset(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(controller()->ShouldHideTopViews());

  // Immersive mode should start in the revealed state due to the lock.
  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_TRUE(controller()->IsRevealed());
  EXPECT_FALSE(controller()->ShouldHideTopViews());

  chrome::ToggleFullscreenMode(browser());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(controller()->ShouldHideTopViews());

  lock1.reset();
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(controller()->ShouldHideTopViews());

  // Immersive mode should start in the closed state because the lock is no
  // longer held.
  chrome::ToggleFullscreenMode(browser());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_TRUE(controller()->ShouldHideTopViews());

  // 2) Test that acquiring a revealed state lock reveals the top-of-window
  // views if they are hidden.
  EXPECT_FALSE(controller()->IsRevealed());
  lock1.reset(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  EXPECT_TRUE(controller()->IsRevealed());

  // 3) Test that the top-of-window views are only hidden when all of the locks
  // are released.
  lock2.reset(controller()->GetRevealedLock(
      ImmersiveModeController::ANIMATE_REVEAL_NO));
  lock1.reset();
  EXPECT_TRUE(controller()->IsRevealed());

  lock2.reset();
  EXPECT_FALSE(controller()->IsRevealed());
}

// Test how changing the bounds of the top container repositions anchored
// widgets and how the visibility of anchored widgets affects whether the
// top-of-window views stay revealed.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest, AnchoredWidgets) {
  BookmarkBarView::DisableAnimationsForTesting(true);

  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());

  gfx::Rect kInitialBounds(100, 100, 100, 100);
  views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.parent = browser_view()->GetNativeWindow();
  params.bounds = kInitialBounds;
  views::Widget anchored_widget;
  anchored_widget.Init(params);

  // 1) Test that an anchored widget does not cause the top-of-window views to
  // reveal but instead prolongs the duration of the reveal till either the
  // widget is unanchored or is hidden.
  EXPECT_FALSE(controller()->IsRevealed());

  anchored_widget.Show();
  controller()->AnchorWidgetToTopContainer(&anchored_widget, 10);

  // Anchoring a widget should not cause the top-of-window views to reveal.
  EXPECT_FALSE(controller()->IsRevealed());

  controller()->StartRevealForTest(true);
  EXPECT_TRUE(controller()->IsRevealed());

  // Once the top-of-window views are revealed, the top-of-window views should
  // stay revealed as long as there is a visible anchored widget (or something
  // else like the mouse hover is keeping the top-of-window views revealed).
  controller()->SetMouseHoveredForTest(false);
  EXPECT_TRUE(controller()->IsRevealed());
  anchored_widget.Hide();
  EXPECT_FALSE(controller()->IsRevealed());

  anchored_widget.Show();
  EXPECT_FALSE(controller()->IsRevealed());
  controller()->StartRevealForTest(true);
  EXPECT_TRUE(controller()->IsRevealed());

  controller()->UnanchorWidgetFromTopContainer(&anchored_widget);
  EXPECT_TRUE(controller()->IsRevealed());
  controller()->SetMouseHoveredForTest(false);
  EXPECT_FALSE(controller()->IsRevealed());

  // 2) Test that the anchored widget is repositioned to |y_offset| below
  // the bottom of the top container when the top container bounds are changed.
  //
  // Make sure that the bookmark bar is hidden.
  ui_test_utils::WaitForBookmarkModelToLoad(
      BookmarkModelFactory::GetForProfile(browser()->profile()));
  if (browser_view()->IsBookmarkBarVisible())
    chrome::ExecuteCommand(browser(), IDC_SHOW_BOOKMARK_BAR);
  EXPECT_FALSE(browser_view()->IsBookmarkBarVisible());

  anchored_widget.SetBounds(kInitialBounds);

  // Anchoring the widget should adjust the top-of-window bounds.
  controller()->AnchorWidgetToTopContainer(&anchored_widget, 10);
  gfx::Rect bounds1 = anchored_widget.GetWindowBoundsInScreen();
  EXPECT_EQ(bounds1.y(),
            browser_view()->top_container()->GetBoundsInScreen().bottom() + 10);
  EXPECT_EQ(kInitialBounds.x(), bounds1.x());
  EXPECT_EQ(kInitialBounds.size(), bounds1.size());

  controller()->StartRevealForTest(true);
  gfx::Rect bounds2 = anchored_widget.GetWindowBoundsInScreen();

  // The top-of-window bounds changed in the immersive reveal. |anchored_widget|
  // should have been repositioned.
  EXPECT_EQ(bounds2.y(),
            browser_view()->top_container()->GetBoundsInScreen().bottom() + 10);
  EXPECT_EQ(kInitialBounds.x(), bounds2.x());
  EXPECT_EQ(kInitialBounds.size(), bounds2.size());

  // Showing the bookmark bar changes the top container bounds and should
  // reposition the anchored widget.
  chrome::ExecuteCommand(browser(), IDC_SHOW_BOOKMARK_BAR);
  EXPECT_TRUE(browser_view()->IsBookmarkBarVisible());
  gfx::Rect bounds3 = anchored_widget.GetWindowBoundsInScreen();
  EXPECT_EQ(bounds3.y(),
            browser_view()->top_container()->GetBoundsInScreen().bottom() + 10);
  EXPECT_GT(bounds3.y(), bounds2.y());
  EXPECT_EQ(kInitialBounds.x(), bounds3.x());
  EXPECT_EQ(kInitialBounds.size(), bounds3.size());

  // 3) Test that the anchored widget is not repositioned when immersive mode
  // is not enabled.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(controller()->IsEnabled());
  chrome::ExecuteCommand(browser(), IDC_SHOW_BOOKMARK_BAR);
  EXPECT_EQ(bounds3, anchored_widget.GetWindowBoundsInScreen());
  EXPECT_NE(browser_view()->top_container()->GetBoundsInScreen().bottom() + 10,
            bounds3.bottom());

  // 4) Test that reenabling immersive fullscreen repositions any anchored
  // widgets.
  //
  // Maximize the window so that a bounds change in the top container is not
  // reported when entering immersive fullscreen. The top container has the
  // same bounds when |browser_view| is maximized as when the top container is
  // revealed when |browser_view_| is in immersive mode.
  ash::wm::MaximizeWindow(browser_view()->GetNativeWindow());
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());

  gfx::Rect bounds4 = anchored_widget.GetWindowBoundsInScreen();
  EXPECT_NE(bounds3, bounds4);
  EXPECT_EQ(bounds4.y(),
            browser_view()->top_container()->GetBoundsInScreen().bottom() + 10);
  EXPECT_EQ(kInitialBounds.x(), bounds4.x());
  EXPECT_EQ(kInitialBounds.size(), bounds4.size());

  BookmarkBarView::DisableAnimationsForTesting(false);
}

// Shelf-specific immersive mode tests.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest, ImmersiveShelf) {
  // Shelf is visible when the test starts.
  ash::internal::ShelfLayoutManager* shelf =
      ash::Shell::GetPrimaryRootWindowController()->GetShelfLayoutManager();
  ASSERT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());

  // Turning immersive mode on sets the shelf to auto-hide.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());

  // Disabling immersive mode puts it back.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(browser_view()->IsFullscreen());
  ASSERT_FALSE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());

  // The user could toggle the launcher behavior.
  shelf->SetAutoHideBehavior(ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());

  // Enabling immersive mode keeps auto-hide.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(browser_view()->IsFullscreen());
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());

  // Disabling immersive mode maintains the user's auto-hide selection.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_FALSE(browser_view()->IsFullscreen());
  ASSERT_FALSE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());
}

// Test how being simultaneously in tab fullscreen and immersive fullscreen
// affects the shelf visibility and whether the tab indicators are hidden.
IN_PROC_BROWSER_TEST_F(ImmersiveModeControllerAshTest,
                       TabAndBrowserFullscreen) {
  ash::internal::ShelfLayoutManager* shelf =
      ash::Shell::GetPrimaryRootWindowController()->GetShelfLayoutManager();

  controller()->SetForceHideTabIndicatorsForTest(false);

  // The shelf should start out as visible.
  ASSERT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());

  // 1) Test that entering tab fullscreen from immersive mode hides the tab
  // indicators and the shelf.
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());
  EXPECT_FALSE(controller()->ShouldHideTabIndicators());

  // The shelf visibility and the tab indicator visibility are updated as a
  // result of NOTIFICATION_FULLSCREEN_CHANGED which is asynchronous. Wait for
  // the notification before testing visibility.
  scoped_ptr<FullscreenNotificationObserver> waiter(
      new FullscreenNotificationObserver());

  browser()->fullscreen_controller()->ToggleFullscreenModeForTab(
      browser_view()->GetActiveWebContents(), true);
  waiter->Wait();
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_HIDDEN, shelf->visibility_state());
  EXPECT_TRUE(controller()->ShouldHideTabIndicators());

  // 2) Test that exiting tab fullscreen shows the tab indicators and autohides
  // the shelf.
  waiter.reset(new FullscreenNotificationObserver());
  browser()->fullscreen_controller()->ToggleFullscreenModeForTab(
      browser_view()->GetActiveWebContents(), false);
  waiter->Wait();
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());
  EXPECT_FALSE(controller()->ShouldHideTabIndicators());

  // 3) Test that exiting tab fullscreen and immersive fullscreen
  // simultaneously correctly updates the shelf visibility and whether the tab
  // indicators should be hidden.
  waiter.reset(new FullscreenNotificationObserver());
  browser()->fullscreen_controller()->ToggleFullscreenModeForTab(
      browser_view()->GetActiveWebContents(), true);
  waiter->Wait();
  waiter.reset(new FullscreenNotificationObserver());
  chrome::ToggleFullscreenMode(browser());
  waiter->Wait();

  ASSERT_FALSE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());
  EXPECT_TRUE(controller()->ShouldHideTabIndicators());
}

#endif  // defined(OS_CHROMEOS)
