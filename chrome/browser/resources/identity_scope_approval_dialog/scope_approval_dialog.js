// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var webview;

/**
 * Points the webview to the starting URL of a scope authorization
 * flow, and unhides the dialog once the page has loaded.
 * @param {string} url The url of the authorization entry point.
 * @param {Object} win The dialog window that contains this page. Can
 *     be left undefined if the caller does not want to display the
 *     window.
 */
function loadAuthUrlAndShowWindow(url, win) {
  webview.src = url;
  if (win) {
    webview.addEventListener('loadstop', function() {
      win.show();
    });
  }
}

document.addEventListener('DOMContentLoaded', function() {
  webview = document.querySelector('webview');

  document.querySelector('.titlebar-close-button').onclick = function() {
    window.close();
  };

  chrome.identityPrivate.getResources(function(resources) {
    var style = document.styleSheets[0];

    function insertRule(selector, url) {
      style.insertRule(selector + ' { background-image: url(' + url + '); }',
                       style.cssRules.length);
    }

    insertRule('.titlebar-close-button', resources.IDR_CLOSE_DIALOG);
    insertRule('.titlebar-close-button:hover', resources.IDR_CLOSE_DIALOG_H);
    insertRule('.titlebar-close-button:active', resources.IDR_CLOSE_DIALOG_P);

    document.title = resources.IDS_EXTENSION_PERMISSIONS_PROMPT_TITLE;
  });
});

