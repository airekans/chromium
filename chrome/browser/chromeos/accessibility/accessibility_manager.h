// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_MANAGER_H_

#include "ash/shell_delegate.h"
#include "base/prefs/pref_change_registrar.h"
#include "chrome/browser/chromeos/accessibility/accessibility_util.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;

namespace content {
class WebUI;
}

namespace chromeos {

struct AccessibilityStatusEventDetails {
  AccessibilityStatusEventDetails(
      bool enabled,
      ash::AccessibilityNotificationVisibility notify);

  AccessibilityStatusEventDetails(
      bool enabled,
      ash::MagnifierType magnifier_type,
      ash::AccessibilityNotificationVisibility notify);

  bool enabled;
  ash::MagnifierType magnifier_type;
  ash::AccessibilityNotificationVisibility notify;
};

// AccessibilityManager changes the statuses of accessibility features
// watching profile notifications and pref-changes.
// TODO(yoshiki): merge MagnificationManager with AccessibilityManager.
class AccessibilityManager : public content::NotificationObserver {
 public:
  // Creates an instance of AccessibilityManager, this should be called once,
  // because only one instance should exist at the same time.
  static void Initialize();
  // Deletes the existing instance of AccessibilityManager.
  static void Shutdown();
  // Returns the existing instance. If there is no instance, returns NULL.
  static AccessibilityManager* Get();

  // Enables or disables spoken feedback. Enabling spoken feedback installs the
  // ChromeVox component extension.  If this is being called in a login/oobe
  // login screen, pass the WebUI object in login_web_ui so that ChromeVox
  // can be injected directly into that screen, otherwise it should be NULL.
  void EnableSpokenFeedback(bool enabled,
                            content::WebUI* login_web_ui,
                            ash::AccessibilityNotificationVisibility notify);

  // Returns true if spoken feedback is enabled, or false if not.
  bool IsSpokenFeedbackEnabled();

  // Toggles whether Chrome OS spoken feedback is on or off. See docs for
  // EnableSpokenFeedback, above.
  void ToggleSpokenFeedback(content::WebUI* login_web_ui,
                            ash::AccessibilityNotificationVisibility notify);

  // Speaks the specified string.
  void Speak(const std::string& text);

  // Speaks the given text if the accessibility pref is already set.
  void MaybeSpeak(const std::string& text);

  // Enables or disables the high contrast mode for Chrome.
  void EnableHighContrast(bool enabled);

  // Returns true if High Contrast is enabled, or false if not.
  bool IsHighContrastEnabled();

  void SetProfileForTest(Profile* profile);

 protected:
  AccessibilityManager();
  virtual ~AccessibilityManager();

 private:
  void UpdateSpokenFeedbackStatusFromPref();
  void UpdateHighContrastStatusFromPref();

  void SetProfile(Profile* profile);

  void UpdateChromeOSAccessibilityHistograms();

  // content::NotificationObserver implementation:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  Profile* profile_;
  content::NotificationRegistrar notification_registrar_;
  scoped_ptr<PrefChangeRegistrar> pref_change_registrar_;

  bool spoken_feedback_enabled_;
  bool high_contrast_enabled_;

  DISALLOW_COPY_AND_ASSIGN(AccessibilityManager);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_ACCESSIBILITY_ACCESSIBILITY_MANAGER_H_
