// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/notifier/p2p_notifier.h"

#include <cstddef>

#include "jingle/notifier/listener/fake_push_client.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_state_map.h"
#include "sync/notifier/fake_sync_notifier_observer.h"
#include "sync/notifier/object_id_state_map_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

class P2PNotifierTest : public testing::Test {
 protected:
  P2PNotifierTest()
      : fake_push_client_(new notifier::FakePushClient()),
        p2p_notifier_(
            scoped_ptr<notifier::PushClient>(fake_push_client_),
            NOTIFY_OTHERS),
        next_sent_notification_to_reflect_(0) {
    p2p_notifier_.RegisterHandler(&fake_observer_);
  }

  virtual ~P2PNotifierTest() {
    p2p_notifier_.UnregisterHandler(&fake_observer_);
  }

  ModelTypeStateMap MakeStateMap(ModelTypeSet types) {
    return ModelTypeSetToStateMap(types, std::string());
  }

  // Simulate receiving all the notifications we sent out since last
  // time this was called.
  void ReflectSentNotifications() {
    const std::vector<notifier::Notification>& sent_notifications =
        fake_push_client_->sent_notifications();
    for(size_t i = next_sent_notification_to_reflect_;
        i < sent_notifications.size(); ++i) {
      p2p_notifier_.OnIncomingNotification(sent_notifications[i]);
    }
    next_sent_notification_to_reflect_ = sent_notifications.size();
  }

  // Owned by |p2p_notifier_|.
  notifier::FakePushClient* fake_push_client_;
  P2PNotifier p2p_notifier_;
  FakeSyncNotifierObserver fake_observer_;

 private:
  size_t next_sent_notification_to_reflect_;
};

// Make sure the P2PNotificationTarget <-> string conversions work.
TEST_F(P2PNotifierTest, P2PNotificationTarget) {
  for (int i = FIRST_NOTIFICATION_TARGET;
       i <= LAST_NOTIFICATION_TARGET; ++i) {
    P2PNotificationTarget target = static_cast<P2PNotificationTarget>(i);
    const std::string& target_str = P2PNotificationTargetToString(target);
    EXPECT_FALSE(target_str.empty());
    EXPECT_EQ(target, P2PNotificationTargetFromString(target_str));
  }
  EXPECT_EQ(NOTIFY_SELF, P2PNotificationTargetFromString("unknown"));
}

// Make sure notification targeting works correctly.
TEST_F(P2PNotifierTest, P2PNotificationDataIsTargeted) {
  {
    const P2PNotificationData notification_data(
        "sender", NOTIFY_SELF, ModelTypeSet());
    EXPECT_TRUE(notification_data.IsTargeted("sender"));
    EXPECT_FALSE(notification_data.IsTargeted("other1"));
    EXPECT_FALSE(notification_data.IsTargeted("other2"));
  }
  {
    const P2PNotificationData notification_data(
        "sender", NOTIFY_OTHERS, ModelTypeSet());
    EXPECT_FALSE(notification_data.IsTargeted("sender"));
    EXPECT_TRUE(notification_data.IsTargeted("other1"));
    EXPECT_TRUE(notification_data.IsTargeted("other2"));
  }
  {
    const P2PNotificationData notification_data(
        "sender", NOTIFY_ALL, ModelTypeSet());
    EXPECT_TRUE(notification_data.IsTargeted("sender"));
    EXPECT_TRUE(notification_data.IsTargeted("other1"));
    EXPECT_TRUE(notification_data.IsTargeted("other2"));
  }
}

// Make sure the P2PNotificationData <-> string conversions work for a
// default-constructed P2PNotificationData.
TEST_F(P2PNotifierTest, P2PNotificationDataDefault) {
  const P2PNotificationData notification_data;
  EXPECT_TRUE(notification_data.IsTargeted(""));
  EXPECT_FALSE(notification_data.IsTargeted("other1"));
  EXPECT_FALSE(notification_data.IsTargeted("other2"));
  EXPECT_TRUE(notification_data.GetChangedTypes().Empty());
  const std::string& notification_data_str = notification_data.ToString();
  EXPECT_EQ(
      "{\"changedTypes\":[],\"notificationType\":\"notifySelf\","
      "\"senderId\":\"\"}", notification_data_str);

  P2PNotificationData notification_data_parsed;
  EXPECT_TRUE(notification_data_parsed.ResetFromString(notification_data_str));
  EXPECT_TRUE(notification_data.Equals(notification_data_parsed));
}

// Make sure the P2PNotificationData <-> string conversions work for a
// non-default-constructed P2PNotificationData.
TEST_F(P2PNotifierTest, P2PNotificationDataNonDefault) {
  const ModelTypeSet changed_types(BOOKMARKS, THEMES);
  const P2PNotificationData notification_data(
      "sender", NOTIFY_ALL, changed_types);
  EXPECT_TRUE(notification_data.IsTargeted("sender"));
  EXPECT_TRUE(notification_data.IsTargeted("other1"));
  EXPECT_TRUE(notification_data.IsTargeted("other2"));
  EXPECT_TRUE(notification_data.GetChangedTypes().Equals(changed_types));
  const std::string& notification_data_str = notification_data.ToString();
  EXPECT_EQ(
      "{\"changedTypes\":[\"Bookmarks\",\"Themes\"],"
      "\"notificationType\":\"notifyAll\","
      "\"senderId\":\"sender\"}", notification_data_str);

  P2PNotificationData notification_data_parsed;
  EXPECT_TRUE(notification_data_parsed.ResetFromString(notification_data_str));
  EXPECT_TRUE(notification_data.Equals(notification_data_parsed));
}

