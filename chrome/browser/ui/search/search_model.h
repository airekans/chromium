// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_SEARCH_MODEL_H_
#define CHROME_BROWSER_UI_SEARCH_SEARCH_MODEL_H_

#include "base/basictypes.h"
#include "base/observer_list.h"
#include "chrome/common/search_types.h"

namespace content {
class WebContents;
}

namespace chrome {
namespace search {

class SearchModelObserver;

// An observable model for UI components that care about search model state
// changes.
class SearchModel {
 public:
  explicit SearchModel(content::WebContents* web_contents);
  ~SearchModel();

  // Change the mode.  Change notifications are sent to observers.  An animated
  // transition may be requested.
  void SetMode(const Mode& mode);

  // Get the active mode.
  const Mode& mode() const { return mode_; }

  // Add and remove observers.
  void AddObserver(SearchModelObserver* observer);
  void RemoveObserver(SearchModelObserver* observer);

  // This can be NULL if this is the browser model and it's accessed during
  // startup or shutdown.
  const content::WebContents* web_contents() const {
    return web_contents_;
  }

  void set_web_contents(content::WebContents* web_contents) {
    web_contents_ = web_contents;
  }

 private:
  // The display mode of UI elements such as the toolbar, the tab strip, etc.
  Mode mode_;

  // Weak. Used to access current profile to determine incognito status.
  content::WebContents* web_contents_;

  // Observers.
  ObserverList<SearchModelObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(SearchModel);
};

}  // namespace search
}  // namespace chrome

#endif  // CHROME_BROWSER_UI_SEARCH_SEARCH_MODEL_H_
