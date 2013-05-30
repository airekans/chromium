// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_MESSAGE_CENTER_TRAY_BRIDGE_H_
#define CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_MESSAGE_CENTER_TRAY_BRIDGE_H_

#import <AppKit/AppKit.h>

#include "base/basictypes.h"
#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/message_center/message_center_tray_delegate.h"

@class MCPopupCollection;
@class MCStatusItemView;
@class MCTrayController;

namespace message_center {
class MessageCenter;
class MessageCenterTray;
}

// MessageCenterTrayBridge is the owner of all the Cocoa UI objects for the
// message_center. It bridges C++ notifications from the MessageCenterTray to
// the various UI objects.
class MessageCenterTrayBridge :
    public message_center::MessageCenterTrayDelegate {
 public:
  explicit MessageCenterTrayBridge(
      message_center::MessageCenter* message_center);
  virtual ~MessageCenterTrayBridge();

  // message_center::MessageCenterTrayDelegate:
  virtual void OnMessageCenterTrayChanged() OVERRIDE;
  virtual bool ShowPopups() OVERRIDE;
  virtual void HidePopups() OVERRIDE;
  virtual void UpdatePopups() OVERRIDE;
  virtual bool ShowMessageCenter() OVERRIDE;
  virtual void HideMessageCenter() OVERRIDE;

  message_center::MessageCenter* message_center() { return message_center_; }

 private:
  // Updates the unread count on the status item.
  void UpdateStatusItem();

  // The global, singleton message center model object. Weak.
  message_center::MessageCenter* message_center_;

  // C++ controller for the notification tray UI.
  scoped_ptr<message_center::MessageCenterTray> tray_;

  // Obj-C window controller for the notification tray.
  scoped_nsobject<MCTrayController> tray_controller_;

  // Whether or not the |tray_controller_| needs to be updated the next time
  // it is opened.
  bool updates_pending_;

  // View that is displayed on the system menu bar item.
  scoped_nsobject<MCStatusItemView> status_item_view_;

  // Obj-C controller for the on-screen popup notifications.
  scoped_nsobject<MCPopupCollection> popup_collection_;

  // Weak pointer factory to posts tasks to self.
  base::WeakPtrFactory<MessageCenterTrayBridge> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(MessageCenterTrayBridge);
};

#endif  // CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_MESSAGE_CENTER_TRAY_BRIDGE_H_
