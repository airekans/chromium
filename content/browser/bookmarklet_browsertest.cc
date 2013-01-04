// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/string_util.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/shell.h"
#include "content/test/content_browser_test.h"
#include "content/test/content_browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

class BookmarkletTest : public ContentBrowserTest {
 public:
  void NavigateToStartPage() {
    NavigateToURL(shell(), GURL("data:text/html,start page"));
    EXPECT_EQ("start page", GetBodyText());
  }

  std::string GetBodyText() {
    std::string body_text;
    EXPECT_TRUE(ExecuteScriptAndExtractString(
        shell()->web_contents(),
        "window.domAutomationController.send(document.body.innerText);",
        &body_text));
    return body_text;
  }
};

IN_PROC_BROWSER_TEST_F(BookmarkletTest, Redirect) {
  NavigateToStartPage();

  NavigateToURL(shell(), GURL(
      "javascript:location.href='data:text/plain,SUCCESS'"));
  EXPECT_EQ("SUCCESS", GetBodyText());
}

IN_PROC_BROWSER_TEST_F(BookmarkletTest, RedirectVoided) {
  NavigateToStartPage();

  // This test should be redundant with the Redirect test above.  The point
  // here is to emphasize that in either case the assignment to location during
  // the evaluation of the script should suppress loading the script result.
  // Here, because of the void() wrapping there is no script result.
  NavigateToURL(shell(), GURL(
      "javascript:void(location.href='data:text/plain,SUCCESS')"));
  EXPECT_EQ("SUCCESS", GetBodyText());
}

IN_PROC_BROWSER_TEST_F(BookmarkletTest, NonEmptyResult) {
  NavigateToStartPage();
  // If there's no navigation, javascript: URLs are run synchronously.
  shell()->LoadURL(GURL("javascript:'hello world'"));

  EXPECT_EQ("hello world", GetBodyText());
}

IN_PROC_BROWSER_TEST_F(BookmarkletTest, DocumentWrite) {
  NavigateToStartPage();

  // If there's no navigation, javascript: URLs are run synchronously.
  shell()->LoadURL(GURL(
      "javascript:document.open();"
      "document.write('hello world');"
      "document.close();"));
  EXPECT_EQ("hello world", GetBodyText());
}


}  // namespace content

