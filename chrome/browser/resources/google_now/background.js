// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// 'use strict'; TODO(vadimt): Uncomment once crbug.com/237617 is fixed.

/**
 * @fileoverview The event page for Google Now for Chrome implementation.
 * The Google Now event page gets Google Now cards from the server and shows
 * them as Chrome notifications.
 * The service performs periodic updating of Google Now cards.
 * Each updating of the cards includes 4 steps:
 * 1. Obtaining the location of the machine;
 * 2. Processing requests for cards dismissals that are not yet sent to the
 *    server;
 * 3. Making a server request based on that location;
 * 4. Showing the received cards as notifications.
 */

// TODO(vadimt): Use background permission to show notifications even when all
// browser windows are closed.
// TODO(vadimt): Decide what to do in incognito mode.
// TODO(vadimt): Honor the flag the enables Google Now integration.
// TODO(vadimt): Figure out the final values of the constants.
// TODO(vadimt): Remove 'console' calls.
// TODO(vadimt): Consider sending JS stacks for chrome.* API errors and
// malformed server responses.

/**
 * Standard response code for successful HTTP requests. This is the only success
 * code the server will send.
 */
var HTTP_OK = 200;

/**
 * Initial period for polling for Google Now Notifications cards to use when the
 * period from the server is not available.
 */
var INITIAL_POLLING_PERIOD_SECONDS = 5 * 60;  // 5 minutes

/**
 * Maximal period for polling for Google Now Notifications cards to use when the
 * period from the server is not available.
 */
var MAXIMUM_POLLING_PERIOD_SECONDS = 60 * 60;  // 1 hour

/**
 * Initial period for retrying the server request for dismissing cards.
 */
var INITIAL_RETRY_DISMISS_PERIOD_SECONDS = 60;  // 1 minute

/**
 * Maximum period for retrying the server request for dismissing cards.
 */
var MAXIMUM_RETRY_DISMISS_PERIOD_SECONDS = 60 * 60;  // 1 hour

/**
 * Time we keep dismissals after successful server dismiss requests.
 */
var DISMISS_RETENTION_TIME_MS = 20 * 60 * 1000;  // 20 minutes

/**
 * Names for tasks that can be created by the extension.
 */
var UPDATE_CARDS_TASK_NAME = 'update-cards';
var DISMISS_CARD_TASK_NAME = 'dismiss-card';
var CARD_CLICKED_TASK_NAME = 'card-clicked';
var RETRY_DISMISS_TASK_NAME = 'retry-dismiss';

var LOCATION_WATCH_NAME = 'location-watch';

/**
 * Checks if a new task can't be scheduled when another task is already
 * scheduled.
 * @param {string} newTaskName Name of the new task.
 * @param {string} scheduledTaskName Name of the scheduled task.
 * @return {boolean} Whether the new task conflicts with the existing task.
 */
function areTasksConflicting(newTaskName, scheduledTaskName) {
  if (newTaskName == UPDATE_CARDS_TASK_NAME &&
      scheduledTaskName == UPDATE_CARDS_TASK_NAME) {
    // If a card update is requested while an old update is still scheduled, we
    // don't need the new update.
    return true;
  }

  if (newTaskName == RETRY_DISMISS_TASK_NAME &&
      (scheduledTaskName == UPDATE_CARDS_TASK_NAME ||
       scheduledTaskName == DISMISS_CARD_TASK_NAME ||
       scheduledTaskName == RETRY_DISMISS_TASK_NAME)) {
    // No need to schedule retry-dismiss action if another action that tries to
    // send dismissals is scheduled.
    return true;
  }

  return false;
}

var tasks = buildTaskManager(areTasksConflicting);

// Add error processing to API calls.
tasks.instrumentApiFunction(chrome.location.onLocationUpdate, 'addListener', 0);
tasks.instrumentApiFunction(chrome.notifications, 'create', 2);
tasks.instrumentApiFunction(chrome.notifications, 'update', 2);
tasks.instrumentApiFunction(
    chrome.notifications.onButtonClicked, 'addListener', 0);
