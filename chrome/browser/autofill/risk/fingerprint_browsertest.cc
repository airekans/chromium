// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/browser/risk/fingerprint.h"

#include "base/bind.h"
#include "base/message_loop.h"
#include "base/port.h"
#include "chrome/browser/browser_process.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/autofill/browser/risk/proto/fingerprint.pb.h"
#include "content/public/browser/geolocation_provider.h"
#include "content/public/common/geoposition.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebRect.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebScreenInfo.h"
#include "ui/gfx/rect.h"

namespace autofill {
namespace risk {

const uint64 kObfuscatedGaiaId = GG_UINT64_C(16571487432910023183);
const char kCharset[] = "UTF-8";
const char kAcceptLanguages[] = "en-US,en";
const int kScreenColorDepth = 53;

const double kLatitude = -42.0;
const double kLongitude = 17.3;
const double kAltitude = 123.4;
const double kAccuracy = 73.7;
const int kGeolocationTime = 87;

class AutofillRiskFingerprintTest : public InProcessBrowserTest {
 public:
  AutofillRiskFingerprintTest()
      : kWindowBounds(2, 3, 5, 7),
        kContentBounds(11, 13, 17, 37),
        kScreenBounds(0, 0, 101, 71),
        kAvailableScreenBounds(0, 11, 101, 60),
        kUnavailableScreenBounds(0, 0, 101, 11),
        message_loop_(MessageLoop::TYPE_UI) {}

  void GetFingerprintTestCallback(scoped_ptr<Fingerprint> fingerprint) {
    // Verify that all fields Chrome can fill have been filled.
    ASSERT_TRUE(fingerprint->has_machine_characteristics());
    const Fingerprint_MachineCharacteristics& machine =
        fingerprint->machine_characteristics();
    ASSERT_TRUE(machine.has_operating_system_build());
    ASSERT_TRUE(machine.has_browser_install_time_hours());
    ASSERT_GT(machine.font_size(), 0);
    ASSERT_GT(machine.plugin_size(), 0);
    ASSERT_TRUE(machine.has_utc_offset_ms());
    ASSERT_TRUE(machine.has_browser_language());
    ASSERT_GT(machine.requested_language_size(), 0);
    ASSERT_TRUE(machine.has_charset());
    ASSERT_TRUE(machine.has_screen_count());
    ASSERT_TRUE(machine.has_screen_size());
    ASSERT_TRUE(machine.screen_size().has_width());
    ASSERT_TRUE(machine.screen_size().has_height());
    ASSERT_TRUE(machine.has_screen_color_depth());
    ASSERT_TRUE(machine.has_unavailable_screen_size());
    ASSERT_TRUE(machine.unavailable_screen_size().has_width());
    ASSERT_TRUE(machine.unavailable_screen_size().has_height());
    ASSERT_TRUE(machine.has_user_agent());
    ASSERT_TRUE(machine.has_cpu());
    ASSERT_TRUE(machine.cpu().has_vendor_name());
    ASSERT_TRUE(machine.cpu().has_brand());
    ASSERT_TRUE(machine.has_ram());
    ASSERT_TRUE(machine.has_graphics_card());
    ASSERT_TRUE(machine.graphics_card().has_vendor_id());
    ASSERT_TRUE(machine.graphics_card().has_device_id());
    ASSERT_TRUE(machine.has_browser_build());
    ASSERT_TRUE(machine.has_browser_feature());

    ASSERT_TRUE(fingerprint->has_transient_state());
    const Fingerprint_TransientState& transient_state =
        fingerprint->transient_state();
    ASSERT_TRUE(transient_state.has_inner_window_size());
    ASSERT_TRUE(transient_state.has_outer_window_size());
    ASSERT_TRUE(transient_state.inner_window_size().has_width());
    ASSERT_TRUE(transient_state.inner_window_size().has_height());
    ASSERT_TRUE(transient_state.outer_window_size().has_width());
    ASSERT_TRUE(transient_state.outer_window_size().has_height());

    ASSERT_TRUE(fingerprint->has_user_characteristics());
    const Fingerprint_UserCharacteristics& user_characteristics =
        fingerprint->user_characteristics();
    ASSERT_TRUE(user_characteristics.has_location());
    const Fingerprint_UserCharacteristics_Location& location =
        user_characteristics.location();
    ASSERT_TRUE(location.has_altitude());
    ASSERT_TRUE(location.has_latitude());
    ASSERT_TRUE(location.has_longitude());
    ASSERT_TRUE(location.has_accuracy());
    ASSERT_TRUE(location.has_time_in_ms());

    ASSERT_TRUE(fingerprint->has_metadata());
    ASSERT_TRUE(fingerprint->metadata().has_timestamp_ms());
    ASSERT_TRUE(fingerprint->metadata().has_obfuscated_gaia_id());
    ASSERT_TRUE(fingerprint->metadata().has_fingerprinter_version());

    // Some values have exact known (mocked out) values:
    ASSERT_EQ(2, machine.requested_language_size());
    EXPECT_EQ("en-US", machine.requested_language(0));
    EXPECT_EQ("en", machine.requested_language(1));
    EXPECT_EQ(kCharset, machine.charset());
    EXPECT_EQ(kScreenColorDepth, machine.screen_color_depth());
    EXPECT_EQ(kUnavailableScreenBounds.width(),
              machine.unavailable_screen_size().width());
    EXPECT_EQ(kUnavailableScreenBounds.height(),
              machine.unavailable_screen_size().height());
    EXPECT_EQ(
        Fingerprint_MachineCharacteristics_BrowserFeature_FEATURE_AUTOCHECKOUT,
        machine.browser_feature());
    EXPECT_EQ(kContentBounds.width(),
              transient_state.inner_window_size().width());
    EXPECT_EQ(kContentBounds.height(),
              transient_state.inner_window_size().height());
    EXPECT_EQ(kWindowBounds.width(),
              transient_state.outer_window_size().width());
    EXPECT_EQ(kWindowBounds.height(),
              transient_state.outer_window_size().height());
    EXPECT_EQ(kObfuscatedGaiaId, fingerprint->metadata().obfuscated_gaia_id());
    EXPECT_EQ(kAltitude, location.altitude());
    EXPECT_EQ(kLatitude, location.latitude());
    EXPECT_EQ(kLongitude, location.longitude());
    EXPECT_EQ(kAccuracy, location.accuracy());
    EXPECT_EQ(kGeolocationTime, location.time_in_ms());

    message_loop_.Quit();
  }

