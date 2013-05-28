// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_app_registry.h"

#include "base/files/file_path.h"
#include "base/json/json_file_value_serializer.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/fake_drive_service.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;

namespace drive {

class DriveAppRegistryTest : public testing::Test {
 protected:
  DriveAppRegistryTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_) {
  }

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile);

    // The fake object will be manually deleted in TearDown().
    fake_drive_service_.reset(new google_apis::FakeDriveService);
    fake_drive_service_->LoadAppListForDriveApi("chromeos/drive/applist.json");

    scheduler_.reset(
        new JobScheduler(profile_.get(), fake_drive_service_.get()));

    web_apps_registry_.reset(new DriveAppRegistry(scheduler_.get()));
    web_apps_registry_->Update();
    google_apis::test_util::RunBlockingPoolTask();
  }

  bool VerifyApp(const ScopedVector<DriveAppInfo>& list,
                 const std::string& web_store_id,
                 const std::string& app_id,
                 const std::string& app_name,
                 const std::string& object_type,
                 bool is_primary) {
    bool found = false;
    for (ScopedVector<DriveAppInfo>::const_iterator it = list.begin();
         it != list.end(); ++it) {
      const DriveAppInfo* app = *it;
      if (web_store_id == app->web_store_id) {
        EXPECT_EQ(app_id, app->app_id);
        EXPECT_EQ(app_name, UTF16ToUTF8(app->app_name));
        EXPECT_EQ(object_type, UTF16ToUTF8(app->object_type));
        EXPECT_EQ(is_primary, app->is_primary_selector);
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "Unable to find app with web_store_id "
        << web_store_id;
    return found;
  }

  bool VerifyApp1(const ScopedVector<DriveAppInfo>& list,
                  bool is_primary) {
    return VerifyApp(list, "abcdefabcdef", "11111111",
              "Drive App 1", "Drive App Object 1",
              is_primary);
  }

  bool VerifyApp2(const ScopedVector<DriveAppInfo>& list,
                  bool is_primary) {
    return VerifyApp(list, "deadbeefdeadbeef", "22222222",
              "Drive App 2", "Drive App Object 2",
              is_primary);
  }

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;

  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<google_apis::FakeDriveService> fake_drive_service_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<DriveAppRegistry> web_apps_registry_;
};

TEST_F(DriveAppRegistryTest, LoadAndFindDriveApps) {
  // Find by primary extension 'exe'.
  ScopedVector<DriveAppInfo> ext_results;
  base::FilePath ext_file(FILE_PATH_LITERAL("drive/file.exe"));
  web_apps_registry_->GetAppsForFile(ext_file, std::string(), &ext_results);
  ASSERT_EQ(1U, ext_results.size());
  VerifyApp(ext_results, "abcdefghabcdefghabcdefghabcdefgh", "123456788192",
            "Drive app 1", "", true);

  // Find by primary MIME type.
  ScopedVector<DriveAppInfo> primary_app;
  web_apps_registry_->GetAppsForFile(base::FilePath(),
      "application/vnd.google-apps.drive-sdk.123456788192", &primary_app);
  ASSERT_EQ(1U, primary_app.size());
  VerifyApp(primary_app, "abcdefghabcdefghabcdefghabcdefgh", "123456788192",
            "Drive app 1", "", true);

  // Find by secondary MIME type.
  ScopedVector<DriveAppInfo> secondary_app;
  web_apps_registry_->GetAppsForFile(
      base::FilePath(), "text/html", &secondary_app);
  ASSERT_EQ(1U, secondary_app.size());
  VerifyApp(secondary_app, "abcdefghabcdefghabcdefghabcdefgh", "123456788192",
            "Drive app 1", "", false);
}

}  // namespace drive
