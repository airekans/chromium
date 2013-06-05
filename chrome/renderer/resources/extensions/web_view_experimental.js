// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shim extension to provide permission request API (and possibly other future
// experimental APIs) for <webview> tag.
// See web_view.js for details.
//
// We want to control the permission API feature in <webview> separately from
// the <webview> feature itself. <webview> is available in stable channel, but
// permission API would only be available for channels CHANNEL_DEV and
// CHANNEL_CANARY.

var WebView = require('webView').WebView;
var GetExtensionAPIDefinitions =
      requireNative('apiDefinitions').GetExtensionAPIDefinitions;
var WebRequestEvent = require('webRequest').WebRequestEvent;
var forEach = require('utils').forEach;

/** @type {Array.<string>} */
var PERMISSION_TYPES = ['download', 'media', 'geolocation', 'pointerLock'];

/** @type {string} */
var REQUEST_TYPE_NEW_WINDOW = 'newwindow';

/** @type {string} */
var ERROR_MSG_PERMISSION_ALREADY_DECIDED = '<webview>: ' +
    'Permission has already been decided for this "permissionrequest" event.';

var EXPOSED_PERMISSION_EVENT_ATTRIBS = [
    'lastUnlockedBySelf',
    'permission',
    'requestMethod',
    'url',
    'userGesture'
];

/** @type {string} */
var ERROR_MSG_NEWWINDOW_ACTION_ALREADY_TAKEN = '<webview>: ' +
    'An action has already been taken for this "newwindow" event.';

/** @type {string} */
var ERROR_MSG_WEBVIEW_EXPECTED = '<webview> element expected.';

/**
 * @private
 */
WebView.prototype.maybeSetupExperimentalAPI_ = function() {
  this.setupNewWindowEvent_();
  this.setupPermissionEvent_();
  this.setupExecuteCodeAPI_();
  this.setupWebRequestEvents_();
}

/**
 * @param {!Object} detail The event details, originated from <object>.
 * @private
 */
WebView.prototype.setupPermissionEvent_ = function() {
  var node = this.webviewNode_;
  var browserPluginNode = this.browserPluginNode_;
  var internalevent = '-internal-permissionrequest';
  browserPluginNode.addEventListener(internalevent, function(e) {
    var evt = new Event('permissionrequest', {bubbles: true, cancelable: true});
    var detail = e.detail ? JSON.parse(e.detail) : {};
    forEach(EXPOSED_PERMISSION_EVENT_ATTRIBS, function(i, attribName) {
      if (detail[attribName] !== undefined)
        evt[attribName] = detail[attribName];
    });
    var requestId = detail.requestId;

    if (detail.requestId !== undefined &&
        PERMISSION_TYPES.indexOf(detail.permission) >= 0) {
      // TODO(lazyboy): Also fill in evt.details (see webview specs).
      // http://crbug.com/141197.
      var decisionMade = false;
      // Construct the event.request object.
      var request = {
        allow: function() {
          if (decisionMade) {
            throw new Error(ERROR_MSG_PERMISSION_ALREADY_DECIDED);
          } else {
            browserPluginNode['-internal-setPermission'](requestId, true);
            decisionMade = true;
          }
        },
        deny: function() {
          if (decisionMade) {
            throw new Error(ERROR_MSG_PERMISSION_ALREADY_DECIDED);
          } else {
            browserPluginNode['-internal-setPermission'](requestId, false);
            decisionMade = true;
          }
        }
      };
      evt.request = request;

      // Make browser plugin track lifetime of |request|.
      browserPluginNode['-internal-persistObject'](
          request, detail.permission, requestId);

      var defaultPrevented = !node.dispatchEvent(evt);
      if (!decisionMade && !defaultPrevented) {
        decisionMade = true;
        browserPluginNode['-internal-setPermission'](requestId, false);
      }
    }
  });
};

/**
 * @private
 */