tasks.instrumentApiFunction(chrome.notifications.onClicked, 'addListener', 0);
tasks.instrumentApiFunction(chrome.notifications.onClosed, 'addListener', 0);
tasks.instrumentApiFunction(chrome.runtime.onInstalled, 'addListener', 0);
tasks.instrumentApiFunction(chrome.runtime.onStartup, 'addListener', 0);
tasks.instrumentApiFunction(chrome.tabs, 'create', 1);
tasks.instrumentApiFunction(storage, 'get', 1);

var updateCardsAttempts = buildAttemptManager(
    'cards-update',
    requestLocation,
    INITIAL_POLLING_PERIOD_SECONDS,
    MAXIMUM_POLLING_PERIOD_SECONDS);
var dismissalAttempts = buildAttemptManager(
    'dismiss',
    retryPendingDismissals,
    INITIAL_RETRY_DISMISS_PERIOD_SECONDS,
    MAXIMUM_RETRY_DISMISS_PERIOD_SECONDS);

/**
 * Diagnostic event identifier.
 * @enum {number}
 */
var DiagnosticEvent = {
  REQUEST_FOR_CARDS_TOTAL: 0,
  REQUEST_FOR_CARDS_SUCCESS: 1,
  CARDS_PARSE_SUCCESS: 2,
  DISMISS_REQUEST_TOTAL: 3,
  DISMISS_REQUEST_SUCCESS: 4,
  EVENTS_TOTAL: 5  // EVENTS_TOTAL is not an event; all new events need to be
                   // added before it.
};

/**
 * Records a diagnostic event.
 * @param {DiagnosticEvent} event Event identifier.
 */
function recordEvent(event) {
  var metricDescription = {
    metricName: 'GoogleNow.Event',
    type: 'histogram-linear',
    min: 1,
    max: DiagnosticEvent.EVENTS_TOTAL,
    buckets: DiagnosticEvent.EVENTS_TOTAL + 1
  };

  chrome.metricsPrivate.recordValue(metricDescription, event);
}

/**
 * Shows a notification and remembers information associated with it.
 * @param {Object} card Google Now card represented as a set of parameters for
 *     showing a Chrome notification.
 * @param {Object} notificationsData Map from notification id to the data
 *     associated with a notification.
 * @param {number} previousVersion The version of the shown card with this id,
 *     if it exists, undefined otherwise.
 */
function showNotification(card, notificationsData, previousVersion) {
  console.log('showNotification ' + JSON.stringify(card) + ' ' +
             previousVersion);

  if (typeof card.version != 'number') {
    console.error('card.version is not a number');
    // Fix card version.
    card.version = previousVersion !== undefined ? previousVersion : 0;
  }

  if (previousVersion !== card.version) {
    try {
      // Delete a notification with the specified id if it already exists, and
      // then create a notification.
      chrome.notifications.create(
          card.notificationId,
          card.notification,
          function(notificationId) {
            if (!notificationId || chrome.runtime.lastError) {
              var errorMessage =
                  chrome.runtime.lastError && chrome.runtime.lastError.message;
              console.error('notifications.create: ID=' + notificationId +
                            ', ERROR=' + errorMessage);
            }
          });
    } catch (error) {
      console.error('Error in notifications.create: ' + error);
    }
  } else {
    try {
      // Update existing notification.
      chrome.notifications.update(
          card.notificationId,
          card.notification,
          function(wasUpdated) {
            if (!wasUpdated || chrome.runtime.lastError) {
              var errorMessage =
                  chrome.runtime.lastError && chrome.runtime.lastError.message;
              console.error('notifications.update: UPDATED=' + wasUpdated +
                            ', ERROR=' + errorMessage);
            }
          });
    } catch (error) {
      console.error('Error in notifications.update: ' + error);
    }
  }

  notificationsData[card.notificationId] = {
    actionUrls: card.actionUrls,
    version: card.version
  };
}

