// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {

  var OptionsPage = options.OptionsPage;

  /**
   * ClearBrowserData class
   * Encapsulated handling of the 'Clear Browser Data' overlay page.
   * @class
   */
  function ClearBrowserDataPage() {
    OptionsPage.call(this, 'clearBrowserDataPage',
                     templateData.clearBrowserDataTitle,
                     'clearBrowserDataPage');
  }

  ClearBrowserDataPage.throbIntervalId = 0;

  cr.addSingletonGetter(ClearBrowserDataPage);

  ClearBrowserDataPage.prototype = {
    // Inherit ClearBrowserDataPage from OptionsPage.
    __proto__: OptionsPage.prototype,

    /**
     * Initialize the page.
     */
    initializePage: function() {
      // Call base class implementation to starts preference initialization.
      OptionsPage.prototype.initializePage.call(this);

      var f = this.updateCommitButtonState_.bind(this);
      var types = ['browser.clear_data.browsing_history',
                   'browser.clear_data.download_history',
                   'browser.clear_data.cache',
                   'browser.clear_data.cookies',
                   'browser.clear_data.passwords',
                   'browser.clear_data.form_data'];
      types.forEach(function(type) {
          Preferences.getInstance().addEventListener(type, f);
      });

      var checkboxes = document.querySelectorAll(
          '#checkboxListData input[type=checkbox]');
      for (var i = 0; i < checkboxes.length; i++) {
        checkboxes[i].onclick = f;
      }
      this.updateCommitButtonState_();

      // Setup click handler for the clear(Ok) button.
      $('clearBrowsingDataCommit').onclick = function(event) {
        chrome.send('performClearBrowserData');
      };
    },

    // Set the enabled state of the commit button.
    updateCommitButtonState_: function() {
      var checkboxes = document.querySelectorAll(
          '#checkboxListData input[type=checkbox]');
      var isChecked = false;
      for (var i = 0; i < checkboxes.length; i++) {
        if (checkboxes[i].checked) {
          isChecked = true;
          break;
        }
      }
      $('clearBrowsingDataCommit').disabled = !isChecked;
    },
  };

  //
  // Chrome callbacks
  //
  ClearBrowserDataPage.setClearingState = function(state) {
    $('deleteBrowsingHistoryCheckbox').disabled = state;
    $('deleteDownloadHistoryCheckbox').disabled = state;
    $('deleteCacheCheckbox').disabled = state;
    $('deleteCookiesCheckbox').disabled = state;
    $('deletePasswordsCheckbox').disabled = state;
    $('deleteFormDataCheckbox').disabled = state;
    $('clearBrowsingDataTimePeriod').disabled = state;
    $('cbdThrobber').style.visibility = state ? 'visible' : 'hidden';

    if (state)
      $('clearBrowsingDataCommit').disabled = true;
    else
      ClearBrowserDataPage.getInstance().updateCommitButtonState_();

    function advanceThrobber() {
      var throbber = $('cbdThrobber');
      // TODO(csilv): make this smoother using time-based animation?
      throbber.style.backgroundPositionX =
          ((parseInt(getComputedStyle(throbber).backgroundPositionX, 10) - 16) %
          576) + 'px';
    }
    if (state) {
      ClearBrowserDataPage.throbIntervalId =
          setInterval(advanceThrobber, 30);
    } else {
      clearInterval(ClearBrowserDataPage.throbIntervalId);
    }
  }

  ClearBrowserDataPage.setClearLocalDataLabel = function(label) {
    $('deleteCookiesLabel').innerText = label;
  };

  ClearBrowserDataPage.dismiss = function() {
    OptionsPage.clearOverlays();
    this.setClearingState(false);
  }

  // Export
  return {
    ClearBrowserDataPage: ClearBrowserDataPage
  };

});

