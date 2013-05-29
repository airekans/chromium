// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/app_shim/app_shim_host_mac.h"

#include "apps/app_shim/app_shim_messages.h"
#include "base/basictypes.h"
#include "base/memory/scoped_vector.h"
#include "chrome/test/base/testing_profile.h"
#include "ipc/ipc_message.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestingAppShimHost : public AppShimHost {
 public:
  explicit TestingAppShimHost(Profile* profile);
  virtual ~TestingAppShimHost() {}

  bool ReceiveMessage(IPC::Message* message);

  const std::vector<IPC::Message*>& sent_messages() {
    return sent_messages_.get();
  }

  void set_fails_profile(bool fails_profile) {
    fails_profile_ = fails_profile;
  }

  void set_fails_launch(bool fails_launch) {
    fails_launch_ = fails_launch;
  }

 protected:
  virtual Profile* FetchProfileForDirectory(const std::string& profile_dir)
      OVERRIDE;
  virtual bool Send(IPC::Message* message) OVERRIDE;

 private:
  Profile* test_profile_;
  bool fails_profile_;
  bool fails_launch_;

  ScopedVector<IPC::Message> sent_messages_;

  DISALLOW_COPY_AND_ASSIGN(TestingAppShimHost);
};

TestingAppShimHost::TestingAppShimHost(Profile* profile)
    : test_profile_(profile),
      fails_profile_(false),
      fails_launch_(false) {
}

bool TestingAppShimHost::ReceiveMessage(IPC::Message* message) {
  bool handled = OnMessageReceived(*message);
  delete message;
  return handled;
}

bool TestingAppShimHost::Send(IPC::Message* message) {
  sent_messages_.push_back(message);
  return true;
}

Profile* TestingAppShimHost::FetchProfileForDirectory(
    const std::string& profile_dir) {
  return fails_profile_ ? NULL : test_profile_;
}

class AppShimHostTest : public testing::Test,
                        public apps::AppShimHandler {
 public:
  AppShimHostTest() : launch_count_(0), close_count_(0), focus_count_(0) {}

  TestingAppShimHost* host() { return host_.get(); }
  TestingProfile* profile() { return profile_.get(); }

  bool LaunchWasSuccessful() {
    EXPECT_EQ(1u, host()->sent_messages().size());
    IPC::Message* message = host()->sent_messages()[0];
    EXPECT_EQ(AppShimMsg_LaunchApp_Done::ID, message->type());
    AppShimMsg_LaunchApp_Done::Param param;
    AppShimMsg_LaunchApp_Done::Read(message, &param);
    return param.a;
  }

  void SimulateDisconnect() {
    implicit_cast<IPC::Listener*>(host_.release())->OnChannelError();
  }

 protected:
  virtual bool OnShimLaunch(Host* host) OVERRIDE {
    ++launch_count_;
    return true;
  }

  virtual void OnShimClose(Host* host) OVERRIDE { ++close_count_; }
  virtual void OnShimFocus(Host* host) OVERRIDE { ++focus_count_; }

  int launch_count_;
  int close_count_;
  int focus_count_;

 private:
  virtual void SetUp() OVERRIDE {
    testing::Test::SetUp();
    profile_.reset(new TestingProfile);
    host_.reset(new TestingAppShimHost(profile()));
  }

  scoped_ptr<TestingAppShimHost> host_;
  scoped_ptr<TestingProfile> profile_;

  DISALLOW_COPY_AND_ASSIGN(AppShimHostTest);
};

const char kTestAppId[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kTestProfileDir[] = "Default";

}  // namespace

TEST_F(AppShimHostTest, TestLaunchAppWithHandler) {
  apps::AppShimHandler::RegisterHandler(kTestAppId, this);
  EXPECT_TRUE(host()->ReceiveMessage(
      new AppShimHostMsg_LaunchApp(kTestProfileDir, kTestAppId)));
  EXPECT_EQ(kTestAppId,
            implicit_cast<apps::AppShimHandler::Host*>(host())->GetAppId());
  EXPECT_TRUE(LaunchWasSuccessful());
  EXPECT_EQ(1, launch_count_);
  EXPECT_EQ(0, focus_count_);
  EXPECT_EQ(0, close_count_);

  EXPECT_TRUE(host()->ReceiveMessage(new AppShimHostMsg_FocusApp()));
  EXPECT_EQ(1, focus_count_);

  SimulateDisconnect();
  EXPECT_EQ(1, close_count_);
  apps::AppShimHandler::RemoveHandler(kTestAppId);
}

TEST_F(AppShimHostTest, TestFailProfile) {
  host()->set_fails_profile(true);
  host()->ReceiveMessage(
      new AppShimHostMsg_LaunchApp(kTestProfileDir, kTestAppId));
  ASSERT_FALSE(LaunchWasSuccessful());
}

TEST_F(AppShimHostTest, TestFailLaunch) {
  host()->set_fails_launch(true);
  host()->ReceiveMessage(
      new AppShimHostMsg_LaunchApp(kTestProfileDir, kTestAppId));
  ASSERT_FALSE(LaunchWasSuccessful());
}