/**
 * Parses JSON response from the notification server, show notifications and
 * schedule next update.
 * @param {string} response Server response.
 * @param {function()} callback Completion callback.
 */
function parseAndShowNotificationCards(response, callback) {
  console.log('parseAndShowNotificationCards ' + response);
  try {
    var parsedResponse = JSON.parse(response);
  } catch (error) {
    console.error('parseAndShowNotificationCards parse error: ' + error);
    callback();
    return;
  }

  var cards = parsedResponse.cards;

  if (!(cards instanceof Array)) {
    callback();
    return;
  }

  if (typeof parsedResponse.expiration_timestamp_seconds != 'number') {
    callback();
    return;
  }

  tasks.debugSetStepName('parseAndShowNotificationCards-storage-get');
  storage.get(['activeNotifications', 'recentDismissals'], function(items) {
    console.log('parseAndShowNotificationCards-get ' + JSON.stringify(items));
    items.activeNotifications = items.activeNotifications || {};
    items.recentDismissals = items.recentDismissals || {};

    // Build a set of non-expired recent dismissals. It will be used for
    // client-side filtering of cards.
    var updatedRecentDismissals = {};
    var currentTimeMs = Date.now();
    for (var notificationId in items.recentDismissals) {
      if (currentTimeMs - items.recentDismissals[notificationId] <
          DISMISS_RETENTION_TIME_MS) {
        updatedRecentDismissals[notificationId] =
            items.recentDismissals[notificationId];
      }
    }

    // Mark existing notifications that received an update in this server
    // response.
    for (var i = 0; i < cards.length; ++i) {
      var notificationId = cards[i].notificationId;
      if (!(notificationId in updatedRecentDismissals) &&
          notificationId in items.activeNotifications) {
        items.activeNotifications[notificationId].hasUpdate = true;
      }
    }

    // Delete notifications that didn't receive an update.
    for (var notificationId in items.activeNotifications) {
      console.log('parseAndShowNotificationCards-delete-check ' +
                  notificationId);
      if (!items.activeNotifications[notificationId].hasUpdate) {
        console.log('parseAndShowNotificationCards-delete ' + notificationId);
        chrome.notifications.clear(
            notificationId,
            function() {});
      }
    }

    recordEvent(DiagnosticEvent.CARDS_PARSE_SUCCESS);

    // Create/update notifications and store their new properties.
    var notificationsData = {};
    for (var i = 0; i < cards.length; ++i) {
      var card = cards[i];
      if (!(card.notificationId in updatedRecentDismissals)) {
        var activeNotification = items.activeNotifications[card.notificationId];
        showNotification(card,
                         notificationsData,
                         activeNotification && activeNotification.version);
      }
    }

    updateCardsAttempts.start(parsedResponse.expiration_timestamp_seconds);

    storage.set({
      activeNotifications: notificationsData,
      recentDismissals: updatedRecentDismissals
    });
    callback();
  });
}

/**
 * Requests notification cards from the server.
 * @param {Location} position Location of this computer.
 * @param {function()} callback Completion callback.
 */
function requestNotificationCards(position, callback) {
  console.log('requestNotificationCards ' + JSON.stringify(position) +
      ' from ' + NOTIFICATION_CARDS_URL);

  if (!NOTIFICATION_CARDS_URL) {
    callback();
    return;
  }

  recordEvent(DiagnosticEvent.REQUEST_FOR_CARDS_TOTAL);

  // TODO(vadimt): Should we use 'q' as the parameter name?
  var requestParameters =
      'q=' + position.coords.latitude +
      ',' + position.coords.longitude +
      ',' + position.coords.accuracy;

  // TODO(vadimt): Figure out how to send user's identity to the server.
  var request = buildServerRequest('notifications');

  request.onloadend = tasks.wrapCallback(function(event) {
    console.log('requestNotificationCards-onloadend ' + request.status);
    if (request.status == HTTP_OK) {
      recordEvent(DiagnosticEvent.REQUEST_FOR_CARDS_SUCCESS);
      parseAndShowNotificationCards(request.response, callback);
    } else {
      callback();
    }
  });

  tasks.debugSetStepName('requestNotificationCards-send-request');
  request.send(requestParameters);
}

