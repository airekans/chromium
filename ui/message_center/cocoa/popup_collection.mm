// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/message_center/cocoa/popup_collection.h"

#import "ui/message_center/cocoa/notification_controller.h"
#import "ui/message_center/cocoa/popup_controller.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_observer.h"
#include "ui/message_center/message_center_style.h"

namespace {
const float kAnimationDuration = 0.2;
}  // namespace

@interface MCPopupCollection (Private)
// Returns the primary screen's visible frame rectangle.
- (NSRect)screenFrame;

// Shows a popup, if there is room on-screen, for the given notification.
// Returns YES if the notification was actually displayed.
- (BOOL)addNotification:(const message_center::Notification*)notification;

// Updates the contents of the notification with the given ID.
- (void)updateNotification:(const std::string&)notificationID;

// Removes a popup from the screen and lays out new notifications that can
// now potentially fit on the screen.
- (void)removeNotification:(const std::string&)notificationID;

// Closes all the popups.
- (void)removeAllNotifications;

// Returns the index of the popup showing the notification with the given ID.
- (NSUInteger)indexOfPopupWithNotificationID:(const std::string&)notificationID;

// Repositions all popup notifications if needed.
- (void)layoutNotifications;

// Fits as many new notifications as possible on screen.
- (void)layoutNewNotifications;

// Process notifications pending to remove when no animation is being played.
- (void)processPendingRemoveNotifications;

// Process notifications pending to update when no animation is being played.
- (void)processPendingUpdateNotifications;
@end

namespace {

class PopupCollectionObserver : public message_center::MessageCenterObserver {
 public:
  PopupCollectionObserver(message_center::MessageCenter* message_center,
                          MCPopupCollection* popup_collection)
      : message_center_(message_center),
        popup_collection_(popup_collection) {
    message_center_->AddObserver(this);
  }

  virtual ~PopupCollectionObserver() {
    message_center_->RemoveObserver(this);
  }

  virtual void OnNotificationAdded(
      const std::string& notification_id) OVERRIDE {
    [popup_collection_ layoutNewNotifications];
  }

  virtual void OnNotificationRemoved(const std::string& notification_id,
                                     bool user_id) OVERRIDE {
    [popup_collection_ removeNotification:notification_id];
  }

  virtual void OnNotificationUpdated(
      const std::string& notification_id) OVERRIDE {
    [popup_collection_ updateNotification:notification_id];
  }

 private:
  message_center::MessageCenter* message_center_;  // Weak, global.

  MCPopupCollection* popup_collection_;  // Weak, owns this.
};

}  // namespace

@implementation MCPopupCollection

- (id)initWithMessageCenter:(message_center::MessageCenter*)messageCenter {
  if ((self = [super init])) {
    messageCenter_ = messageCenter;
    observer_.reset(new PopupCollectionObserver(messageCenter_, self));
    popups_.reset([[NSMutableArray alloc] init]);
    popupAnimationDuration_ = kAnimationDuration;
  }
  return self;
}

- (void)dealloc {
  [self removeAllNotifications];
  [super dealloc];
}

- (BOOL)isAnimating {
  return !animatingNotificationIDs_.empty();
}

- (NSTimeInterval)popupAnimationDuration {
  return popupAnimationDuration_;
}

- (void)onPopupAnimationEnded:(const std::string&)notificationID {
  animatingNotificationIDs_.erase(notificationID);
  if (![self isAnimating])
    [self layoutNotifications];

  // Quit the test run loop when no more animation is being played.
  if (![self isAnimating] && testingAnimationEndedCallback_)
    testingAnimationEndedCallback_.get()();
}

// Testing API /////////////////////////////////////////////////////////////////

- (NSArray*)popups {
  return popups_.get();
}

- (void)setScreenFrame:(NSRect)frame {
  testingScreenFrame_ = frame;
}

- (void)setAnimationDuration:(NSTimeInterval)duration {
  popupAnimationDuration_ = duration;
}

- (void)setAnimationEndedCallback:
    (message_center::AnimationEndedCallback)callback {
  testingAnimationEndedCallback_.reset(Block_copy(callback));
}

// Private /////////////////////////////////////////////////////////////////////

- (NSRect)screenFrame {
  if (!NSIsEmptyRect(testingScreenFrame_))
    return testingScreenFrame_;
  return [[[NSScreen screens] objectAtIndex:0] visibleFrame];
}

- (BOOL)addNotification:(const message_center::Notification*)notification {
  // Wait till all existing animations end.
  if ([self isAnimating])
    return NO;

  // The popup is owned by itself. It will be released at close.
  MCPopupController* popup =
      [[MCPopupController alloc] initWithNotification:notification
                                        messageCenter:messageCenter_
                                      popupCollection:self];

  NSRect screenFrame = [self screenFrame];
  NSRect popupFrame = [popup bounds];

  CGFloat x = NSMaxX(screenFrame) - message_center::kMarginBetweenItems -
      NSWidth(popupFrame);
  CGFloat y = 0;

  MCPopupController* bottomPopup = [popups_ lastObject];
  if (!bottomPopup) {
    y = NSMaxY(screenFrame);
  } else {
    y = NSMinY([bottomPopup bounds]);
  }

  y -= message_center::kMarginBetweenItems + NSHeight(popupFrame);

  if (y > NSMinY(screenFrame)) {
    animatingNotificationIDs_.insert(notification->id());
    NSRect bounds = [popup bounds];
    bounds.origin.x = x;
    bounds.origin.y = y;
    [popup showWithAnimation:bounds];
    [popups_ addObject:popup];
    return YES;
  }

  // The popup cannot fit on screen, so it has to be released now.
  [popup release];
  return NO;
}

