// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GOOGLE_APIS_DRIVE_NOTIFICATION_MANAGER_H_
#define CHROME_BROWSER_GOOGLE_APIS_DRIVE_NOTIFICATION_MANAGER_H_

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer.h"
#include "chrome/browser/google_apis/drive_notification_observer.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "sync/notifier/invalidation_handler.h"

class Profile;
class ProfileSyncService;

namespace google_apis {

// Informs observers when they should check Google Drive for updates.
// Conditions under which updates should be searched:
// 1. XMPP invalidation is received from Google Drive.
// 2. Polling timer counts down.
class DriveNotificationManager
    : public BrowserContextKeyedService,
      public syncer::InvalidationHandler {
 public:
  explicit DriveNotificationManager(Profile* profile);
  virtual ~DriveNotificationManager();

  // BrowserContextKeyedService override.
  virtual void Shutdown() OVERRIDE;

  // syncer::InvalidationHandler implementation.
  virtual void OnInvalidatorStateChange(
      syncer::InvalidatorState state) OVERRIDE;
  virtual void OnIncomingInvalidation(
      const syncer::ObjectIdInvalidationMap& invalidation_map) OVERRIDE;

  void AddObserver(DriveNotificationObserver* observer);
  void RemoveObserver(DriveNotificationObserver* observer);

  // True when XMPP notification is currently enabled.
  bool push_notification_enabled() const {
    return push_notification_enabled_;
  }

  // True when XMPP notification has been registered.
  bool push_notification_registered() const {
    return push_notification_registered_;
  }

 private:
  enum NotificationSource {
    NOTIFICATION_XMPP,
    NOTIFICATION_POLLING,
  };

  // Restarts the polling timer. Used for polling-based notification.
  void RestartPollingTimer();

  // Notifies the observers that it's time to check for updates.
  // |source| indicates where the notification comes from.
  void NotifyObserversToUpdate(NotificationSource source);

  // Registers for Google Drive invalidation notifications through XMPP.
  void RegisterDriveNotifications();

  // Returns a string representation of NotificationSource.
  static std::string NotificationSourceToString(NotificationSource source);

  Profile* profile_;
  ObserverList<DriveNotificationObserver> observers_;

  // True when Drive File Sync Service is registered for Drive notifications.
  bool push_notification_registered_;
  // True if the XMPP-based push notification is currently enabled.
  bool push_notification_enabled_;
  // True once observers are notified for the first time.
  bool observers_notified_;

  // The timer is used for polling based notification. XMPP should usually be
  // used but notification is done per polling when XMPP is not working.
  base::Timer polling_timer_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<DriveNotificationManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DriveNotificationManager);
};

}  // namespace google_apis

#endif  // CHROME_BROWSER_GOOGLE_APIS_DRIVE_NOTIFICATION_MANAGER_H_