/**
 * Starts getting location for a cards update.
 */
function requestLocation() {
  console.log('requestLocation');
  // TODO(vadimt): Figure out location request options.
  chrome.location.watchLocation(LOCATION_WATCH_NAME, {});
}


/**
 * Obtains new location; requests and shows notification cards based on this
 * location.
 * @param {Location} position Location of this computer.
 */
function updateNotificationsCards(position) {
  console.log('updateNotificationsCards ' + JSON.stringify(position) +
      ' @' + new Date());
  tasks.add(UPDATE_CARDS_TASK_NAME, function(callback) {
    console.log('updateNotificationsCards-task-begin');
    updateCardsAttempts.planForNext(function() {
      processPendingDismissals(function(success) {
        if (success) {
          // The cards are requested only if there are no unsent dismissals.
          requestNotificationCards(position, callback);
        } else {
          callback();
        }
      });
    });
  });
}

/**
 * Sends a server request to dismiss a card.
 * @param {string} notificationId Unique identifier of the card.
 * @param {number} dismissalTimeMs Time of the user's dismissal of the card in
 *     milliseconds since epoch.
 * @param {function(boolean)} callbackBoolean Completion callback with 'success'
 *     parameter.
 */
function requestCardDismissal(
    notificationId, dismissalTimeMs, callbackBoolean) {
  console.log('requestDismissingCard ' + notificationId + ' from ' +
      NOTIFICATION_CARDS_URL);
  recordEvent(DiagnosticEvent.DISMISS_REQUEST_TOTAL);
  // Send a dismiss request to the server.
  var requestParameters = 'id=' + notificationId +
                          '&dismissalAge=' + (Date.now() - dismissalTimeMs);
  var request = buildServerRequest('dismiss');
  request.onloadend = tasks.wrapCallback(function(event) {
    console.log('requestDismissingCard-onloadend ' + request.status);
    if (request.status == HTTP_OK)
      recordEvent(DiagnosticEvent.DISMISS_REQUEST_SUCCESS);

    callbackBoolean(request.status == HTTP_OK);
  });

  tasks.debugSetStepName('requestCardDismissal-send-request');
  request.send(requestParameters);
}

/**
 * Tries to send dismiss requests for all pending dismissals.
 * @param {function(boolean)} callbackBoolean Completion callback with 'success'
 *     parameter. Success means that no pending dismissals are left.
 */
function processPendingDismissals(callbackBoolean) {
  tasks.debugSetStepName('processPendingDismissals-storage-get');
  storage.get(['pendingDismissals', 'recentDismissals'], function(items) {
    console.log('processPendingDismissals-storage-get ' +
                JSON.stringify(items));
    items.pendingDismissals = items.pendingDismissals || [];
    items.recentDismissals = items.recentDismissals || {};

    var dismissalsChanged = false;

    function onFinish(success) {
      if (dismissalsChanged) {
        storage.set({
          pendingDismissals: items.pendingDismissals,
          recentDismissals: items.recentDismissals
        });
      }
      callbackBoolean(success);
    }

    function doProcessDismissals() {
      if (items.pendingDismissals.length == 0) {
        dismissalAttempts.stop();
        onFinish(true);
        return;
      }

      // Send dismissal for the first card, and if successful, repeat
      // recursively with the rest.
      var dismissal = items.pendingDismissals[0];
      requestCardDismissal(
          dismissal.notificationId, dismissal.time, function(success) {
        if (success) {
          dismissalsChanged = true;
          items.pendingDismissals.splice(0, 1);
          items.recentDismissals[dismissal.notificationId] = Date.now();
          doProcessDismissals();
        } else {
          onFinish(false);
        }
      });
    }

    doProcessDismissals();
  });
}