- (void)updateNotification:(const std::string&)notificationID {
  // The notification may not be on screen.
  if ([self indexOfPopupWithNotificationID:notificationID] == NSNotFound)
    return;

  // Don't bother with the update if the notification is going to be removed.
  if (pendingRemoveNotificationIDs_.find(notificationID) !=
          pendingRemoveNotificationIDs_.end()) {
    return;
  }

  pendingUpdateNotificationIDs_.insert(notificationID);
  [self processPendingUpdateNotifications];
}

- (void)removeNotification:(const std::string&)notificationID {
  // The notification may not be on screen.
  if ([self indexOfPopupWithNotificationID:notificationID] == NSNotFound)
    return;

  // Don't bother with the update if the notification is going to be removed.
  pendingUpdateNotificationIDs_.erase(notificationID);

  pendingRemoveNotificationIDs_.insert(notificationID);
  [self processPendingRemoveNotifications];
}

- (void)removeAllNotifications {
  [popups_ makeObjectsPerformSelector:@selector(closeWithAnimation)];
  [popups_ makeObjectsPerformSelector:@selector(markPopupCollectionGone)];
  [popups_ removeAllObjects];
}

- (NSUInteger)indexOfPopupWithNotificationID:
    (const std::string&)notificationID {
  return [popups_ indexOfObjectPassingTest:
      ^BOOL(id popup, NSUInteger index, BOOL* stop) {
          return [popup notificationID] == notificationID;
      }];
}

- (void)layoutNotifications {
  // Wait till all existing animations end.
  if ([self isAnimating])
    return;

  NSRect screenFrame = [self screenFrame];

  // The popup starts at top-right corner.
  CGFloat maxY = NSMaxY(screenFrame);

  // Iterate all notifications and reposition each if needed. If one does not
  // fit on screen, close it and any other on-screen popups that come after it.
  NSUInteger removeAt = NSNotFound;
  for (NSUInteger i = 0; i < [popups_ count]; ++i) {
    MCPopupController* popup = [popups_ objectAtIndex:i];
    NSRect oldFrame = [popup bounds];
    NSRect frame = oldFrame;
    frame.origin.y = maxY - message_center::kMarginBetweenItems -
                     NSHeight(frame);

    // If this popup does not fit on screen, stop repositioning and close this
    // and subsequent popups.
    if (NSMinY(frame) < NSMinY(screenFrame)) {
      removeAt = i;
      break;
    }

    if (!NSEqualRects(frame, oldFrame)) {
      [popup setBounds:frame];
      animatingNotificationIDs_.insert([popup notificationID]);
    }

    // Set the new maximum Y to be the bottom of this notification.
    maxY = NSMinY(frame);
  }

  if (removeAt != NSNotFound) {
    // Remove any popups that are on screen but no longer fit.
    while ([popups_ count] >= removeAt && [popups_ count]) {
      [[popups_ lastObject] close];
      [popups_ removeLastObject];
    }
  } else {
    [self layoutNewNotifications];
  }

  [self processPendingRemoveNotifications];
  [self processPendingUpdateNotifications];
}

- (void)layoutNewNotifications {
  // Wait till all existing animations end.
  if ([self isAnimating])
    return;

  // Display any new popups that can now fit on screen, starting from the
  // oldest notification that has not been shown up.
  const auto& allPopups = messageCenter_->GetPopupNotifications();
  for (auto it = allPopups.rbegin(); it != allPopups.rend(); ++it) {
    if ([self indexOfPopupWithNotificationID:(*it)->id()] == NSNotFound) {
      // If there's no room left on screen to display notifications, stop
      // trying.
      if (![self addNotification:*it])
        break;
    }
  }
}

- (void)processPendingRemoveNotifications {
  // Wait till all existing animations end.
  if ([self isAnimating])
    return;

  for (const auto& notificationID : pendingRemoveNotificationIDs_) {
    NSUInteger index = [self indexOfPopupWithNotificationID:notificationID];
    if (index != NSNotFound) {
      [[popups_ objectAtIndex:index] closeWithAnimation];
      animatingNotificationIDs_.insert(notificationID);
      [popups_ removeObjectAtIndex:index];
    }
  }
  pendingRemoveNotificationIDs_.clear();
}

- (void)processPendingUpdateNotifications {
  // Wait till all existing animations end.
  if ([self isAnimating])
    return;

  if (pendingUpdateNotificationIDs_.empty())
    return;

  // Go through all model objects in the message center. If there is a replaced
  // notification, the controller's current model object may be stale.
  const auto& modelPopups = messageCenter_->GetPopupNotifications();
  for (auto iter = modelPopups.begin(); iter != modelPopups.end(); ++iter) {
    const std::string& notificationID = (*iter)->id();

    // Does the notification need to be updated?
    if (pendingUpdateNotificationIDs_.find(notificationID) ==
            pendingUpdateNotificationIDs_.end()) {
      continue;
    }

    // Is the notification still on screen?
    NSUInteger index = [self indexOfPopupWithNotificationID:notificationID];
    if (index == NSNotFound)
      continue;

    MCPopupController* popup = [popups_ objectAtIndex:index];

    CGFloat oldHeight =
        NSHeight([[[popup notificationController] view] frame]);
    CGFloat newHeight = NSHeight(
        [[popup notificationController] updateNotification:*iter]);

    // The notification has changed height. This requires updating the popup
    // window.
    if (oldHeight != newHeight) {
      NSRect popupFrame = [popup bounds];
      popupFrame.origin.y -= newHeight - oldHeight;
      popupFrame.size.height += newHeight - oldHeight;
      [popup setBounds:popupFrame];
    }
  }

  pendingUpdateNotificationIDs_.clear();

  // Start re-layout of all notifications, so that it readjusts the Y origin of
  // all updated popups and any popups that come below them.
  [self layoutNotifications];
}

@end
