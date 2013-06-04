// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_page.h"

#include "base/memory/scoped_ptr.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_test_sink.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class FakePageDelegate : public InstantPage::Delegate {
 public:
  virtual ~FakePageDelegate() {
  }

  MOCK_METHOD1(InstantPageRenderViewCreated,
               void(const content::WebContents* contents));
  MOCK_METHOD2(InstantSupportDetermined,
               void(const content::WebContents* contents,
                    bool supports_instant));
  MOCK_METHOD1(InstantPageRenderViewGone,
               void(const content::WebContents* contents));
  MOCK_METHOD2(InstantPageAboutToNavigateMainFrame,
               void(const content::WebContents* contents,
                    const GURL& url));
  MOCK_METHOD2(SetSuggestions,
               void(const content::WebContents* contents,
                    const std::vector<InstantSuggestion>& suggestions));
  MOCK_METHOD3(ShowInstantOverlay,
               void(const content::WebContents* contents,
                    int height,
                    InstantSizeUnits units));
  MOCK_METHOD0(LogDropdownShown, void());
  MOCK_METHOD2(FocusOmnibox,
               void(const content::WebContents* contents,
                    OmniboxFocusState state));
  MOCK_METHOD5(NavigateToURL,
               void(const content::WebContents* contents,
                    const GURL& url,
                    content::PageTransition transition,
                    WindowOpenDisposition disposition,
                    bool is_search_type));
  MOCK_METHOD1(DeleteMostVisitedItem, void(const GURL& url));
  MOCK_METHOD1(UndoMostVisitedDeletion, void(const GURL& url));
  MOCK_METHOD0(UndoAllMostVisitedDeletions, void());
  MOCK_METHOD1(InstantPageLoadFailed, void(content::WebContents* contents));
};

class FakePage : public InstantPage {
 public:
  FakePage(Delegate* delegate, const std::string& instant_url)
    : InstantPage(delegate, instant_url) {
  }
  virtual ~FakePage() {
  }

  using InstantPage::SetContents;
};

}  // namespace

class InstantPageTest : public ChromeRenderViewHostTestHarness {
 public:
  scoped_ptr<FakePage> page;
  FakePageDelegate delegate;
};

TEST_F(InstantPageTest, IsLocal) {
  page.reset(new FakePage(&delegate, ""));
  EXPECT_FALSE(page->IsLocal());
  page->SetContents(web_contents());
  NavigateAndCommit(GURL(chrome::kChromeSearchLocalNtpUrl));
  EXPECT_TRUE(page->IsLocal());
  NavigateAndCommit(GURL("http://example.com"));
  EXPECT_FALSE(page->IsLocal());
  NavigateAndCommit(GURL(chrome::kChromeSearchLocalGoogleNtpUrl));
  EXPECT_TRUE(page->IsLocal());
}

TEST_F(InstantPageTest, DetermineIfPageSupportsInstant_Local) {
  page.reset(new FakePage(&delegate, ""));
  EXPECT_FALSE(page->supports_instant());
  page->SetContents(web_contents());
  NavigateAndCommit(GURL(chrome::kChromeSearchLocalNtpUrl));
  EXPECT_TRUE(page->IsLocal());
  EXPECT_CALL(delegate, InstantSupportDetermined(web_contents(), true))
      .Times(1);
  page->DetermineIfPageSupportsInstant();
  EXPECT_TRUE(page->supports_instant());
}

TEST_F(InstantPageTest, DetermineIfPageSupportsInstant_NonLocal) {
  page.reset(new FakePage(&delegate, ""));
  EXPECT_FALSE(page->supports_instant());
  page->SetContents(web_contents());
  NavigateAndCommit(GURL("http://example.com/"));
  EXPECT_FALSE(page->IsLocal());
  process()->sink().ClearMessages();
  page->DetermineIfPageSupportsInstant();
  const IPC::Message* message = process()->sink().GetFirstMessageMatching(
      ChromeViewMsg_DetermineIfPageSupportsInstant::ID);
  ASSERT_TRUE(message != NULL);
  EXPECT_EQ(web_contents()->GetRoutingID(), message->routing_id());
}

TEST_F(InstantPageTest, DispatchRequestToDeleteMostVisitedItem) {
  page.reset(new FakePage(&delegate, ""));
  page->SetContents(web_contents());
  GURL item_url("www.foo.com");
  EXPECT_CALL(delegate, DeleteMostVisitedItem(item_url)).Times(1);
  EXPECT_TRUE(page->OnMessageReceived(
      ChromeViewHostMsg_SearchBoxDeleteMostVisitedItem(rvh()->GetRoutingID(),
                                                       item_url)));
}

TEST_F(InstantPageTest, DispatchRequestToUndoMostVisitedDeletion) {
  page.reset(new FakePage(&delegate, ""));
  page->SetContents(web_contents());
  GURL item_url("www.foo.com");
  EXPECT_CALL(delegate, UndoMostVisitedDeletion(item_url)).Times(1);
  EXPECT_TRUE(page->OnMessageReceived(
      ChromeViewHostMsg_SearchBoxUndoMostVisitedDeletion(rvh()->GetRoutingID(),
                                                         item_url)));
}

TEST_F(InstantPageTest, DispatchRequestToUndoAllMostVisitedDeletions) {
  page.reset(new FakePage(&delegate, ""));
  page->SetContents(web_contents());
  EXPECT_CALL(delegate, UndoAllMostVisitedDeletions()).Times(1);
  EXPECT_TRUE(page->OnMessageReceived(
      ChromeViewHostMsg_SearchBoxUndoAllMostVisitedDeletions(
          rvh()->GetRoutingID())));
}