/**
 * Submits a task to send pending dismissals.
 */
function retryPendingDismissals() {
  tasks.add(RETRY_DISMISS_TASK_NAME, function(callback) {
    dismissalAttempts.planForNext(function() {
      processPendingDismissals(function(success) { callback(); });
     });
  });
}

/**
 * Opens URL corresponding to the clicked part of the notification.
 * @param {string} notificationId Unique identifier of the notification.
 * @param {function(Object): string} selector Function that extracts the url for
 *     the clicked area from the button action URLs info.
 */
function onNotificationClicked(notificationId, selector) {
  tasks.add(CARD_CLICKED_TASK_NAME, function(callback) {
    tasks.debugSetStepName('onNotificationClicked-get-activeNotifications');
    storage.get('activeNotifications', function(items) {
      items.activeNotifications = items.activeNotifications || {};

      var actionUrls = items.activeNotifications[notificationId].actionUrls;
      if (typeof actionUrls != 'object') {
        callback();
        return;
      }

      var url = selector(actionUrls);

      if (typeof url != 'string') {
        callback();
        return;
      }

      chrome.tabs.create({url: url}, function(tab) {
        if (!tab)
          chrome.windows.create({url: url});
      });
      callback();
    });
  });
}

/**
 * Callback for chrome.notifications.onClosed event.
 * @param {string} notificationId Unique identifier of the notification.
 * @param {boolean} byUser Whether the notification was closed by the user.
 */
function onNotificationClosed(notificationId, byUser) {
  if (!byUser)
    return;

  chrome.metricsPrivate.recordUserAction('GoogleNow.Dismissed');

  tasks.add(DISMISS_CARD_TASK_NAME, function(callback) {
    dismissalAttempts.start();

    // Deleting the notification in case it was re-added while this task was
    // scheduled, waiting for execution.
    chrome.notifications.clear(
        notificationId,
        function() {});

    tasks.debugSetStepName('onNotificationClosed-get-pendingDismissals');
    storage.get('pendingDismissals', function(items) {
      items.pendingDismissals = items.pendingDismissals || [];

      var dismissal = {
        notificationId: notificationId,
        time: Date.now()
      };
      items.pendingDismissals.push(dismissal);
      storage.set({pendingDismissals: items.pendingDismissals});
      processPendingDismissals(function(success) { callback(); });
    });
  });
}

/**
 * Initializes the event page on install or on browser startup.
 */
function initialize() {
  // Create an update timer for a case when for some reason location request
  // gets stuck.
  updateCardsAttempts.start(MAXIMUM_POLLING_PERIOD_SECONDS);

  var initialStorage = {
    activeNotifications: {}
  };
  storage.set(initialStorage);

  requestLocation();
}

chrome.runtime.onInstalled.addListener(function(details) {
  console.log('onInstalled ' + JSON.stringify(details));
  if (details.reason != 'chrome_update') {
    initialize();
  }
});

chrome.runtime.onStartup.addListener(function() {
  console.log('onStartup');
  initialize();
});

chrome.notifications.onClicked.addListener(
    function(notificationId) {
      chrome.metricsPrivate.recordUserAction('GoogleNow.MessageClicked');
      onNotificationClicked(notificationId, function(actionUrls) {
        return actionUrls.messageUrl;
      });
    });

chrome.notifications.onButtonClicked.addListener(
    function(notificationId, buttonIndex) {
      chrome.metricsPrivate.recordUserAction(
          'GoogleNow.ButtonClicked' + buttonIndex);
      onNotificationClicked(notificationId, function(actionUrls) {
        if (!Array.isArray(actionUrls.buttonUrls))
          return undefined;

        return actionUrls.buttonUrls[buttonIndex];
      });
    });

chrome.notifications.onClosed.addListener(onNotificationClosed);

chrome.location.onLocationUpdate.addListener(updateNotificationsCards);
