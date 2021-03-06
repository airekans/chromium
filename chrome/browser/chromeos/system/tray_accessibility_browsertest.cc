// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/magnifier/magnification_controller.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray_accessibility.h"
#include "ash/system/user/login_status.h"
#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/accessibility/magnification_manager.h"
#include "chrome/browser/chromeos/cros/cros_in_process_browser_test.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/user_manager_impl.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/policy_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/test/test_utils.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/widget/widget.h"

using testing::AnyNumber;
using testing::Return;
using testing::_;
using testing::WithParamInterface;

namespace chromeos {

enum PrefSettingMechanism {
  PREF_SERVICE,
  POLICY,
};

void SetMagnifierEnabled(bool enabled) {
  MagnificationManager::Get()->SetMagnifierEnabled(enabled);
}

class TrayAccessibilityTest
    : public CrosInProcessBrowserTest,
      public WithParamInterface<PrefSettingMechanism> {
 protected:
  TrayAccessibilityTest() {}
  virtual ~TrayAccessibilityTest() {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    EXPECT_CALL(provider_, IsInitializationComplete(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(provider_, RegisterPolicyDomain(_)).Times(AnyNumber());
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(&provider_);
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(switches::kLoginManager);
    command_line->AppendSwitchASCII(switches::kLoginProfile,
                                    TestingProfile::kTestUserProfileDir);
  }

  virtual void RunTestOnMainThreadLoop() OVERRIDE {
    // Need to mark oobe completed to show detailed views.
    StartupUtils::MarkOobeCompleted();
    CrosInProcessBrowserTest::RunTestOnMainThreadLoop();
  }

  void SetShowAccessibilityOptionsInSystemTrayMenu(bool value) {
    if (GetParam() == PREF_SERVICE) {
      Profile* profile = ProfileManager::GetDefaultProfile();
      PrefService* prefs = profile->GetPrefs();
      prefs->SetBoolean(prefs::kShouldAlwaysShowAccessibilityMenu, value);
    } else if (GetParam() == POLICY) {
      policy::PolicyMap policy_map;
      policy_map.Set(policy::key::kShowAccessibilityOptionsInSystemTrayMenu,
                     policy::POLICY_LEVEL_MANDATORY,
                     policy::POLICY_SCOPE_USER,
                     base::Value::CreateBooleanValue(value));
      provider_.UpdateChromePolicy(policy_map);
      base::RunLoop().RunUntilIdle();
    } else {
      FAIL() << "Unknown test parameterization";
    }
  }

  ash::internal::TrayAccessibility* tray() {
    return ash::Shell::GetInstance()->GetPrimarySystemTray()->
        GetTrayAccessibilityForTest();
  }

  bool IsTrayIconVisible() {
    return tray()->tray_icon_visible_;
  }

  views::View* CreateMenuItem() {
    return tray()->CreateDefaultView(GetLoginStatus());
  }

  void DestroyMenuItem() {
    return tray()->DestroyDefaultView();
  }

  bool CanCreateMenuItem() {
    views::View* menu_item_view = CreateMenuItem();
    DestroyMenuItem();
    return menu_item_view != NULL;
  }

  void SetLoginStatus(ash::user::LoginStatus status) {
    tray()->UpdateAfterLoginStatusChange(status);
  }

  ash::user::LoginStatus GetLoginStatus() {
    return tray()->login_;
  }

  bool CreateDetailedMenu() {
    tray()->PopupDetailedView(0, false);
    return tray()->detailed_menu_ != NULL;
  }

  void CloseDetailMenu() {
    CHECK(tray()->detailed_menu_);
    tray()->DestroyDetailedView();
    tray()->detailed_menu_ = NULL;
  }

  void ClickSpokenFeedbackOnDetailMenu() {
    views::View* button = tray()->detailed_menu_->spoken_feedback_view_;
    tray()->detailed_menu_->OnViewClicked(button);
  }

  void ClickHighContrastOnDetailMenu() {
    views::View* button = tray()->detailed_menu_->high_contrast_view_;
    EXPECT_TRUE(button);
    tray()->detailed_menu_->OnViewClicked(button);
  }

  void ClickScreenMagnifierOnDetailMenu() {
    views::View* button = tray()->detailed_menu_->screen_magnifier_view_;
    EXPECT_TRUE(button);
    tray()->detailed_menu_->OnViewClicked(button);
  }

  bool IsSpokenFeedbackEnabledOnDetailMenu() {
    return tray()->detailed_menu_->spoken_feedback_enabled_;
  }

  bool IsHighContrastEnabledOnDetailMenu() {
    return tray()->detailed_menu_->high_contrast_enabled_;
  }

  bool IsScreenMagnifierEnabledOnDetailMenu() {
    return tray()->detailed_menu_->screen_magnifier_enabled_;
  }

  policy::MockConfigurationPolicyProvider provider_;
};

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, LoginStatus) {
  EXPECT_EQ(ash::user::LOGGED_IN_NONE, GetLoginStatus());

  UserManager::Get()->UserLoggedIn(
      "owner@invalid.domain", "owner@invalid.domain", true);
  UserManager::Get()->SessionStarted();

  EXPECT_EQ(ash::user::LOGGED_IN_USER, GetLoginStatus());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, ShowTrayIcon) {
  SetLoginStatus(ash::user::LOGGED_IN_NONE);

  // Confirms that the icon is invisible before login.
  EXPECT_FALSE(IsTrayIconVisible());

  UserManager::Get()->UserLoggedIn(
      "owner@invalid.domain", "owner@invalid.domain", true);
  UserManager::Get()->SessionStarted();

  // Confirms that the icon is invisible just after login.
  EXPECT_FALSE(IsTrayIconVisible());

  // Toggling spoken feedback changes the visibillity of the icon.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(IsTrayIconVisible());

  // Toggling high contrast the visibillity of the icon.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_FALSE(IsTrayIconVisible());

  // Toggling magnifier the visibility of the icon.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(IsTrayIconVisible());
  SetMagnifierEnabled(false);
  EXPECT_FALSE(IsTrayIconVisible());

  // Enabling all accessibility features.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(IsTrayIconVisible());
  SetMagnifierEnabled(false);
  EXPECT_FALSE(IsTrayIconVisible());

  // Confirms that prefs::kShouldAlwaysShowAccessibilityMenu doesn't affect
  // the icon on the tray.
  SetShowAccessibilityOptionsInSystemTrayMenu(true);
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(IsTrayIconVisible());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_FALSE(IsTrayIconVisible());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, ShowMenu) {
  // Login
  UserManager::Get()->UserLoggedIn(
      "owner@invalid.domain", "owner@invalid.domain", true);
  UserManager::Get()->SessionStarted();

  SetShowAccessibilityOptionsInSystemTrayMenu(false);

  // Confirms that the menu is hidden.
  EXPECT_FALSE(CanCreateMenuItem());

  // Toggling spoken feedback changes the visibillity of the menu.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_FALSE(CanCreateMenuItem());

  // Toggling high contrast changes the visibillity of the menu.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_FALSE(CanCreateMenuItem());

  // Toggling screen magnifier changes the visibility of the menu.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_FALSE(CanCreateMenuItem());

  // Enabling all accessibility features.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_FALSE(CanCreateMenuItem());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, ShowMenuWithShowMenuOption) {
  // Login
  UserManager::Get()->UserLoggedIn(
      "owner@invalid.domain", "owner@invalid.domain", true);
  UserManager::Get()->SessionStarted();

  SetShowAccessibilityOptionsInSystemTrayMenu(true);

  // Confirms that the menu is visible.
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling spoken feedback.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling high contrast.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling screen magnifier.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CanCreateMenuItem());

  // Enabling all accessibility features.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CanCreateMenuItem());

  SetShowAccessibilityOptionsInSystemTrayMenu(false);

  // Confirms that the menu is invisible.
  EXPECT_FALSE(CanCreateMenuItem());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, ShowMenuWithShowOnLoginScreen) {
  SetLoginStatus(ash::user::LOGGED_IN_NONE);

  // Confirms that the menu is visible.
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling spoken feedback.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling high contrast.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CanCreateMenuItem());

