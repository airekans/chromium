// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var afterWhitelistExtension = function(msg) {
  chrome.tabCapture.capture({audio: true, video: true}, function(stream) {
    chrome.test.assertTrue(!!stream);
    stream.stop();
    chrome.test.succeed();
  });
};

var afterOpenNewTab = function(msg) {
  chrome.tabCapture.capture({audio: true, video: true}, function(stream) {
    chrome.test.assertTrue(!stream);
    chrome.test.sendMessage('ready4', afterWhitelistExtension);
  });
};

var afterGrantPermission = function(msg) {
  chrome.tabCapture.capture({audio: true, video: true}, function(stream) {
    chrome.test.assertTrue(!!stream);
    stream.stop();
    chrome.test.sendMessage('ready3', afterOpenNewTab);
  });
};

var afterOpenTab = function(msg) {
  chrome.tabCapture.capture({audio: true, video: true}, function(stream) {
    chrome.test.assertLastError(
      'Extension has not been invoked for the current page (see activeTab ' +
      'permission). Chrome pages cannot be captured.');
    chrome.test.assertTrue(!stream);

    chrome.test.sendMessage('ready2', afterGrantPermission);
  });
};

chrome.test.notifyPass();
chrome.test.sendMessage('ready1', afterOpenTab);
