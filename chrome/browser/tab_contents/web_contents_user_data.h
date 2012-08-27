// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_WEB_CONTENTS_USER_DATA_H_
#define CHROME_BROWSER_TAB_CONTENTS_WEB_CONTENTS_USER_DATA_H_

#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

// A base class for classes attached to, and scoped to, the lifetime of a
// WebContents. For example:
//
// class FooTabHelper : public WebContentsUserData<FooTabHelper> {
//  public:
//   explicit FooTabHelper(content::WebContents* contents);
//   virtual ~FooTabHelper();
//
template <typename T>
class WebContentsUserData : public base::SupportsUserData::Data {
 public:
  // Creates an object of type T, and attaches it to the specified WebContents.
  static void CreateForWebContents(content::WebContents* contents) {
    void* key = reinterpret_cast<void*>(&CreateForWebContents);
    contents->SetUserData(key, new T(contents));
  }

  // Retrieves the instance of type T that was attached to the specified
  // WebContents (via CreateForWebContents above) and returns it. If no instance
  // of the type was attached, returns NULL.
  static T* FromWebContents(content::WebContents* contents) {
    void* key = reinterpret_cast<void*>(&CreateForWebContents);
    return static_cast<T*>(contents->GetUserData(key));
  }
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_WEB_CONTENTS_USER_DATA_H_