  // The menu is keeping visible regardless of toggling screen magnifier.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CanCreateMenuItem());

  // Enabling all accessibility features.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CanCreateMenuItem());
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CanCreateMenuItem());
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CanCreateMenuItem());

  SetShowAccessibilityOptionsInSystemTrayMenu(true);

  // Confirms that the menu is keeping visible.
  EXPECT_TRUE(CanCreateMenuItem());

  SetShowAccessibilityOptionsInSystemTrayMenu(false);

  // Confirms that the menu is keeping visible.
  EXPECT_TRUE(CanCreateMenuItem());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, KeepMenuVisibilityOnLockScreen) {
  // Enables high contrast mode.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CanCreateMenuItem());

  // Locks the screen.
  SetLoginStatus(ash::user::LOGGED_IN_LOCKED);
  EXPECT_TRUE(CanCreateMenuItem());

  // Disables high contrast mode.
  AccessibilityManager::Get()->EnableHighContrast(false);

  // Confirms that the menu is still visible.
  EXPECT_TRUE(CanCreateMenuItem());
}

#if defined(OS_CHROMEOS)
#define MAYBE_ClickDetailMenu DISABLED_ClickDetailMenu
#else
#define MAYBE_ClickDetailMenu ClickDetailMenu
#endif

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, MAYBE_ClickDetailMenu) {
  // Confirms that the check item toggles the spoken feedback.
  EXPECT_FALSE(AccessibilityManager::Get()->IsSpokenFeedbackEnabled());

  EXPECT_TRUE(CreateDetailedMenu());
  ClickSpokenFeedbackOnDetailMenu();
  EXPECT_TRUE(AccessibilityManager::Get()->IsSpokenFeedbackEnabled());

  EXPECT_TRUE(CreateDetailedMenu());
  ClickSpokenFeedbackOnDetailMenu();
  EXPECT_FALSE(AccessibilityManager::Get()->IsSpokenFeedbackEnabled());

  // Confirms that the check item toggles the high contrast.
  EXPECT_FALSE(AccessibilityManager::Get()->IsHighContrastEnabled());

  EXPECT_TRUE(CreateDetailedMenu());
  ClickHighContrastOnDetailMenu();
  EXPECT_TRUE(AccessibilityManager::Get()->IsHighContrastEnabled());

  EXPECT_TRUE(CreateDetailedMenu());
  ClickHighContrastOnDetailMenu();
  EXPECT_FALSE(AccessibilityManager::Get()->IsHighContrastEnabled());

  // Confirms that the check item toggles the magnifier.
  EXPECT_FALSE(AccessibilityManager::Get()->IsHighContrastEnabled());

  EXPECT_FALSE(MagnificationManager::Get()->IsMagnifierEnabled());
  EXPECT_TRUE(CreateDetailedMenu());
  ClickScreenMagnifierOnDetailMenu();
  EXPECT_TRUE(MagnificationManager::Get()->IsMagnifierEnabled());

  EXPECT_TRUE(CreateDetailedMenu());
  ClickScreenMagnifierOnDetailMenu();
  EXPECT_FALSE(MagnificationManager::Get()->IsMagnifierEnabled());
}