// Set up the P2PNotifier, simulate a successful connection, and send
// a notification with the default target (NOTIFY_OTHERS).  The
// observer should receive only a notification from the call to
// UpdateEnabledTypes().
TEST_F(P2PNotifierTest, NotificationsBasic) {
  const ModelTypeSet enabled_types(BOOKMARKS, PREFERENCES);

  p2p_notifier_.UpdateRegisteredIds(&fake_observer_,
                                    ModelTypeSetToObjectIdSet(enabled_types));

  p2p_notifier_.SetUniqueId("sender");

  const char kEmail[] = "foo@bar.com";
  const char kToken[] = "token";
  p2p_notifier_.UpdateCredentials(kEmail, kToken);
  {
    notifier::Subscription expected_subscription;
    expected_subscription.channel = kSyncP2PNotificationChannel;
    expected_subscription.from = kEmail;
    EXPECT_TRUE(notifier::SubscriptionListsEqual(
        fake_push_client_->subscriptions(),
        notifier::SubscriptionList(1, expected_subscription)));
  }
  EXPECT_EQ(kEmail, fake_push_client_->email());
  EXPECT_EQ(kToken, fake_push_client_->token());

  ReflectSentNotifications();
  fake_push_client_->EnableNotifications();
  EXPECT_EQ(NO_NOTIFICATION_ERROR,
            fake_observer_.GetNotificationsDisabledReason());

  ReflectSentNotifications();
  EXPECT_EQ(1, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      ModelTypeStateMapToObjectIdStateMap(MakeStateMap(enabled_types)),
      Eq(fake_observer_.GetLastNotificationIdStateMap()));
  EXPECT_EQ(REMOTE_NOTIFICATION, fake_observer_.GetLastNotificationSource());

  // Sent with target NOTIFY_OTHERS so should not be propagated to
  // |fake_observer_|.
  {
    ModelTypeSet changed_types(THEMES, APPS);
    p2p_notifier_.SendNotification(changed_types);
  }

  ReflectSentNotifications();
  EXPECT_EQ(1, fake_observer_.GetNotificationCount());
}

// Set up the P2PNotifier and send out notifications with various
// target settings.  The notifications received by the observer should
// be consistent with the target settings.
TEST_F(P2PNotifierTest, SendNotificationData) {
  const ModelTypeSet enabled_types(BOOKMARKS, PREFERENCES, THEMES);
  const ModelTypeSet changed_types(THEMES, APPS);
  const ModelTypeSet expected_types(THEMES);

  p2p_notifier_.UpdateRegisteredIds(&fake_observer_,
                                    ModelTypeSetToObjectIdSet(enabled_types));

  p2p_notifier_.SetUniqueId("sender");
  p2p_notifier_.UpdateCredentials("foo@bar.com", "fake_token");

  ReflectSentNotifications();
  fake_push_client_->EnableNotifications();
  EXPECT_EQ(NO_NOTIFICATION_ERROR,
            fake_observer_.GetNotificationsDisabledReason());

  ReflectSentNotifications();
  EXPECT_EQ(1, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      ModelTypeStateMapToObjectIdStateMap(MakeStateMap(enabled_types)),
      Eq(fake_observer_.GetLastNotificationIdStateMap()));
  EXPECT_EQ(REMOTE_NOTIFICATION, fake_observer_.GetLastNotificationSource());

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(P2PNotificationData());
  ReflectSentNotifications();
  EXPECT_EQ(1, fake_observer_.GetNotificationCount());

  const ObjectIdStateMap& expected_ids =
      ModelTypeStateMapToObjectIdStateMap(MakeStateMap(expected_types));

  // Should be propagated.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender", NOTIFY_SELF, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(2, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      expected_ids,
      Eq(fake_observer_.GetLastNotificationIdStateMap()));

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender2", NOTIFY_SELF, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(2, fake_observer_.GetNotificationCount());

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender", NOTIFY_SELF, ModelTypeSet()));
  ReflectSentNotifications();
  EXPECT_EQ(2, fake_observer_.GetNotificationCount());

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender", NOTIFY_OTHERS, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(2, fake_observer_.GetNotificationCount());

  // Should be propagated.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender2", NOTIFY_OTHERS, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(3, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      expected_ids,
      Eq(fake_observer_.GetLastNotificationIdStateMap()));

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender2", NOTIFY_OTHERS, ModelTypeSet()));
  ReflectSentNotifications();
  EXPECT_EQ(3, fake_observer_.GetNotificationCount());

  // Should be propagated.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender", NOTIFY_ALL, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(4, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      expected_ids,
      Eq(fake_observer_.GetLastNotificationIdStateMap()));

  // Should be propagated.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender2", NOTIFY_ALL, changed_types));
  ReflectSentNotifications();
  EXPECT_EQ(5, fake_observer_.GetNotificationCount());
  EXPECT_THAT(
      expected_ids,
      Eq(fake_observer_.GetLastNotificationIdStateMap()));

  // Should be dropped.
  p2p_notifier_.SendNotificationDataForTest(
      P2PNotificationData("sender2", NOTIFY_ALL, ModelTypeSet()));
  ReflectSentNotifications();
  EXPECT_EQ(5, fake_observer_.GetNotificationCount());
}

}  // namespace

}  // namespace syncer