WebView.prototype.setupExecuteCodeAPI_ = function() {
  var self = this;
  this.webviewNode_['executeScript'] = function(var_args) {
    var args = [self.browserPluginNode_.getGuestInstanceId()].concat(
                    Array.prototype.slice.call(arguments));
    chrome.webview.executeScript.apply(null, args);
  }
  this.webviewNode_['insertCSS'] = function(var_args) {
    var args = [self.browserPluginNode_.getGuestInstanceId()].concat(
                    Array.prototype.slice.call(arguments));
    chrome.webview.insertCSS.apply(null, args);
  }
};

/**
 * @private
 */
WebView.prototype.setupNewWindowEvent_ = function() {
  var NEW_WINDOW_EVENT_ATTRIBUTES = [
    'initialHeight',
    'initialWidth',
    'targetUrl',
    'windowOpenDisposition',
    'name'
  ];

  var node = this.webviewNode_;
  var browserPluginNode = this.browserPluginNode_;
  browserPluginNode.addEventListener('-internal-newwindow', function(e) {
    var evt = new Event('newwindow', { bubbles: true, cancelable: true });
    var detail = e.detail ? JSON.parse(e.detail) : {};

    NEW_WINDOW_EVENT_ATTRIBUTES.forEach(function(attribName) {
      evt[attribName] = detail[attribName];
    });
    var requestId = detail.requestId;
    var actionTaken = false;

    var validateCall = function () {
      if (actionTaken)
        throw new Error(ERROR_MSG_NEWWINDOW_ACTION_ALREADY_TAKEN);
      actionTaken = true;
    };

    var window = {
      attach: function(webview) {
        validateCall();
        if (!webview)
          throw new Error(ERROR_MSG_WEBVIEW_EXPECTED);
        // Attach happens asynchronously to give the tagWatcher an opportunity
        // to pick up the new webview before attach operates on it, if it hasn't
        // been attached to the DOM already.
        // Note: Any subsequent errors cannot be exceptions because they happen
        // asynchronously.
        setTimeout(function() {
          var attached =
              browserPluginNode['-internal-attachWindowTo'](webview,
                                                            detail.windowId);
          if (!attached) {
            console.error('Unable to attach the new window to the provided ' +
                'webview.');
          }
          // If the object being passed into attach is not a valid <webview>
          // then we will fail and it will be treated as if the new window
          // was rejected. The permission API plumbing is used here to clean
          // up the state created for the new window if attaching fails.
          browserPluginNode['-internal-setPermission'](requestId, attached);
        }, 0);
      },
      discard: function() {
        validateCall();
        browserPluginNode['-internal-setPermission'](requestId, false);
      }
    };
    evt.window = window;
    // Make browser plugin track lifetime of |window|.
    browserPluginNode['-internal-persistObject'](
        window, detail.permission, requestId);

    var defaultPrevented = !node.dispatchEvent(evt);
    if (!actionTaken && !defaultPrevented) {
      actionTaken = true;
      // The default action is to discard the window.
      browserPluginNode['-internal-setPermission'](requestId, false);
      console.warn('<webview>: A new window was blocked.');
    }
  });
}

/**
 * @private
 */
WebView.prototype.setupWebRequestEvents_ = function() {
  var self = this;
  // Populate the WebRequest events from the API definition.
  var webRequestDefinition = GetExtensionAPIDefinitions().filter(function(api) {
    return api.namespace == 'webRequest';
  })[0];
  for (var i = 0; i < webRequestDefinition.events.length; ++i) {
    Object.defineProperty(self.webviewNode_,
                          webRequestDefinition.events[i].name, {
      get: function(webRequestEvent) {
        return function() {
          if (!self[webRequestEvent.name + '_']) {
            self[webRequestEvent.name + '_'] =
                new WebRequestEvent(
                    'webview.' + webRequestEvent.name,
                    webRequestEvent.parameters,
                    webRequestEvent.extraParameters, null,
                    self.browserPluginNode_.getInstanceId());
          }
          return self[webRequestEvent.name + '_'];
        }
      }(webRequestDefinition.events[i]),
      // No setter.
      enumerable: true
    });
  }
};