 protected:
  const gfx::Rect kWindowBounds;
  const gfx::Rect kContentBounds;
  const gfx::Rect kScreenBounds;
  const gfx::Rect kAvailableScreenBounds;
  const gfx::Rect kUnavailableScreenBounds;
  MessageLoop message_loop_;
};

// This test is flaky on Windows. See http://crbug.com/178356.
#if defined(OS_WIN)
#define MAYBE_GetFingerprint DISABLED_GetFingerprint
#else
#define MAYBE_GetFingerprint GetFingerprint
#endif
// Test that getting a fingerprint works on some basic level.
IN_PROC_BROWSER_TEST_F(AutofillRiskFingerprintTest, MAYBE_GetFingerprint) {
  content::Geoposition position;
  position.latitude = kLatitude;
  position.longitude = kLongitude;
  position.altitude = kAltitude;
  position.accuracy = kAccuracy;
  position.timestamp =
      base::Time::UnixEpoch() +
      base::TimeDelta::FromMilliseconds(kGeolocationTime);
  scoped_refptr<content::MessageLoopRunner> runner =
      new content::MessageLoopRunner;
  content::GeolocationProvider::OverrideLocationForTesting(
      position, runner->QuitClosure());
  runner->Run();

  WebKit::WebScreenInfo screen_info;
  screen_info.depth = kScreenColorDepth;
  screen_info.rect = WebKit::WebRect(kScreenBounds);
  screen_info.availableRect = WebKit::WebRect(kAvailableScreenBounds);

  internal::GetFingerprintInternal(
      kObfuscatedGaiaId, kWindowBounds, kContentBounds, screen_info,
      "25.0.0.123", kCharset, kAcceptLanguages, base::Time::Now(),
      DIALOG_TYPE_AUTOCHECKOUT, g_browser_process->GetApplicationLocale(),
      base::Bind(&AutofillRiskFingerprintTest::GetFingerprintTestCallback,
                 base::Unretained(this)));

  // Wait for the callback to be called.
  message_loop_.Run();
}

}  // namespace risk
}  // namespace autofill