IN_PROC_BROWSER_TEST_P(TrayAccessibilityTest, CheckMarksOnDetailMenu) {
  // At first, all of the check is unchecked.
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Enabling spoken feedback.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_TRUE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Disabling spoken feedback.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Enabling high contrast.
  AccessibilityManager::Get()->EnableHighContrast(true);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_TRUE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Disabling high contrast.
  AccessibilityManager::Get()->EnableHighContrast(false);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Enabling full screen magnifier.
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_TRUE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Disabling screen magnifier.
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Enabling all of the a11y features.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      true, NULL, ash::A11Y_NOTIFICATION_NONE);
  AccessibilityManager::Get()->EnableHighContrast(true);
  SetMagnifierEnabled(true);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_TRUE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_TRUE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_TRUE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();

  // Disabling all of the a11y features.
  AccessibilityManager::Get()->EnableSpokenFeedback(
      false, NULL, ash::A11Y_NOTIFICATION_NONE);
  AccessibilityManager::Get()->EnableHighContrast(false);
  SetMagnifierEnabled(false);
  EXPECT_TRUE(CreateDetailedMenu());
  EXPECT_FALSE(IsSpokenFeedbackEnabledOnDetailMenu());
  EXPECT_FALSE(IsHighContrastEnabledOnDetailMenu());
  EXPECT_FALSE(IsScreenMagnifierEnabledOnDetailMenu());
  CloseDetailMenu();
}

INSTANTIATE_TEST_CASE_P(TrayAccessibilityTestInstance,
                        TrayAccessibilityTest,
                        testing::Values(PREF_SERVICE,
                                        POLICY));

}  // namespace chromeos
