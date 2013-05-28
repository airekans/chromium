// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_integration_service.h"

#include "base/message_loop.h"
#include "chrome/browser/chromeos/drive/dummy_file_system.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/dummy_drive_service.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {

class DriveIntegrationServiceTest : public testing::Test {
 public:
  DriveIntegrationServiceTest() :
      ui_thread_(content::BrowserThread::UI, &message_loop_),
      integration_service_(NULL) {}

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile);
    integration_service_.reset(new DriveIntegrationService(
        profile_.get(),
        new google_apis::DummyDriveService,
        base::FilePath(),
        new DummyFileSystem));
  }

  virtual void TearDown() OVERRIDE {
    integration_service_.reset();
    google_apis::test_util::RunBlockingPoolTask();
    profile_.reset();
  }

 protected:
  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<DriveIntegrationService> integration_service_;
};

TEST_F(DriveIntegrationServiceTest, InitializeAndShutdown) {
  integration_service_->Initialize();
  google_apis::test_util::RunBlockingPoolTask();
  integration_service_->Shutdown();
}

}  // namespace drive
