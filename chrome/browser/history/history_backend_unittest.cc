// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/favicon/imported_favicon_usage.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/in_memory_database.h"
#include "chrome/browser/history/in_memory_history_backend.h"
#include "chrome/browser/history/visit_filter.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/thumbnail_score.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/tools/profiles/thumbnail-inl.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/test/test_browser_thread.h"
#include "googleurl/src/gurl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/image/image.h"

using base::Time;
using base::TimeDelta;

// This file only tests functionality where it is most convenient to call the
// backend directly. Most of the history backend functions are tested by the
// history unit test. Because of the elaborate callbacks involved, this is no
// harder than calling it directly for many things.

namespace {

// data we'll put into the thumbnail database
static const unsigned char blob1[] =
    "12346102356120394751634516591348710478123649165419234519234512349134";

static const gfx::Size kTinySize = gfx::Size(10, 10);
static const gfx::Size kSmallSize = gfx::Size(16, 16);
static const gfx::Size kLargeSize = gfx::Size(32, 32);

// Comparison functions as to make it easier to check results of
// GetFaviconBitmaps() and GetIconMappingsForPageURL().
bool IconMappingLessThan(const history::IconMapping& a,
                         const history::IconMapping& b) {
  return a.icon_url < b.icon_url;
}

bool FaviconBitmapLessThan(const history::FaviconBitmap& a,
                           const history::FaviconBitmap& b) {
  return a.pixel_size.GetArea() < b.pixel_size.GetArea();
}

}  // namepace

namespace history {

class HistoryBackendTest;

// This must be a separate object since HistoryBackend manages its lifetime.
// This just forwards the messages we're interested in to the test object.
class HistoryBackendTestDelegate : public HistoryBackend::Delegate {
 public:
  explicit HistoryBackendTestDelegate(HistoryBackendTest* test) : test_(test) {}

  virtual void NotifyProfileError(int backend_id,
                                  sql::InitStatus init_status) OVERRIDE {}
  virtual void SetInMemoryBackend(int backend_id,
                                  InMemoryHistoryBackend* backend) OVERRIDE;
  virtual void BroadcastNotifications(int type,
                                      HistoryDetails* details) OVERRIDE;
  virtual void DBLoaded(int backend_id) OVERRIDE;
  virtual void StartTopSitesMigration(int backend_id) OVERRIDE;
  virtual void NotifyVisitDBObserversOnAddVisit(
      const BriefVisitInfo& info) OVERRIDE {}

 private:
  // Not owned by us.
  HistoryBackendTest* test_;

  DISALLOW_COPY_AND_ASSIGN(HistoryBackendTestDelegate);
};

class HistoryBackendCancelableRequest
    : public CancelableRequestProvider,
      public CancelableRequestConsumerTSimple<int> {
 public:
  HistoryBackendCancelableRequest() {}

  template<class RequestType>
  CancelableRequestProvider::Handle MockScheduleOfRequest(
      RequestType* request) {
    AddRequest(request, this);
    return request->handle();
  }
};

class HistoryBackendTest : public testing::Test {
 public:
  HistoryBackendTest()
      : bookmark_model_(NULL),
        loaded_(false),
        num_broadcasted_notifications_(0),
        ui_thread_(content::BrowserThread::UI, &message_loop_) {
  }

  virtual ~HistoryBackendTest() {
  }

  // Callback for QueryMostVisited.
  void OnQueryMostVisited(CancelableRequestProvider::Handle handle,
                          history::MostVisitedURLList data) {
    most_visited_list_.swap(data);
  }

  // Callback for QueryFiltered.
  void OnQueryFiltered(CancelableRequestProvider::Handle handle,
                       const history::FilteredURLList& data) {
    filtered_list_ = data;
  }

  const history::MostVisitedURLList& get_most_visited_list() const {
    return most_visited_list_;
  }

  const history::FilteredURLList& get_filtered_list() const {
    return filtered_list_;
  }

  int num_broadcasted_notifications() const {
    return num_broadcasted_notifications_;
  }

 protected:
  scoped_refptr<HistoryBackend> backend_;  // Will be NULL on init failure.
  scoped_ptr<InMemoryHistoryBackend> mem_backend_;

  void AddRedirectChain(const char* sequence[], int page_id) {
    AddRedirectChainWithTransitionAndTime(sequence, page_id,
                                          content::PAGE_TRANSITION_LINK,
                                          Time::Now());
  }

  void AddRedirectChainWithTransitionAndTime(
      const char* sequence[],
      int page_id,
      content::PageTransition transition,
      base::Time time) {
    history::RedirectList redirects;
    for (int i = 0; sequence[i] != NULL; ++i)
      redirects.push_back(GURL(sequence[i]));

    int int_scope = 1;
    void* scope = 0;
    memcpy(&scope, &int_scope, sizeof(int_scope));
    history::HistoryAddPageArgs request(
        redirects.back(), time, scope, page_id, GURL(),
        redirects, transition, history::SOURCE_BROWSED,
        true);
    backend_->AddPage(request);
  }

  // Adds CLIENT_REDIRECT page transition.
  // |url1| is the source URL and |url2| is the destination.
  // |did_replace| is true if the transition is non-user initiated and the
  // navigation entry for |url2| has replaced that for |url1|. The possibly
  // updated transition code of the visit records for |url1| and |url2| is
  // returned by filling in |*transition1| and |*transition2|, respectively.
  // |time| is a time of the redirect.
  void  AddClientRedirect(const GURL& url1, const GURL& url2, bool did_replace,
                          base::Time time,
                          int* transition1, int* transition2) {
    void* const dummy_scope = reinterpret_cast<void*>(0x87654321);
    history::RedirectList redirects;
    if (url1.is_valid())
      redirects.push_back(url1);
    if (url2.is_valid())
      redirects.push_back(url2);
    HistoryAddPageArgs request(
        url2, time, dummy_scope, 0, url1,
        redirects, content::PAGE_TRANSITION_CLIENT_REDIRECT,
        history::SOURCE_BROWSED, did_replace);
    backend_->AddPage(request);

    *transition1 = getTransition(url1);
    *transition2 = getTransition(url2);
  }

  int getTransition(const GURL& url) {
    if (!url.is_valid())
      return 0;
    URLRow row;
    URLID id = backend_->db()->GetRowForURL(url, &row);
    VisitVector visits;
    EXPECT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
    return visits[0].transition;
  }

  base::FilePath getTestDir() {
    return test_dir_;
  }

  // Returns a gfx::Size vector with small size.
  const std::vector<gfx::Size> GetSizesSmall() {
    std::vector<gfx::Size> sizes_small;
    sizes_small.push_back(kSmallSize);
    return sizes_small;
  }

  // Returns a gfx::Size vector with large size.
  const std::vector<gfx::Size> GetSizesLarge() {
    std::vector<gfx::Size> sizes_large;
    sizes_large.push_back(kLargeSize);
    return sizes_large;
  }

  // Returns a gfx::Size vector with small and large sizes.
  const std::vector<gfx::Size> GetSizesSmallAndLarge() {
    std::vector<gfx::Size> sizes_small_and_large;
    sizes_small_and_large.push_back(kSmallSize);
    sizes_small_and_large.push_back(kLargeSize);
    return sizes_small_and_large;
  }

  // Returns a gfx::Size vector with tiny, small and large sizes.
  const std::vector<gfx::Size> GetSizesTinySmallAndLarge() {
    std::vector<gfx::Size> sizes_tiny_small_and_large;
    sizes_tiny_small_and_large.push_back(kTinySize);
    sizes_tiny_small_and_large.push_back(kSmallSize);
    sizes_tiny_small_and_large.push_back(kLargeSize);
    return sizes_tiny_small_and_large;
  }

  // Returns 1x and 2x scale factors.
  const std::vector<ui::ScaleFactor> GetScaleFactors1x2x() {
    std::vector<ui::ScaleFactor> scale_factors_1x_2x;
    scale_factors_1x_2x.push_back(ui::SCALE_FACTOR_100P);
    scale_factors_1x_2x.push_back(ui::SCALE_FACTOR_200P);
    return scale_factors_1x_2x;
  }

  // Returns the number of icon mappings of |icon_type| to |page_url|.
  size_t NumIconMappingsForPageURL(const GURL& page_url,
                                   chrome::IconType icon_type) {
    std::vector<IconMapping> icon_mappings;
    backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url, icon_type,
                                                       &icon_mappings);
    return icon_mappings.size();
  }

  // Returns the icon mappings for |page_url| sorted alphabetically by icon
  // URL in ascending order. Returns true if there is at least one icon
  // mapping.
  bool GetSortedIconMappingsForPageURL(
      const GURL& page_url,
      std::vector<IconMapping>* icon_mappings) {
    if (!backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
        icon_mappings)) {
      return false;
    }
    std::sort(icon_mappings->begin(), icon_mappings->end(),
              IconMappingLessThan);
    return true;
  }

  // Returns the favicon bitmaps for |icon_id| sorted by pixel size in
  // ascending order. Returns true if there is at least one favicon bitmap.
  bool GetSortedFaviconBitmaps(chrome::FaviconID icon_id,
                               std::vector<FaviconBitmap>* favicon_bitmaps) {
    if (!backend_->thumbnail_db_->GetFaviconBitmaps(icon_id, favicon_bitmaps))
      return false;
    std::sort(favicon_bitmaps->begin(), favicon_bitmaps->end(),
              FaviconBitmapLessThan);
    return true;
  }

  // Returns true if there is exactly one favicon bitmap associated to
  // |favicon_id|. If true, returns favicon bitmap in output parameter.
  bool GetOnlyFaviconBitmap(const chrome::FaviconID icon_id,
                            FaviconBitmap* favicon_bitmap) {
    std::vector<FaviconBitmap> favicon_bitmaps;
    if (!backend_->thumbnail_db_->GetFaviconBitmaps(icon_id, &favicon_bitmaps))
      return false;
    if (favicon_bitmaps.size() != 1)
      return false;
    *favicon_bitmap = favicon_bitmaps[0];
    return true;
  }

  // Generates |favicon_bitmap_data| with entries for the icon_urls and sizes
  // specified. The bitmap_data for entries are lowercase letters of the
  // alphabet starting at 'a' for the entry at index 0.
  void GenerateFaviconBitmapData(
      const GURL& icon_url1,
      const std::vector<gfx::Size>& icon_url1_sizes,
      std::vector<chrome::FaviconBitmapData>* favicon_bitmap_data) {
    GenerateFaviconBitmapData(icon_url1, icon_url1_sizes, GURL(),
                              std::vector<gfx::Size>(), favicon_bitmap_data);
  }

  void GenerateFaviconBitmapData(
      const GURL& icon_url1,
      const std::vector<gfx::Size>& icon_url1_sizes,
      const GURL& icon_url2,
      const std::vector<gfx::Size>& icon_url2_sizes,
      std::vector<chrome::FaviconBitmapData>* favicon_bitmap_data) {
    favicon_bitmap_data->clear();

    char bitmap_char = 'a';
    for (size_t i = 0; i < icon_url1_sizes.size(); ++i) {
      std::vector<unsigned char> data;
      data.push_back(bitmap_char);
      chrome::FaviconBitmapData bitmap_data_element;
      bitmap_data_element.bitmap_data =
          base::RefCountedBytes::TakeVector(&data);
      bitmap_data_element.pixel_size = icon_url1_sizes[i];
      bitmap_data_element.icon_url = icon_url1;
      favicon_bitmap_data->push_back(bitmap_data_element);

      ++bitmap_char;
    }

    for (size_t i = 0; i < icon_url2_sizes.size(); ++i) {
      std::vector<unsigned char> data;
      data.push_back(bitmap_char);
      chrome::FaviconBitmapData bitmap_data_element;
      bitmap_data_element.bitmap_data =
          base::RefCountedBytes::TakeVector(&data);
      bitmap_data_element.pixel_size = icon_url2_sizes[i];
      bitmap_data_element.icon_url = icon_url2;
      favicon_bitmap_data->push_back(bitmap_data_element);

      ++bitmap_char;
    }
  }

  // Returns true if |bitmap_data| is equal to |expected_data|.
  bool BitmapDataEqual(char expected_data,
                       scoped_refptr<base::RefCountedMemory> bitmap_data) {
    return bitmap_data.get() &&
           bitmap_data->size() == 1u &&
           *bitmap_data->front() == expected_data;
  }

  BookmarkModel bookmark_model_;

 protected:
  // testing::Test
  virtual void SetUp() {
    if (!file_util::CreateNewTempDirectory(FILE_PATH_LITERAL("BackendTest"),
                                           &test_dir_))
      return;
    backend_ = new HistoryBackend(test_dir_,
                                  0,
                                  new HistoryBackendTestDelegate(this),
                                  &bookmark_model_);
    backend_->Init(std::string(), false);
  }

  bool loaded_;

 private:
  friend class HistoryBackendTestDelegate;

  virtual void TearDown() {
    if (backend_)
      backend_->Closing();
    backend_ = NULL;
    mem_backend_.reset();
    file_util::Delete(test_dir_, true);
  }

  void SetInMemoryBackend(int backend_id, InMemoryHistoryBackend* backend) {
    mem_backend_.reset(backend);
  }

  void BroadcastNotifications(int type,
                              HistoryDetails* details) {
    ++num_broadcasted_notifications_;

    // Send the notifications directly to the in-memory database.
    content::Details<HistoryDetails> det(details);
    mem_backend_->Observe(type, content::Source<HistoryBackendTest>(NULL), det);

    // The backend passes ownership of the details pointer to us.
    delete details;
  }

  // The number of notifications which were broadcasted.
  int num_broadcasted_notifications_;

  MessageLoop message_loop_;
  base::FilePath test_dir_;
  history::MostVisitedURLList most_visited_list_;
  history::FilteredURLList filtered_list_;
  content::TestBrowserThread ui_thread_;
};

void HistoryBackendTestDelegate::SetInMemoryBackend(int backend_id,
    InMemoryHistoryBackend* backend) {
  test_->SetInMemoryBackend(backend_id, backend);
}

void HistoryBackendTestDelegate::BroadcastNotifications(
    int type,
    HistoryDetails* details) {
  test_->BroadcastNotifications(type, details);
}

void HistoryBackendTestDelegate::DBLoaded(int backend_id) {
  test_->loaded_ = true;
}

void HistoryBackendTestDelegate::StartTopSitesMigration(int backend_id) {
  test_->backend_->MigrateThumbnailsDatabase();
}

// http://crbug.com/114287
#if defined(OS_WIN)
#define MAYBE_Loaded DISABLED_Loaded
#else
#define MAYBE_Loaded Loaded
#endif // defined(OS_WIN)
TEST_F(HistoryBackendTest, MAYBE_Loaded) {
  ASSERT_TRUE(backend_.get());
  ASSERT_TRUE(loaded_);
}

TEST_F(HistoryBackendTest, DeleteAll) {
  ASSERT_TRUE(backend_.get());

  // Add two favicons, each with two bitmaps. Note that we add favicon2 before
  // adding favicon1. This is so that favicon1 one gets ID 2 autoassigned to
  // the database, which will change when the other one is deleted. This way
  // we can test that updating works properly.
  GURL favicon_url1("http://www.google.com/favicon.ico");
  GURL favicon_url2("http://news.google.com/favicon.ico");
  chrome::FaviconID favicon2 = backend_->thumbnail_db_->AddFavicon(favicon_url2,
      chrome::FAVICON, GetSizesSmallAndLarge());
  chrome::FaviconID favicon1 = backend_->thumbnail_db_->AddFavicon(favicon_url1,
      chrome::FAVICON, GetSizesSmallAndLarge());

  std::vector<unsigned char> data;
  data.push_back('a');
  EXPECT_TRUE(backend_->thumbnail_db_->AddFaviconBitmap(favicon1,
      new base::RefCountedBytes(data), Time::Now(), kSmallSize));
  data[0] = 'b';
  EXPECT_TRUE(backend_->thumbnail_db_->AddFaviconBitmap(favicon1,
     new base::RefCountedBytes(data), Time::Now(), kLargeSize));

  data[0] = 'c';
  EXPECT_TRUE(backend_->thumbnail_db_->AddFaviconBitmap(favicon2,
      new base::RefCountedBytes(data), Time::Now(), kSmallSize));
  data[0] = 'd';
  EXPECT_TRUE(backend_->thumbnail_db_->AddFaviconBitmap(favicon2,
     new base::RefCountedBytes(data), Time::Now(), kLargeSize));

  // First visit two URLs.
  URLRow row1(GURL("http://www.google.com/"));
  row1.set_visit_count(2);
  row1.set_typed_count(1);
  row1.set_last_visit(Time::Now());
  backend_->thumbnail_db_->AddIconMapping(row1.url(), favicon1);

  URLRow row2(GURL("http://news.google.com/"));
  row2.set_visit_count(1);
  row2.set_last_visit(Time::Now());
  backend_->thumbnail_db_->AddIconMapping(row2.url(), favicon2);

  URLRows rows;
  rows.push_back(row2);  // Reversed order for the same reason as favicons.
  rows.push_back(row1);
  backend_->AddPagesWithDetails(rows, history::SOURCE_BROWSED);

  URLID row1_id = backend_->db_->GetRowForURL(row1.url(), NULL);
  URLID row2_id = backend_->db_->GetRowForURL(row2.url(), NULL);

  // Get the two visits for the URLs we just added.
  VisitVector visits;
  backend_->db_->GetVisitsForURL(row1_id, &visits);
  ASSERT_EQ(1U, visits.size());
  VisitID visit1_id = visits[0].visit_id;

  visits.clear();
  backend_->db_->GetVisitsForURL(row2_id, &visits);
  ASSERT_EQ(1U, visits.size());
  VisitID visit2_id = visits[0].visit_id;

  // The in-memory backend should have been set and it should have gotten the
  // typed URL.
  ASSERT_TRUE(mem_backend_.get());
  URLRow outrow1;
  EXPECT_TRUE(mem_backend_->db_->GetRowForURL(row1.url(), NULL));

  // Add thumbnails for each page. The |Images| take ownership of SkBitmap
  // created from decoding the images.
  ThumbnailScore score(0.25, true, true);
  scoped_ptr<SkBitmap> google_bitmap(
      gfx::JPEGCodec::Decode(kGoogleThumbnail, sizeof(kGoogleThumbnail)));

  gfx::Image google_image = gfx::Image::CreateFrom1xBitmap(*google_bitmap);

  Time time;
  GURL gurl;
  backend_->thumbnail_db_->SetPageThumbnail(gurl, row1_id, &google_image,
                                            score, time);
  scoped_ptr<SkBitmap> weewar_bitmap(
     gfx::JPEGCodec::Decode(kWeewarThumbnail, sizeof(kWeewarThumbnail)));
  gfx::Image weewar_image = gfx::Image::CreateFrom1xBitmap(*weewar_bitmap);
  backend_->thumbnail_db_->SetPageThumbnail(gurl, row2_id, &weewar_image,
                                            score, time);

  // Star row1.
  bookmark_model_.AddURL(
      bookmark_model_.bookmark_bar_node(), 0, string16(), row1.url());

  // Set full text index for each one.
  backend_->text_database_->AddPageData(row1.url(), row1_id, visit1_id,
                                        row1.last_visit(),
                                        UTF8ToUTF16("Title 1"),
                                        UTF8ToUTF16("Body 1"));
  backend_->text_database_->AddPageData(row2.url(), row2_id, visit2_id,
                                        row2.last_visit(),
                                        UTF8ToUTF16("Title 2"),
                                        UTF8ToUTF16("Body 2"));

  // Now finally clear all history.
  backend_->DeleteAllHistory();

  // The first URL should be preserved but the time should be cleared.
  EXPECT_TRUE(backend_->db_->GetRowForURL(row1.url(), &outrow1));
  EXPECT_EQ(row1.url(), outrow1.url());
  EXPECT_EQ(0, outrow1.visit_count());
  EXPECT_EQ(0, outrow1.typed_count());
  EXPECT_TRUE(Time() == outrow1.last_visit());

  // The second row should be deleted.
  URLRow outrow2;
  EXPECT_FALSE(backend_->db_->GetRowForURL(row2.url(), &outrow2));

  // All visits should be deleted for both URLs.
  VisitVector all_visits;
  backend_->db_->GetAllVisitsInRange(Time(), Time(), 0, &all_visits);
  ASSERT_EQ(0U, all_visits.size());

  // All thumbnails should be deleted.
  std::vector<unsigned char> out_data;
  EXPECT_FALSE(backend_->thumbnail_db_->GetPageThumbnail(outrow1.id(),
                                                         &out_data));
  EXPECT_FALSE(backend_->thumbnail_db_->GetPageThumbnail(row2_id, &out_data));

  // We should have a favicon and favicon bitmaps for the first URL only. We
  // look them up by favicon URL since the IDs may have changed.
  chrome::FaviconID out_favicon1 = backend_->thumbnail_db_->
      GetFaviconIDForFaviconURL(favicon_url1, chrome::FAVICON, NULL);
  EXPECT_TRUE(out_favicon1);

  std::vector<FaviconBitmap> favicon_bitmaps;
  EXPECT_TRUE(backend_->thumbnail_db_->GetFaviconBitmaps(
      out_favicon1, &favicon_bitmaps));
  ASSERT_EQ(2u, favicon_bitmaps.size());

  FaviconBitmap favicon_bitmap1 = favicon_bitmaps[0];
  FaviconBitmap favicon_bitmap2 = favicon_bitmaps[1];

  // Favicon bitmaps do not need to be in particular order.
  if (favicon_bitmap1.pixel_size == kLargeSize) {
    FaviconBitmap tmp_favicon_bitmap = favicon_bitmap1;
    favicon_bitmap1 = favicon_bitmap2;
    favicon_bitmap2 = tmp_favicon_bitmap;
  }

  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap1.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap1.pixel_size);

  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmap2.bitmap_data));
  EXPECT_EQ(kLargeSize, favicon_bitmap2.pixel_size);

  chrome::FaviconID out_favicon2 = backend_->thumbnail_db_->
      GetFaviconIDForFaviconURL(favicon_url2, chrome::FAVICON, NULL);
  EXPECT_FALSE(out_favicon2) << "Favicon not deleted";

  // The remaining URL should still reference the same favicon, even if its
  // ID has changed.
  std::vector<IconMapping> mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      outrow1.url(), chrome::FAVICON, &mappings));
  EXPECT_EQ(1u, mappings.size());
  EXPECT_EQ(out_favicon1, mappings[0].icon_id);

  // The first URL should still be bookmarked.
  EXPECT_TRUE(bookmark_model_.IsBookmarked(row1.url()));

  // The full text database should have no data.
  std::vector<TextDatabase::Match> text_matches;
  Time first_time_searched;
  backend_->text_database_->GetTextMatches(UTF8ToUTF16("Body"),
                                           QueryOptions(),
                                           &text_matches,
                                           &first_time_searched);
  EXPECT_EQ(0U, text_matches.size());
}

// Checks that adding a visit, then calling DeleteAll, and then trying to add
// data for the visited page works.  This can happen when clearing the history
// immediately after visiting a page.
TEST_F(HistoryBackendTest, DeleteAllThenAddData) {
  ASSERT_TRUE(backend_.get());

  Time visit_time = Time::Now();
  GURL url("http://www.google.com/");
  HistoryAddPageArgs request(url, visit_time, NULL, 0, GURL(),
                             history::RedirectList(),
                             content::PAGE_TRANSITION_KEYWORD_GENERATED,
                             history::SOURCE_BROWSED, false);
  backend_->AddPage(request);

  // Check that a row was added.
  URLRow outrow;
  EXPECT_TRUE(backend_->db_->GetRowForURL(url, &outrow));

  // Check that the visit was added.
  VisitVector all_visits;
  backend_->db_->GetAllVisitsInRange(Time(), Time(), 0, &all_visits);
  ASSERT_EQ(1U, all_visits.size());

  // Clear all history.
  backend_->DeleteAllHistory();

  // The row should be deleted.
  EXPECT_FALSE(backend_->db_->GetRowForURL(url, &outrow));

  // The visit should be deleted.
  backend_->db_->GetAllVisitsInRange(Time(), Time(), 0, &all_visits);
  ASSERT_EQ(0U, all_visits.size());

  // Try and set the full text index.
  backend_->SetPageTitle(url, UTF8ToUTF16("Title"));
  backend_->SetPageContents(url, UTF8ToUTF16("Body"));

  // The row should still be deleted.
  EXPECT_FALSE(backend_->db_->GetRowForURL(url, &outrow));

  // The visit should still be deleted.
  backend_->db_->GetAllVisitsInRange(Time(), Time(), 0, &all_visits);
  ASSERT_EQ(0U, all_visits.size());

  // The full text database should have no data.
  std::vector<TextDatabase::Match> text_matches;
  Time first_time_searched;
  backend_->text_database_->GetTextMatches(UTF8ToUTF16("Body"),
                                           QueryOptions(),
                                           &text_matches,
                                           &first_time_searched);
  EXPECT_EQ(0U, text_matches.size());
}

TEST_F(HistoryBackendTest, URLsNoLongerBookmarked) {
  GURL favicon_url1("http://www.google.com/favicon.ico");
  GURL favicon_url2("http://news.google.com/favicon.ico");

  std::vector<unsigned char> data;
  data.push_back('1');
  chrome::FaviconID favicon1 = backend_->thumbnail_db_->AddFavicon(
      favicon_url1,
      chrome::FAVICON,
      GetDefaultFaviconSizes(),
      new base::RefCountedBytes(data),
      Time::Now(),
      gfx::Size());

  data[0] = '2';
  chrome::FaviconID favicon2 = backend_->thumbnail_db_->AddFavicon(
      favicon_url2,
      chrome::FAVICON,
      GetDefaultFaviconSizes(),
      new base::RefCountedBytes(data),
      Time::Now(),
      gfx::Size());

  // First visit two URLs.
  URLRow row1(GURL("http://www.google.com/"));
  row1.set_visit_count(2);
  row1.set_typed_count(1);
  row1.set_last_visit(Time::Now());
  EXPECT_TRUE(backend_->thumbnail_db_->AddIconMapping(row1.url(), favicon1));

  URLRow row2(GURL("http://news.google.com/"));
  row2.set_visit_count(1);
  row2.set_last_visit(Time::Now());
  EXPECT_TRUE(backend_->thumbnail_db_->AddIconMapping(row2.url(), favicon2));

  URLRows rows;
  rows.push_back(row2);  // Reversed order for the same reason as favicons.
  rows.push_back(row1);
  backend_->AddPagesWithDetails(rows, history::SOURCE_BROWSED);

  URLID row1_id = backend_->db_->GetRowForURL(row1.url(), NULL);
  URLID row2_id = backend_->db_->GetRowForURL(row2.url(), NULL);

  // Star the two URLs.
  bookmark_utils::AddIfNotBookmarked(&bookmark_model_, row1.url(), string16());
  bookmark_utils::AddIfNotBookmarked(&bookmark_model_, row2.url(), string16());

  // Delete url 2. Because url 2 is starred this won't delete the URL, only
  // the visits.
  backend_->expirer_.DeleteURL(row2.url());

  // Make sure url 2 is still valid, but has no visits.
  URLRow tmp_url_row;
  EXPECT_EQ(row2_id, backend_->db_->GetRowForURL(row2.url(), NULL));
  VisitVector visits;
  backend_->db_->GetVisitsForURL(row2_id, &visits);
  EXPECT_EQ(0U, visits.size());
  // The favicon should still be valid.
  EXPECT_EQ(favicon2,
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(favicon_url2,
                                                         chrome::FAVICON,
                                                         NULL));

  // Unstar row2.
  bookmark_utils::RemoveAllBookmarks(&bookmark_model_, row2.url());

  // Tell the backend it was unstarred. We have to explicitly do this as
  // BookmarkModel isn't wired up to the backend during testing.
  std::set<GURL> unstarred_urls;
  unstarred_urls.insert(row2.url());
  backend_->URLsNoLongerBookmarked(unstarred_urls);

  // The URL should no longer exist.
  EXPECT_FALSE(backend_->db_->GetRowForURL(row2.url(), &tmp_url_row));
  // And the favicon should be deleted.
  EXPECT_EQ(0,
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(favicon_url2,
                                                         chrome::FAVICON,
                                                         NULL));

  // Unstar row 1.
  bookmark_utils::RemoveAllBookmarks(&bookmark_model_, row1.url());
  // Tell the backend it was unstarred. We have to explicitly do this as
  // BookmarkModel isn't wired up to the backend during testing.
  unstarred_urls.clear();
  unstarred_urls.insert(row1.url());
  backend_->URLsNoLongerBookmarked(unstarred_urls);

  // The URL should still exist (because there were visits).
  EXPECT_EQ(row1_id, backend_->db_->GetRowForURL(row1.url(), NULL));

  // There should still be visits.
  visits.clear();
  backend_->db_->GetVisitsForURL(row1_id, &visits);
  EXPECT_EQ(1U, visits.size());

  // The favicon should still be valid.
  EXPECT_EQ(favicon1,
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(favicon_url1,
                                                         chrome::FAVICON,
                                                         NULL));
}

// Tests a handful of assertions for a navigation with a type of
// KEYWORD_GENERATED.
TEST_F(HistoryBackendTest, KeywordGenerated) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://google.com");

  Time visit_time = Time::Now() - base::TimeDelta::FromDays(1);
  HistoryAddPageArgs request(url, visit_time, NULL, 0, GURL(),
                             history::RedirectList(),
                             content::PAGE_TRANSITION_KEYWORD_GENERATED,
                             history::SOURCE_BROWSED, false);
  backend_->AddPage(request);

  // A row should have been added for the url.
  URLRow row;
  URLID url_id = backend_->db()->GetRowForURL(url, &row);
  ASSERT_NE(0, url_id);

  // The typed count should be 1.
  ASSERT_EQ(1, row.typed_count());

  // KEYWORD_GENERATED urls should not be added to the segment db.
  std::string segment_name = VisitSegmentDatabase::ComputeSegmentName(url);
  EXPECT_EQ(0, backend_->db()->GetSegmentNamed(segment_name));

  // One visit should be added.
  VisitVector visits;
  EXPECT_TRUE(backend_->db()->GetVisitsForURL(url_id, &visits));
  EXPECT_EQ(1U, visits.size());

  // But no visible visits.
  visits.clear();
  QueryOptions query_options;
  query_options.max_count = 1;
  backend_->db()->GetVisibleVisitsInRange(query_options, &visits);
  EXPECT_TRUE(visits.empty());

  // Expire the visits.
  std::set<GURL> restrict_urls;
  backend_->expire_backend()->ExpireHistoryBetween(restrict_urls,
                                                   visit_time, Time::Now());

  // The visit should have been nuked.
  visits.clear();
  EXPECT_TRUE(backend_->db()->GetVisitsForURL(url_id, &visits));
  EXPECT_TRUE(visits.empty());

  // As well as the url.
  ASSERT_EQ(0, backend_->db()->GetRowForURL(url, &row));
}

TEST_F(HistoryBackendTest, ClientRedirect) {
  ASSERT_TRUE(backend_.get());

  int transition1;
  int transition2;

  // Initial transition to page A.
  GURL url_a("http://google.com/a");
  AddClientRedirect(GURL(), url_a, false, base::Time(),
                    &transition1, &transition2);
  EXPECT_TRUE(transition2 & content::PAGE_TRANSITION_CHAIN_END);

  // User initiated redirect to page B.
  GURL url_b("http://google.com/b");
  AddClientRedirect(url_a, url_b, false, base::Time(),
                    &transition1, &transition2);
  EXPECT_TRUE(transition1 & content::PAGE_TRANSITION_CHAIN_END);
  EXPECT_TRUE(transition2 & content::PAGE_TRANSITION_CHAIN_END);

  // Non-user initiated redirect to page C.
  GURL url_c("http://google.com/c");
  AddClientRedirect(url_b, url_c, true, base::Time(),
                    &transition1, &transition2);
  EXPECT_FALSE(transition1 & content::PAGE_TRANSITION_CHAIN_END);
  EXPECT_TRUE(transition2 & content::PAGE_TRANSITION_CHAIN_END);
}

TEST_F(HistoryBackendTest, ImportedFaviconsTest) {
  // Setup test data - two Urls in the history, one with favicon assigned and
  // one without.
  GURL favicon_url1("http://www.google.com/favicon.ico");
  std::vector<unsigned char> data;
  data.push_back('1');
  chrome::FaviconID favicon1 = backend_->thumbnail_db_->AddFavicon(
      favicon_url1,
      chrome::FAVICON,
      GetDefaultFaviconSizes(),
      base::RefCountedBytes::TakeVector(&data),
      Time::Now(),
      gfx::Size());
  URLRow row1(GURL("http://www.google.com/"));
  row1.set_visit_count(1);
  row1.set_last_visit(Time::Now());
  EXPECT_TRUE(backend_->thumbnail_db_->AddIconMapping(row1.url(), favicon1));

  URLRow row2(GURL("http://news.google.com/"));
  row2.set_visit_count(1);
  row2.set_last_visit(Time::Now());
  URLRows rows;
  rows.push_back(row1);
  rows.push_back(row2);
  backend_->AddPagesWithDetails(rows, history::SOURCE_BROWSED);
  URLRow url_row1, url_row2;
  EXPECT_FALSE(backend_->db_->GetRowForURL(row1.url(), &url_row1) == 0);
  EXPECT_FALSE(backend_->db_->GetRowForURL(row2.url(), &url_row2) == 0);
  EXPECT_EQ(1u, NumIconMappingsForPageURL(row1.url(), chrome::FAVICON));
  EXPECT_EQ(0u, NumIconMappingsForPageURL(row2.url(), chrome::FAVICON));

  // Now provide one imported favicon for both URLs already in the registry.
  // The new favicon should only be used with the URL that doesn't already have
  // a favicon.
  std::vector<ImportedFaviconUsage> favicons;
  ImportedFaviconUsage favicon;
  favicon.favicon_url = GURL("http://news.google.com/favicon.ico");
  favicon.png_data.push_back('2');
  favicon.urls.insert(row1.url());
  favicon.urls.insert(row2.url());
  favicons.push_back(favicon);
  backend_->SetImportedFavicons(favicons);
  EXPECT_FALSE(backend_->db_->GetRowForURL(row1.url(), &url_row1) == 0);
  EXPECT_FALSE(backend_->db_->GetRowForURL(row2.url(), &url_row2) == 0);

  std::vector<IconMapping> mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      row1.url(), chrome::FAVICON, &mappings));
  EXPECT_EQ(1u, mappings.size());
  EXPECT_EQ(favicon1, mappings[0].icon_id);
  EXPECT_EQ(favicon_url1, mappings[0].icon_url);

  mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
    row2.url(), chrome::FAVICON, &mappings));
  EXPECT_EQ(1u, mappings.size());
  EXPECT_EQ(favicon.favicon_url, mappings[0].icon_url);

  // A URL should not be added to history (to store favicon), if
  // the URL is not bookmarked.
  GURL url3("http://mail.google.com");
  favicons.clear();
  favicon.favicon_url = GURL("http://mail.google.com/favicon.ico");
  favicon.png_data.push_back('3');
  favicon.urls.insert(url3);
  favicons.push_back(favicon);
  backend_->SetImportedFavicons(favicons);
  URLRow url_row3;
  EXPECT_TRUE(backend_->db_->GetRowForURL(url3, &url_row3) == 0);

  // If the URL is bookmarked, it should get added to history with 0 visits.
  bookmark_model_.AddURL(bookmark_model_.bookmark_bar_node(), 0, string16(),
                         url3);
  backend_->SetImportedFavicons(favicons);
  EXPECT_FALSE(backend_->db_->GetRowForURL(url3, &url_row3) == 0);
  EXPECT_TRUE(url_row3.visit_count() == 0);
}

TEST_F(HistoryBackendTest, StripUsernamePasswordTest) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://anyuser:anypass@www.google.com");
  GURL stripped_url("http://www.google.com");

  // Clear all history.
  backend_->DeleteAllHistory();

  // Visit the url with username, password.
  backend_->AddPageVisit(url, base::Time::Now(), 0,
      content::PageTransitionFromInt(
          content::PageTransitionGetQualifier(content::PAGE_TRANSITION_TYPED)),
      history::SOURCE_BROWSED);

  // Fetch the row information about stripped url from history db.
  VisitVector visits;
  URLID row_id = backend_->db_->GetRowForURL(stripped_url, NULL);
  backend_->db_->GetVisitsForURL(row_id, &visits);

  // Check if stripped url is stored in database.
  ASSERT_EQ(1U, visits.size());
}

TEST_F(HistoryBackendTest, AddPageVisitSource) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://www.google.com");

  // Clear all history.
  backend_->DeleteAllHistory();

  // Assume visiting the url from an externsion.
  backend_->AddPageVisit(
      url, base::Time::Now(), 0, content::PAGE_TRANSITION_TYPED,
      history::SOURCE_EXTENSION);
  // Assume the url is imported from Firefox.
  backend_->AddPageVisit(url, base::Time::Now(), 0,
                         content::PAGE_TRANSITION_TYPED,
                         history::SOURCE_FIREFOX_IMPORTED);
  // Assume this url is also synced.
  backend_->AddPageVisit(url, base::Time::Now(), 0,
                         content::PAGE_TRANSITION_TYPED,
                         history::SOURCE_SYNCED);

  // Fetch the row information about the url from history db.
  VisitVector visits;
  URLID row_id = backend_->db_->GetRowForURL(url, NULL);
  backend_->db_->GetVisitsForURL(row_id, &visits);

  // Check if all the visits to the url are stored in database.
  ASSERT_EQ(3U, visits.size());
  VisitSourceMap visit_sources;
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(3U, visit_sources.size());
  int sources = 0;
  for (int i = 0; i < 3; i++) {
    switch (visit_sources[visits[i].visit_id]) {
      case history::SOURCE_EXTENSION:
        sources |= 0x1;
        break;
      case history::SOURCE_FIREFOX_IMPORTED:
        sources |= 0x2;
        break;
      case history::SOURCE_SYNCED:
        sources |= 0x4;
      default:
        break;
    }
  }
  EXPECT_EQ(0x7, sources);
}

TEST_F(HistoryBackendTest, AddPageVisitNotLastVisit) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://www.google.com");

  // Clear all history.
  backend_->DeleteAllHistory();

  // Create visit times
  base::Time recent_time = base::Time::Now();
  base::TimeDelta visit_age = base::TimeDelta::FromDays(3);
  base::Time older_time = recent_time - visit_age;

  // Visit the url with recent time.
  backend_->AddPageVisit(url, recent_time, 0,
      content::PageTransitionFromInt(
          content::PageTransitionGetQualifier(content::PAGE_TRANSITION_TYPED)),
      history::SOURCE_BROWSED);

  // Add to the url a visit with older time (could be syncing from another
  // client, etc.).
  backend_->AddPageVisit(url, older_time, 0,
      content::PageTransitionFromInt(
          content::PageTransitionGetQualifier(content::PAGE_TRANSITION_TYPED)),
      history::SOURCE_SYNCED);

  // Fetch the row information about url from history db.
  VisitVector visits;
  URLRow row;
  URLID row_id = backend_->db_->GetRowForURL(url, &row);
  backend_->db_->GetVisitsForURL(row_id, &visits);

  // Last visit time should be the most recent time, not the most recently added
  // visit.
  ASSERT_EQ(2U, visits.size());
  ASSERT_EQ(recent_time, row.last_visit());
}

TEST_F(HistoryBackendTest, AddPageArgsSource) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://testpageargs.com");

  // Assume this page is browsed by user.
  HistoryAddPageArgs request1(url, base::Time::Now(), NULL, 0, GURL(),
                             history::RedirectList(),
                             content::PAGE_TRANSITION_KEYWORD_GENERATED,
                             history::SOURCE_BROWSED, false);
  backend_->AddPage(request1);
  // Assume this page is synced.
  HistoryAddPageArgs request2(url, base::Time::Now(), NULL, 0, GURL(),
                             history::RedirectList(),
                             content::PAGE_TRANSITION_LINK,
                             history::SOURCE_SYNCED, false);
  backend_->AddPage(request2);
  // Assume this page is browsed again.
  HistoryAddPageArgs request3(url, base::Time::Now(), NULL, 0, GURL(),
                             history::RedirectList(),
                             content::PAGE_TRANSITION_TYPED,
                             history::SOURCE_BROWSED, false);
  backend_->AddPage(request3);

  // Three visits should be added with proper sources.
  VisitVector visits;
  URLRow row;
  URLID id = backend_->db()->GetRowForURL(url, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(3U, visits.size());
  VisitSourceMap visit_sources;
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(1U, visit_sources.size());
  EXPECT_EQ(history::SOURCE_SYNCED, visit_sources.begin()->second);
}

TEST_F(HistoryBackendTest, AddVisitsSource) {
  ASSERT_TRUE(backend_.get());

  GURL url1("http://www.cnn.com");
  std::vector<VisitInfo> visits1, visits2;
  visits1.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(5),
      content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(1),
      content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(
      Time::Now(), content::PAGE_TRANSITION_LINK));

  GURL url2("http://www.example.com");
  visits2.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(10),
      content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(Time::Now(), content::PAGE_TRANSITION_LINK));

  // Clear all history.
  backend_->DeleteAllHistory();

  // Add the visits.
  backend_->AddVisits(url1, visits1, history::SOURCE_IE_IMPORTED);
  backend_->AddVisits(url2, visits2, history::SOURCE_SYNCED);

  // Verify the visits were added with their sources.
  VisitVector visits;
  URLRow row;
  URLID id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(3U, visits.size());
  VisitSourceMap visit_sources;
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(3U, visit_sources.size());
  for (int i = 0; i < 3; i++)
    EXPECT_EQ(history::SOURCE_IE_IMPORTED, visit_sources[visits[i].visit_id]);
  id = backend_->db()->GetRowForURL(url2, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(2U, visits.size());
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(2U, visit_sources.size());
  for (int i = 0; i < 2; i++)
    EXPECT_EQ(history::SOURCE_SYNCED, visit_sources[visits[i].visit_id]);
}

TEST_F(HistoryBackendTest, GetMostRecentVisits) {
  ASSERT_TRUE(backend_.get());

  GURL url1("http://www.cnn.com");
  std::vector<VisitInfo> visits1;
  visits1.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(5),
      content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(1),
      content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(
      Time::Now(), content::PAGE_TRANSITION_LINK));

  // Clear all history.
  backend_->DeleteAllHistory();

  // Add the visits.
  backend_->AddVisits(url1, visits1, history::SOURCE_IE_IMPORTED);

  // Verify the visits were added with their sources.
  VisitVector visits;
  URLRow row;
  URLID id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetMostRecentVisitsForURL(id, 1, &visits));
  ASSERT_EQ(1U, visits.size());
  EXPECT_EQ(visits1[2].first, visits[0].visit_time);
}

TEST_F(HistoryBackendTest, RemoveVisitsTransitions) {
  ASSERT_TRUE(backend_.get());

  // Clear all history.
  backend_->DeleteAllHistory();

  GURL url1("http://www.cnn.com");
  VisitInfo typed_visit(
      Time::Now() - base::TimeDelta::FromDays(6),
      content::PAGE_TRANSITION_TYPED);
  VisitInfo reload_visit(
      Time::Now() - base::TimeDelta::FromDays(5),
      content::PAGE_TRANSITION_RELOAD);
  VisitInfo link_visit(
      Time::Now() - base::TimeDelta::FromDays(4),
      content::PAGE_TRANSITION_LINK);
  std::vector<VisitInfo> visits_to_add;
  visits_to_add.push_back(typed_visit);
  visits_to_add.push_back(reload_visit);
  visits_to_add.push_back(link_visit);

  // Add the visits.
  backend_->AddVisits(url1, visits_to_add, history::SOURCE_SYNCED);

  // Verify that the various counts are what we expect.
  VisitVector visits;
  URLRow row;
  URLID id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(3U, visits.size());
  ASSERT_EQ(1, row.typed_count());
  ASSERT_EQ(2, row.visit_count());

  // Now, delete the typed visit and verify that typed_count is updated.
  ASSERT_TRUE(backend_->RemoveVisits(VisitVector(1, visits[0])));
  id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(2U, visits.size());
  ASSERT_EQ(0, row.typed_count());
  ASSERT_EQ(1, row.visit_count());

  // Delete the reload visit now and verify that none of the counts have
  // changed.
  ASSERT_TRUE(backend_->RemoveVisits(VisitVector(1, visits[0])));
  id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(1U, visits.size());
  ASSERT_EQ(0, row.typed_count());
  ASSERT_EQ(1, row.visit_count());

  // Delete the last visit and verify that we delete the URL.
  ASSERT_TRUE(backend_->RemoveVisits(VisitVector(1, visits[0])));
  ASSERT_EQ(0, backend_->db()->GetRowForURL(url1, &row));
}

TEST_F(HistoryBackendTest, RemoveVisitsSource) {
  ASSERT_TRUE(backend_.get());

  GURL url1("http://www.cnn.com");
  std::vector<VisitInfo> visits1, visits2;
  visits1.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(5),
      content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(Time::Now(),
    content::PAGE_TRANSITION_LINK));

  GURL url2("http://www.example.com");
  visits2.push_back(VisitInfo(
      Time::Now() - base::TimeDelta::FromDays(10),
      content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(Time::Now(), content::PAGE_TRANSITION_LINK));

  // Clear all history.
  backend_->DeleteAllHistory();

  // Add the visits.
  backend_->AddVisits(url1, visits1, history::SOURCE_IE_IMPORTED);
  backend_->AddVisits(url2, visits2, history::SOURCE_SYNCED);

  // Verify the visits of url1 were added.
  VisitVector visits;
  URLRow row;
  URLID id = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(2U, visits.size());
  // Remove these visits.
  ASSERT_TRUE(backend_->RemoveVisits(visits));

  // Now check only url2's source in visit_source table.
  VisitSourceMap visit_sources;
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(0U, visit_sources.size());
  id = backend_->db()->GetRowForURL(url2, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(id, &visits));
  ASSERT_EQ(2U, visits.size());
  ASSERT_TRUE(backend_->GetVisitsSource(visits, &visit_sources));
  ASSERT_EQ(2U, visit_sources.size());
  for (int i = 0; i < 2; i++)
    EXPECT_EQ(history::SOURCE_SYNCED, visit_sources[visits[i].visit_id]);
}

// Test for migration of adding visit_source table.
TEST_F(HistoryBackendTest, MigrationVisitSource) {
  ASSERT_TRUE(backend_.get());
  backend_->Closing();
  backend_ = NULL;

  base::FilePath old_history_path;
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &old_history_path));
  old_history_path = old_history_path.AppendASCII("History");
  old_history_path = old_history_path.AppendASCII("HistoryNoSource");

  // Copy history database file to current directory so that it will be deleted
  // in Teardown.
  base::FilePath new_history_path(getTestDir());
  file_util::Delete(new_history_path, true);
  file_util::CreateDirectory(new_history_path);
  base::FilePath new_history_file =
      new_history_path.Append(chrome::kHistoryFilename);
  ASSERT_TRUE(file_util::CopyFile(old_history_path, new_history_file));

  backend_ = new HistoryBackend(new_history_path,
                                0,
                                new HistoryBackendTestDelegate(this),
                                &bookmark_model_);
  backend_->Init(std::string(), false);
  backend_->Closing();
  backend_ = NULL;

  // Now the database should already be migrated.
  // Check version first.
  int cur_version = HistoryDatabase::GetCurrentVersion();
  sql::Connection db;
  ASSERT_TRUE(db.Open(new_history_file));
  sql::Statement s(db.GetUniqueStatement(
      "SELECT value FROM meta WHERE key = 'version'"));
  ASSERT_TRUE(s.Step());
  int file_version = s.ColumnInt(0);
  EXPECT_EQ(cur_version, file_version);

  // Check visit_source table is created and empty.
  s.Assign(db.GetUniqueStatement(
      "SELECT name FROM sqlite_master WHERE name=\"visit_source\""));
  ASSERT_TRUE(s.Step());
  s.Assign(db.GetUniqueStatement("SELECT * FROM visit_source LIMIT 10"));
  EXPECT_FALSE(s.Step());
}

// Test that SetFaviconMappingsForPageAndRedirects correctly updates icon
// mappings based on redirects, icon URLs and icon types.
TEST_F(HistoryBackendTest, SetFaviconMappingsForPageAndRedirects) {
  // Init recent_redirects_
  const GURL url1("http://www.google.com");
  const GURL url2("http://www.google.com/m");
  URLRow url_info1(url1);
  url_info1.set_visit_count(0);
  url_info1.set_typed_count(0);
  url_info1.set_last_visit(base::Time());
  url_info1.set_hidden(false);
  backend_->db_->AddURL(url_info1);

  URLRow url_info2(url2);
  url_info2.set_visit_count(0);
  url_info2.set_typed_count(0);
  url_info2.set_last_visit(base::Time());
  url_info2.set_hidden(false);
  backend_->db_->AddURL(url_info2);

  history::RedirectList redirects;
  redirects.push_back(url2);
  redirects.push_back(url1);
  backend_->recent_redirects_.Put(url1, redirects);

  const GURL icon_url1("http://www.google.com/icon");
  const GURL icon_url2("http://www.google.com/icon2");

  // Generate bitmap data for a page with two favicons.
  std::vector<chrome::FaviconBitmapData> two_favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url1, GetSizesSmallAndLarge(),
      icon_url2, GetSizesSmallAndLarge(), &two_favicon_bitmap_data);

  // Generate bitmap data for a page with a single favicon.
  std::vector<chrome::FaviconBitmapData> one_favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url1, GetSizesSmallAndLarge(),
                            &one_favicon_bitmap_data);

  // Add two favicons
  backend_->SetFavicons(url1, chrome::FAVICON, two_favicon_bitmap_data);
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url1, chrome::FAVICON));
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url2, chrome::FAVICON));

  // Add one touch_icon
  backend_->SetFavicons(url1, chrome::TOUCH_ICON, one_favicon_bitmap_data);
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url1, chrome::TOUCH_ICON));
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url2, chrome::TOUCH_ICON));
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url1, chrome::FAVICON));

  // Add one TOUCH_PRECOMPOSED_ICON
  backend_->SetFavicons(
      url1, chrome::TOUCH_PRECOMPOSED_ICON, one_favicon_bitmap_data);
  // The touch_icon was replaced.
  EXPECT_EQ(0u, NumIconMappingsForPageURL(url1, chrome::TOUCH_ICON));
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url1, chrome::FAVICON));
  EXPECT_EQ(1u,
            NumIconMappingsForPageURL(url1, chrome::TOUCH_PRECOMPOSED_ICON));
  EXPECT_EQ(1u,
            NumIconMappingsForPageURL(url2, chrome::TOUCH_PRECOMPOSED_ICON));

  // Add a touch_icon.
  backend_->SetFavicons(url1, chrome::TOUCH_ICON, one_favicon_bitmap_data);
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url1, chrome::TOUCH_ICON));
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url1, chrome::FAVICON));
  // The TOUCH_PRECOMPOSED_ICON was replaced.
  EXPECT_EQ(0u,
            NumIconMappingsForPageURL(url1, chrome::TOUCH_PRECOMPOSED_ICON));

  // Add a single favicon.
  backend_->SetFavicons(url1, chrome::FAVICON, one_favicon_bitmap_data);
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url1, chrome::TOUCH_ICON));
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url1, chrome::FAVICON));
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url2, chrome::FAVICON));

  // Add two favicons.
  backend_->SetFavicons(url1, chrome::FAVICON, two_favicon_bitmap_data);
  EXPECT_EQ(1u, NumIconMappingsForPageURL(url1, chrome::TOUCH_ICON));
  EXPECT_EQ(2u, NumIconMappingsForPageURL(url1, chrome::FAVICON));
}

// Test that there is no churn in icon mappings from calling
// SetFavicons() twice with the same |favicon_bitmap_data| parameter.
TEST_F(HistoryBackendTest, SetFaviconMappingsForPageDuplicates) {
  const GURL url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmallAndLarge(),
                            &favicon_bitmap_data);

  backend_->SetFavicons(url, chrome::FAVICON, favicon_bitmap_data);

  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      url, chrome::FAVICON, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  IconMappingID mapping_id = icon_mappings[0].mapping_id;

  backend_->SetFavicons(url, chrome::FAVICON, favicon_bitmap_data);

  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      url, chrome::FAVICON, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());

  // The same row in the icon_mapping table should be used for the mapping as
  // before.
  EXPECT_EQ(mapping_id, icon_mappings[0].mapping_id);
}

// Test that calling SetFavicons() with FaviconBitmapData of different pixel
// sizes than the initially passed in FaviconBitmapData deletes the no longer
// used favicon bitmaps.
TEST_F(HistoryBackendTest, SetFaviconsDeleteBitmaps) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmallAndLarge(),
                            &favicon_bitmap_data);
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  // Test initial state.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(GetSortedIconMappingsForPageURL(page_url, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url, icon_mappings[0].icon_url);
  EXPECT_EQ(chrome::FAVICON, icon_mappings[0].icon_type);
  chrome::FaviconID favicon_id = icon_mappings[0].icon_id;

  std::vector<FaviconBitmap> favicon_bitmaps;
  EXPECT_TRUE(GetSortedFaviconBitmaps(favicon_id, &favicon_bitmaps));
  EXPECT_EQ(2u, favicon_bitmaps.size());
  FaviconBitmapID small_bitmap_id = favicon_bitmaps[0].bitmap_id;
  EXPECT_NE(0, small_bitmap_id);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmaps[0].bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmaps[0].pixel_size);
  FaviconBitmapID large_bitmap_id = favicon_bitmaps[1].bitmap_id;
  EXPECT_NE(0, large_bitmap_id);
  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmaps[1].bitmap_data));
  EXPECT_EQ(kLargeSize, favicon_bitmaps[1].pixel_size);

  // Call SetFavicons() with bitmap data for only the large bitmap. Check that
  // the small bitmap is in fact deleted.
  GenerateFaviconBitmapData(icon_url, GetSizesLarge(), &favicon_bitmap_data);
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  scoped_refptr<base::RefCountedMemory> bitmap_data_out;
  gfx::Size pixel_size_out;
  EXPECT_FALSE(backend_->thumbnail_db_->GetFaviconBitmap(small_bitmap_id,
      NULL, &bitmap_data_out, &pixel_size_out));
  EXPECT_TRUE(backend_->thumbnail_db_->GetFaviconBitmap(large_bitmap_id,
      NULL, &bitmap_data_out, &pixel_size_out));
  EXPECT_TRUE(BitmapDataEqual('a', bitmap_data_out));
  EXPECT_EQ(kLargeSize, pixel_size_out);

  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  // Call SetFavicons() with no bitmap data. Check that the bitmaps and icon
  // mappings are deleted.
  favicon_bitmap_data.clear();
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  EXPECT_FALSE(backend_->thumbnail_db_->GetFaviconBitmap(large_bitmap_id, NULL,
      NULL, NULL));
  icon_mappings.clear();
  EXPECT_FALSE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));

  // Notifications should have been broadcast for each call to SetFavicons().
  EXPECT_EQ(3, num_broadcasted_notifications());
}

// Test updating a single favicon bitmap's data via SetFavicons.
TEST_F(HistoryBackendTest, SetFaviconsReplaceBitmapData) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon");

  std::vector<unsigned char> data_initial;
  data_initial.push_back('a');

  chrome::FaviconBitmapData bitmap_data_element;
  bitmap_data_element.bitmap_data =
      base::RefCountedBytes::TakeVector(&data_initial);
  bitmap_data_element.pixel_size = kSmallSize;
  bitmap_data_element.icon_url = icon_url;
  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  favicon_bitmap_data.push_back(bitmap_data_element);

  // Add bitmap to the database.
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  chrome::FaviconID original_favicon_id =
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
          icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, original_favicon_id);
  FaviconBitmap original_favicon_bitmap;
  EXPECT_TRUE(
      GetOnlyFaviconBitmap(original_favicon_id, &original_favicon_bitmap));
  EXPECT_TRUE(BitmapDataEqual('a', original_favicon_bitmap.bitmap_data));

  EXPECT_EQ(1, num_broadcasted_notifications());

  // Call SetFavicons() with completely identical data.
  std::vector<unsigned char> updated_data;
  updated_data.push_back('a');
  favicon_bitmap_data[0].bitmap_data = new base::RefCountedBytes(updated_data);
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  chrome::FaviconID updated_favicon_id =
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
          icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, updated_favicon_id);
  FaviconBitmap updated_favicon_bitmap;
  EXPECT_TRUE(
      GetOnlyFaviconBitmap(updated_favicon_id, &updated_favicon_bitmap));
  EXPECT_TRUE(BitmapDataEqual('a', updated_favicon_bitmap.bitmap_data));

  // Because the bitmap data is byte equivalent, no notifications should have
  // been broadcasted.
  EXPECT_EQ(1, num_broadcasted_notifications());

  // Call SetFavicons() with identical data but a different bitmap.
  updated_data[0] = 'b';
  favicon_bitmap_data[0].bitmap_data = new base::RefCountedBytes(updated_data);
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  updated_favicon_id =
      backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
          icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, updated_favicon_id);
  EXPECT_TRUE(
      GetOnlyFaviconBitmap(updated_favicon_id, &updated_favicon_bitmap));
  EXPECT_TRUE(BitmapDataEqual('b', updated_favicon_bitmap.bitmap_data));

  // There should be no churn in FaviconIDs or FaviconBitmapIds even though
  // the bitmap data changed.
  EXPECT_EQ(original_favicon_bitmap.icon_id, updated_favicon_bitmap.icon_id);
  EXPECT_EQ(original_favicon_bitmap.bitmap_id,
            updated_favicon_bitmap.bitmap_id);

  // A notification should have been broadcasted as the favicon bitmap data has
  // changed.
  EXPECT_EQ(2, num_broadcasted_notifications());
}

// Test that if two pages share the same FaviconID, changing the favicon for
// one page does not affect the other.
TEST_F(HistoryBackendTest, SetFaviconsSameFaviconURLForTwoPages) {
  GURL icon_url("http://www.google.com/favicon.ico");
  GURL icon_url_new("http://www.google.com/favicon2.ico");
  GURL page_url1("http://www.google.com");
  GURL page_url2("http://www.google.ca");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmallAndLarge(),
                            &favicon_bitmap_data);

  backend_->SetFavicons(page_url1, chrome::FAVICON, favicon_bitmap_data);

  std::vector<GURL> icon_urls;
  icon_urls.push_back(icon_url);

  std::vector<chrome::FaviconBitmapResult> bitmap_results;
  backend_->UpdateFaviconMappingsAndFetch(
      page_url2, icon_urls, chrome::FAVICON, kSmallSize.width(),
      GetScaleFactors1x2x(), &bitmap_results);

  // Check that the same FaviconID is mapped to both page URLs.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      page_url1, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  chrome::FaviconID favicon_id = icon_mappings[0].icon_id;
  EXPECT_NE(0, favicon_id);

  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      page_url2, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  // Change the icon URL that |page_url1| is mapped to.
  GenerateFaviconBitmapData(icon_url_new, GetSizesSmall(),
                            &favicon_bitmap_data);
  backend_->SetFavicons(page_url1, chrome::FAVICON, favicon_bitmap_data);

  // |page_url1| should map to a new FaviconID and have valid bitmap data.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      page_url1, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url_new, icon_mappings[0].icon_url);
  EXPECT_NE(favicon_id, icon_mappings[0].icon_id);

  std::vector<FaviconBitmap> favicon_bitmaps;
  EXPECT_TRUE(backend_->thumbnail_db_->GetFaviconBitmaps(
      icon_mappings[0].icon_id, &favicon_bitmaps));
  EXPECT_EQ(1u, favicon_bitmaps.size());

  // |page_url2| should still map to the same FaviconID and have valid bitmap
  // data.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      page_url2, &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  favicon_bitmaps.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetFaviconBitmaps(favicon_id,
                                                         &favicon_bitmaps));
  EXPECT_EQ(2u, favicon_bitmaps.size());

  // A notification should have been broadcast for each call to SetFavicons()
  // and each call to UpdateFaviconMappingsAndFetch().
  EXPECT_EQ(3, num_broadcasted_notifications());
}

// Test that no notifications are broadcast as a result of calling
// UpdateFaviconMappingsAndFetch() for an icon URL which is already
// mapped to the passed in page URL.
TEST_F(HistoryBackendTest, UpdateFaviconMappingsAndFetchNoChange) {
  GURL page_url("http://www.google.com");
  GURL icon_url("http://www.google.com/favicon.ico");
  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmall(), &favicon_bitmap_data);

  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  chrome::FaviconID icon_id =
    backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
        icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, icon_id);
  EXPECT_EQ(1, num_broadcasted_notifications());

  std::vector<GURL> icon_urls;
  icon_urls.push_back(icon_url);

  std::vector<chrome::FaviconBitmapResult> bitmap_results;
  backend_->UpdateFaviconMappingsAndFetch(
      page_url, icon_urls, chrome::FAVICON, kSmallSize.width(),
      GetScaleFactors1x2x(), &bitmap_results);

  EXPECT_EQ(icon_id, backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
      icon_url, chrome::FAVICON, NULL));

  // No notification should have been broadcast as no icon mapping, favicon,
  // or favicon bitmap was updated, added or removed.
  EXPECT_EQ(1, num_broadcasted_notifications());
}

// Test repeatedly calling MergeFavicon(). |page_url| is initially not known
// to the database.
TEST_F(HistoryBackendTest, MergeFaviconPageURLNotInDB) {
  GURL page_url("http://www.google.com");
  GURL icon_url("http:/www.google.com/favicon.ico");

  std::vector<unsigned char> data;
  data.push_back('a');
  scoped_refptr<base::RefCountedBytes> bitmap_data(
      new base::RefCountedBytes(data));

  backend_->MergeFavicon(
      page_url, icon_url, chrome::FAVICON, bitmap_data, kSmallSize);

  // |page_url| should now be mapped to |icon_url| and the favicon bitmap should
  // not be expired.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url, icon_mappings[0].icon_url);

  FaviconBitmap favicon_bitmap;
  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  data[0] = 'b';
  bitmap_data = new base::RefCountedBytes(data);
  backend_->MergeFavicon(
      page_url, icon_url, chrome::FAVICON, bitmap_data, kSmallSize);

  // |page_url| should still have a single favicon bitmap. The bitmap data
  // should be updated.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url, icon_mappings[0].icon_url);

  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);
}

// Test calling MergeFavicon() when |page_url| is known to the database.
TEST_F(HistoryBackendTest, MergeFaviconPageURLInDB) {
  GURL page_url("http://www.google.com");
  GURL icon_url1("http:/www.google.com/favicon.ico");
  GURL icon_url2("http://www.google.com/favicon2.ico");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url1, GetSizesSmall(),
                            &favicon_bitmap_data);

  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  // Test initial state.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url1, icon_mappings[0].icon_url);

  FaviconBitmap favicon_bitmap;
  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  EXPECT_EQ(1, num_broadcasted_notifications());

  // 1) Merge identical favicon bitmap.
  std::vector<unsigned char> data;
  data.push_back('a');
  scoped_refptr<base::RefCountedBytes> bitmap_data(
      new base::RefCountedBytes(data));
  backend_->MergeFavicon(
      page_url, icon_url1, chrome::FAVICON, bitmap_data, kSmallSize);

  // All the data should stay the same and no notifications should have been
  // sent.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url1, icon_mappings[0].icon_url);

  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  EXPECT_EQ(1, num_broadcasted_notifications());

  // 2) Merge favicon bitmap of the same size.
  data[0] = 'b';
  bitmap_data = new base::RefCountedBytes(data);
  backend_->MergeFavicon(
      page_url, icon_url1, chrome::FAVICON, bitmap_data, kSmallSize);

  // The small favicon bitmap at |icon_url1| should be overwritten.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url1, icon_mappings[0].icon_url);

  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  // 3) Merge favicon for the same icon URL, but a pixel size for which there is
  // no favicon bitmap.
  data[0] = 'c';
  bitmap_data = new base::RefCountedBytes(data);
  backend_->MergeFavicon(
      page_url, icon_url1, chrome::FAVICON, bitmap_data, kTinySize);

  // A new favicon bitmap should be created and the preexisting favicon bitmap
  // ('b') should be expired.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url1, icon_mappings[0].icon_url);

  std::vector<FaviconBitmap> favicon_bitmaps;
  EXPECT_TRUE(GetSortedFaviconBitmaps(icon_mappings[0].icon_id,
                                      &favicon_bitmaps));
  EXPECT_NE(base::Time(), favicon_bitmaps[0].last_updated);
  EXPECT_TRUE(BitmapDataEqual('c', favicon_bitmaps[0].bitmap_data));
  EXPECT_EQ(kTinySize, favicon_bitmaps[0].pixel_size);
  EXPECT_EQ(base::Time(), favicon_bitmaps[1].last_updated);
  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmaps[1].bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmaps[1].pixel_size);

  // 4) Merge favicon for an icon URL different from the icon URLs already
  // mapped to page URL.
  data[0] = 'd';
  bitmap_data = new base::RefCountedBytes(data);
  backend_->MergeFavicon(
      page_url, icon_url2, chrome::FAVICON, bitmap_data, kSmallSize);

  // The existing favicon bitmaps should be copied over to the newly created
  // favicon at |icon_url2|. |page_url| should solely be mapped to |icon_url2|.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url2, icon_mappings[0].icon_url);

  favicon_bitmaps.clear();
  EXPECT_TRUE(GetSortedFaviconBitmaps(icon_mappings[0].icon_id,
                                      &favicon_bitmaps));
  EXPECT_EQ(base::Time(), favicon_bitmaps[0].last_updated);
  EXPECT_TRUE(BitmapDataEqual('c', favicon_bitmaps[0].bitmap_data));
  EXPECT_EQ(kTinySize, favicon_bitmaps[0].pixel_size);
  // The favicon being merged should take precedence over the preexisting
  // favicon bitmaps.
  EXPECT_NE(base::Time(), favicon_bitmaps[1].last_updated);
  EXPECT_TRUE(BitmapDataEqual('d', favicon_bitmaps[1].bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmaps[1].pixel_size);

  // A notification should have been broadcast for each call to SetFavicons()
  // and MergeFavicon().
  EXPECT_EQ(4, num_broadcasted_notifications());
}

// Test calling MergeFavicon() when |icon_url| is known to the database but not
// mapped to |page_url|.
TEST_F(HistoryBackendTest, MergeFaviconIconURLMappedToDifferentPageURL) {
  GURL page_url1("http://www.google.com");
  GURL page_url2("http://news.google.com");
  GURL page_url3("http://maps.google.com");
  GURL icon_url("http:/www.google.com/favicon.ico");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmall(),
                            &favicon_bitmap_data);

  backend_->SetFavicons(page_url1, chrome::FAVICON, favicon_bitmap_data);

  // Test initial state.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url1,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_url, icon_mappings[0].icon_url);

  FaviconBitmap favicon_bitmap;
  EXPECT_TRUE(GetOnlyFaviconBitmap(icon_mappings[0].icon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  // 1) Merge in an identical favicon bitmap data but for a different page URL.
  std::vector<unsigned char> data;
  data.push_back('a');
  scoped_refptr<base::RefCountedBytes> bitmap_data(
      new base::RefCountedBytes(data));

  backend_->MergeFavicon(
      page_url2, icon_url, chrome::FAVICON, bitmap_data, kSmallSize);

  chrome::FaviconID favicon_id =
    backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
        icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, favicon_id);

  EXPECT_TRUE(GetOnlyFaviconBitmap(favicon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('a', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  // 2) Merging a favicon bitmap with different bitmap data for the same icon
  // URL should overwrite the small favicon bitmap at |icon_url|.
  bitmap_data->data()[0] = 'b';
  backend_->MergeFavicon(
      page_url3, icon_url, chrome::FAVICON, bitmap_data, kSmallSize);

  favicon_id = backend_->thumbnail_db_->GetFaviconIDForFaviconURL(
      icon_url, chrome::FAVICON, NULL);
  EXPECT_NE(0, favicon_id);

  EXPECT_TRUE(GetOnlyFaviconBitmap(favicon_id, &favicon_bitmap));
  EXPECT_NE(base::Time(), favicon_bitmap.last_updated);
  EXPECT_TRUE(BitmapDataEqual('b', favicon_bitmap.bitmap_data));
  EXPECT_EQ(kSmallSize, favicon_bitmap.pixel_size);

  // |icon_url| should be mapped to all three page URLs.
  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url1,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url2,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  icon_mappings.clear();
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url3,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(favicon_id, icon_mappings[0].icon_id);

  // A notification should have been broadcast for each call to SetFavicons()
  // and MergeFavicon().
  EXPECT_EQ(3, num_broadcasted_notifications());
}

// Test that MergeFavicon() does not add more than
// |kMaxFaviconBitmapsPerIconURL| to a favicon.
TEST_F(HistoryBackendTest, MergeFaviconMaxFaviconBitmapsPerIconURL) {
  GURL page_url("http://www.google.com");
  std::string icon_url_string("http://www.google.com/favicon.ico");
  size_t replace_index = icon_url_string.size() - 1;

  std::vector<unsigned char> data;
  data.push_back('a');
  scoped_refptr<base::RefCountedMemory> bitmap_data =
      base::RefCountedBytes::TakeVector(&data);

  int pixel_size = 1;
  for (size_t i = 0; i < kMaxFaviconBitmapsPerIconURL + 1; ++i) {
    icon_url_string[replace_index] = '0' + i;
    GURL icon_url(icon_url_string);

    backend_->MergeFavicon(page_url, icon_url, chrome::FAVICON, bitmap_data,
                           gfx::Size(pixel_size, pixel_size));
    ++pixel_size;
  }

  // There should be a single favicon mapped to |page_url| with exactly
  // kMaxFaviconBitmapsPerIconURL favicon bitmaps.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  std::vector<FaviconBitmap> favicon_bitmaps;
  EXPECT_TRUE(backend_->thumbnail_db_->GetFaviconBitmaps(
      icon_mappings[0].icon_id, &favicon_bitmaps));
  EXPECT_EQ(kMaxFaviconBitmapsPerIconURL, favicon_bitmaps.size());
}

// Tests that the favicon set by MergeFavicon() shows up in the result of
// GetFaviconsForURL().
TEST_F(HistoryBackendTest, MergeFaviconShowsUpInGetFaviconsForURLResult) {
  GURL page_url("http://www.google.com");
  GURL icon_url("http://www.google.com/favicon.ico");
  GURL merged_icon_url("http://wwww.google.com/favicon2.ico");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmallAndLarge(),
                            &favicon_bitmap_data);

  // Set some preexisting favicons for |page_url|.
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  // Merge small favicon.
  std::vector<unsigned char> data;
  data.push_back('c');
  scoped_refptr<base::RefCountedBytes> bitmap_data(
      new base::RefCountedBytes(data));
  backend_->MergeFavicon(
      page_url, merged_icon_url, chrome::FAVICON, bitmap_data, kSmallSize);

  // Request favicon bitmaps for both 1x and 2x to simulate request done by
  // BookmarkModel::GetFavicon().
  std::vector<chrome::FaviconBitmapResult> bitmap_results;
  backend_->GetFaviconsForURL(page_url, chrome::FAVICON, kSmallSize.width(),
                              GetScaleFactors1x2x(), &bitmap_results);

  EXPECT_EQ(2u, bitmap_results.size());
  const chrome::FaviconBitmapResult& first_result = bitmap_results[0];
  const chrome::FaviconBitmapResult& result =
      (first_result.pixel_size == kSmallSize) ? first_result
                                              : bitmap_results[1];
  EXPECT_TRUE(BitmapDataEqual('c', result.bitmap_data));
}

// Test UpdateFaviconMapingsAndFetch() when multiple icon types are passed in.
TEST_F(HistoryBackendTest, UpdateFaviconMappingsAndFetchMultipleIconTypes) {
  GURL page_url1("http://www.google.com");
  GURL page_url2("http://news.google.com");
  GURL page_url3("http://mail.google.com");
  GURL icon_urla("http://www.google.com/favicon1.ico");
  GURL icon_urlb("http://www.google.com/favicon2.ico");
  GURL icon_urlc("http://www.google.com/favicon3.ico");

  // |page_url1| is mapped to |icon_urla| which if of type TOUCH_ICON.
  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_urla, GetSizesSmall(), &favicon_bitmap_data);
  backend_->SetFavicons(page_url1, chrome::TOUCH_ICON, favicon_bitmap_data);

  // |page_url2| is mapped to |icon_urlb| and |icon_urlc| which are of type
  // TOUCH_PRECOMPOSED_ICON.
  GenerateFaviconBitmapData(icon_urlb, GetSizesSmall(), icon_urlc,
                            GetSizesSmall(), &favicon_bitmap_data);
  backend_->SetFavicons(
      page_url2, chrome::TOUCH_PRECOMPOSED_ICON, favicon_bitmap_data);

  std::vector<GURL> icon_urls;
  icon_urls.push_back(icon_urla);
  icon_urls.push_back(icon_urlb);
  icon_urls.push_back(icon_urlc);

  std::vector<chrome::FaviconBitmapResult> bitmap_results;
  backend_->UpdateFaviconMappingsAndFetch(
      page_url3,
      icon_urls,
      (chrome::TOUCH_ICON | chrome::TOUCH_PRECOMPOSED_ICON),
      kSmallSize.width(),
      GetScaleFactors1x2x(),
      &bitmap_results);

  // |page_url1| and |page_url2| should still be mapped to the same icon URLs.
  std::vector<IconMapping> icon_mappings;
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(page_url1,
      &icon_mappings));
  EXPECT_EQ(1u, icon_mappings.size());
  EXPECT_EQ(icon_urla, icon_mappings[0].icon_url);
  EXPECT_EQ(chrome::TOUCH_ICON, icon_mappings[0].icon_type);

  icon_mappings.clear();
  EXPECT_TRUE(GetSortedIconMappingsForPageURL(page_url2, &icon_mappings));
  EXPECT_EQ(2u, icon_mappings.size());
  EXPECT_EQ(icon_urlb, icon_mappings[0].icon_url);
  EXPECT_EQ(chrome::TOUCH_PRECOMPOSED_ICON, icon_mappings[0].icon_type);
  EXPECT_EQ(icon_urlc, icon_mappings[1].icon_url);
  EXPECT_EQ(chrome::TOUCH_PRECOMPOSED_ICON, icon_mappings[1].icon_type);

  // |page_url3| should be mapped only to |icon_urlb| and |icon_urlc| as
  // TOUCH_PRECOMPOSED_ICON is the largest IconType.
  icon_mappings.clear();
  EXPECT_TRUE(GetSortedIconMappingsForPageURL(page_url3, &icon_mappings));
  EXPECT_EQ(2u, icon_mappings.size());
  EXPECT_EQ(icon_urlb, icon_mappings[0].icon_url);
  EXPECT_EQ(chrome::TOUCH_PRECOMPOSED_ICON, icon_mappings[0].icon_type);
  EXPECT_EQ(icon_urlc, icon_mappings[1].icon_url);
  EXPECT_EQ(chrome::TOUCH_PRECOMPOSED_ICON, icon_mappings[1].icon_type);
}

// Test the results of GetFaviconsFromDB() when there are no found
// favicons.
TEST_F(HistoryBackendTest, GetFaviconsFromDBEmpty) {
  const GURL page_url("http://www.google.com/");

  std::vector<chrome::FaviconBitmapResult> bitmap_results;
  EXPECT_FALSE(backend_->GetFaviconsFromDB(page_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results));
  EXPECT_TRUE(bitmap_results.empty());
}

// Test the results of GetFaviconsFromDB() when there are matching favicons
// but there are no associated favicon bitmaps.
TEST_F(HistoryBackendTest, GetFaviconsFromDBNoFaviconBitmaps) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon1");

  chrome::FaviconID icon_id = backend_->thumbnail_db_->AddFavicon(
      icon_url, chrome::FAVICON, GetSizesSmallAndLarge());
  EXPECT_NE(0, icon_id);
  EXPECT_NE(0, backend_->thumbnail_db_->AddIconMapping(page_url, icon_id));

  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_FALSE(backend_->GetFaviconsFromDB(page_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));
  EXPECT_TRUE(bitmap_results_out.empty());
}

// Test that GetFaviconsFromDB() returns results for the bitmaps which most
// closely match the passed in desired size and scale factors.
TEST_F(HistoryBackendTest, GetFaviconsFromDBSelectClosestMatch) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon1");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesTinySmallAndLarge(),
                            &favicon_bitmap_data);

  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_TRUE(backend_->GetFaviconsFromDB(page_url,
                                          chrome::FAVICON,
                                          kSmallSize.width(),
                                          GetScaleFactors1x2x(),
                                          &bitmap_results_out));

  // The bitmap data for the small and large bitmaps should be returned as their
  // sizes match exactly.
  EXPECT_EQ(2u, bitmap_results_out.size());
  // No required order for results.
  if (bitmap_results_out[0].pixel_size == kLargeSize) {
    chrome::FaviconBitmapResult tmp_result = bitmap_results_out[0];
    bitmap_results_out[0] = bitmap_results_out[1];
    bitmap_results_out[1] = tmp_result;
  }

  EXPECT_FALSE(bitmap_results_out[0].expired);
  EXPECT_TRUE(BitmapDataEqual('b', bitmap_results_out[0].bitmap_data));
  EXPECT_EQ(kSmallSize, bitmap_results_out[0].pixel_size);
  EXPECT_EQ(icon_url, bitmap_results_out[0].icon_url);
  EXPECT_EQ(chrome::FAVICON, bitmap_results_out[0].icon_type);

  EXPECT_FALSE(bitmap_results_out[1].expired);
  EXPECT_TRUE(BitmapDataEqual('c', bitmap_results_out[1].bitmap_data));
  EXPECT_EQ(kLargeSize, bitmap_results_out[1].pixel_size);
  EXPECT_EQ(icon_url, bitmap_results_out[1].icon_url);
  EXPECT_EQ(chrome::FAVICON, bitmap_results_out[1].icon_type);
}

// Test that GetFaviconsFromDB() returns results from the icon URL whose
// bitmaps most closely match the passed in desired size and scale factors.
TEST_F(HistoryBackendTest, GetFaviconsFromDBSingleIconURL) {
  const GURL page_url("http://www.google.com/");

  const GURL icon_url1("http://www.google.com/icon1");
  const GURL icon_url2("http://www.google.com/icon2");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url1, GetSizesSmall(), icon_url2,
                            GetSizesLarge(), &favicon_bitmap_data);

  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_TRUE(backend_->GetFaviconsFromDB(page_url,
                                          chrome::FAVICON,
                                          kSmallSize.width(),
                                          GetScaleFactors1x2x(),
                                          &bitmap_results_out));

  // The results should have results for the icon URL with the large bitmap as
  // downscaling is preferred to upscaling.
  EXPECT_EQ(1u, bitmap_results_out.size());
  EXPECT_EQ(kLargeSize, bitmap_results_out[0].pixel_size);
  EXPECT_EQ(icon_url2, bitmap_results_out[0].icon_url);
}

// Test the results of GetFaviconsFromDB() when called with different
// |icon_types|.
TEST_F(HistoryBackendTest, GetFaviconsFromDBIconType) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url1("http://www.google.com/icon1.png");
  const GURL icon_url2("http://www.google.com/icon2.png");

  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url1, GetSizesSmall(),  &favicon_bitmap_data);
  backend_->SetFavicons(page_url, chrome::FAVICON, favicon_bitmap_data);

  GenerateFaviconBitmapData(icon_url2, GetSizesSmall(), &favicon_bitmap_data);
  backend_->SetFavicons(page_url, chrome::TOUCH_ICON, favicon_bitmap_data);

  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_TRUE(backend_->GetFaviconsFromDB(page_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));

  EXPECT_EQ(1u, bitmap_results_out.size());
  EXPECT_EQ(chrome::FAVICON, bitmap_results_out[0].icon_type);
  EXPECT_EQ(icon_url1, bitmap_results_out[0].icon_url);

  bitmap_results_out.clear();
  EXPECT_TRUE(backend_->GetFaviconsFromDB(page_url, chrome::TOUCH_ICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));

  EXPECT_EQ(1u, bitmap_results_out.size());
  EXPECT_EQ(chrome::TOUCH_ICON, bitmap_results_out[0].icon_type);
  EXPECT_EQ(icon_url2, bitmap_results_out[0].icon_url);
}

// Test that GetFaviconsFromDB() correctly sets the expired flag for bitmap
// reults.
TEST_F(HistoryBackendTest, GetFaviconsFromDBExpired) {
  const GURL page_url("http://www.google.com/");
  const GURL icon_url("http://www.google.com/icon.png");

  std::vector<unsigned char> data;
  data.push_back('a');
  scoped_refptr<base::RefCountedBytes> bitmap_data(
      base::RefCountedBytes::TakeVector(&data));
  base::Time last_updated = base::Time::FromTimeT(0);
  chrome::FaviconID icon_id =
      backend_->thumbnail_db_->AddFavicon(icon_url,
                                          chrome::FAVICON,
                                          GetSizesSmallAndLarge(),
                                          bitmap_data,
                                          last_updated,
                                          kSmallSize);
  EXPECT_NE(0, icon_id);
  EXPECT_NE(0, backend_->thumbnail_db_->AddIconMapping(page_url, icon_id));

  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_TRUE(backend_->GetFaviconsFromDB(page_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));

  EXPECT_EQ(1u, bitmap_results_out.size());
  EXPECT_TRUE(bitmap_results_out[0].expired);
}

// Check that UpdateFaviconMappingsAndFetch() call back to the UI when there is
// no valid thumbnail database.
TEST_F(HistoryBackendTest, UpdateFaviconMappingsAndFetchNoDB) {
  // Make the thumbnail database invalid.
  backend_->thumbnail_db_.reset();

  std::vector<chrome::FaviconBitmapResult> bitmap_results;

  backend_->UpdateFaviconMappingsAndFetch(
      GURL(), std::vector<GURL>(), chrome::FAVICON, kSmallSize.width(),
      GetScaleFactors1x2x(), &bitmap_results);

  EXPECT_TRUE(bitmap_results.empty());
}

TEST_F(HistoryBackendTest, CloneFaviconIsRestrictedToSameDomain) {
  const GURL url("http://www.google.com/");
  const GURL same_domain_url("http://www.google.com/subdir/index.html");
  const GURL foreign_domain_url("http://www.not-google.com/");
  const GURL icon_url("http://www.google.com/icon.png");

  // Add a favicon
  std::vector<chrome::FaviconBitmapData> favicon_bitmap_data;
  GenerateFaviconBitmapData(icon_url, GetSizesSmall(),  &favicon_bitmap_data);
  backend_->SetFavicons(url, chrome::FAVICON, favicon_bitmap_data);
  EXPECT_TRUE(backend_->thumbnail_db_->GetIconMappingsForPageURL(
      url, chrome::FAVICON, NULL));

  // Validate starting state.
  std::vector<chrome::FaviconBitmapResult> bitmap_results_out;
  EXPECT_TRUE(backend_->GetFaviconsFromDB(url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));
  EXPECT_FALSE(backend_->GetFaviconsFromDB(same_domain_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));
  EXPECT_FALSE(backend_->GetFaviconsFromDB(foreign_domain_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));

  // Same-domain cloning should work.
  backend_->CloneFavicons(url, same_domain_url);
  EXPECT_TRUE(backend_->GetFaviconsFromDB(same_domain_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));

  // Foreign-domain cloning is forbidden.
  backend_->CloneFavicons(url, foreign_domain_url);
  EXPECT_FALSE(backend_->GetFaviconsFromDB(foreign_domain_url, chrome::FAVICON,
      kSmallSize.width(), GetScaleFactors1x2x(), &bitmap_results_out));
}

TEST_F(HistoryBackendTest, QueryFilteredURLs) {
  const char* google = "http://www.google.com/";
  const char* yahoo = "http://www.yahoo.com/";
  const char* yahoo_sports = "http://sports.yahoo.com/";
  const char* yahoo_sports_with_article1 =
      "http://sports.yahoo.com/article1.htm";
  const char* yahoo_sports_with_article2 =
      "http://sports.yahoo.com/article2.htm";
  const char* yahoo_sports_soccer = "http://sports.yahoo.com/soccer";
  const char* apple = "http://www.apple.com/";

  // Clear all history.
  backend_->DeleteAllHistory();

  Time tested_time = Time::Now().LocalMidnight() +
                     base::TimeDelta::FromHours(4);
  base::TimeDelta half_an_hour = base::TimeDelta::FromMinutes(30);
  base::TimeDelta one_hour = base::TimeDelta::FromHours(1);
  base::TimeDelta one_day = base::TimeDelta::FromDays(1);

  const content::PageTransition kTypedTransition =
      content::PAGE_TRANSITION_TYPED;
  const content::PageTransition kKeywordGeneratedTransition =
      content::PAGE_TRANSITION_KEYWORD_GENERATED;

  const char* redirect_sequence[2];
  redirect_sequence[1] = NULL;

  redirect_sequence[0] = google;
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kTypedTransition,
      tested_time - one_day - half_an_hour * 2);
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0,
      kTypedTransition, tested_time - one_day);
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0,
      kTypedTransition, tested_time - half_an_hour / 2);
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0,
      kTypedTransition, tested_time);

  // Add a visit with a transition that will make sure that no segment gets
  // created for this page (so the subsequent entries will have different URLIDs
  // and SegmentIDs).
  redirect_sequence[0] = apple;
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kKeywordGeneratedTransition,
      tested_time - one_day + one_hour * 6);

  redirect_sequence[0] = yahoo;
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kTypedTransition,
      tested_time - one_day + half_an_hour);
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kTypedTransition,
      tested_time - one_day + half_an_hour * 2);

  redirect_sequence[0] = yahoo_sports;
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kTypedTransition,
      tested_time - one_day - half_an_hour * 2);
  AddRedirectChainWithTransitionAndTime(
      redirect_sequence, 0, kTypedTransition,
      tested_time - one_day);
  int transition1, transition2;
  AddClientRedirect(GURL(yahoo_sports), GURL(yahoo_sports_with_article1), false,
                    tested_time - one_day + half_an_hour,
                    &transition1, &transition2);
  AddClientRedirect(GURL(yahoo_sports_with_article1),
                    GURL(yahoo_sports_with_article2),
                    false,
                    tested_time - one_day + half_an_hour * 2,
                    &transition1, &transition2);

  redirect_sequence[0] = yahoo_sports_soccer;
  AddRedirectChainWithTransitionAndTime(redirect_sequence, 0,
                                        kTypedTransition,
                                        tested_time - half_an_hour);
  backend_->Commit();

  scoped_refptr<QueryFilteredURLsRequest> request1 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  HistoryBackendCancelableRequest cancellable_request;
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request1);

  VisitFilter filter;
  // Time limit is |tested_time| +/- 45 min.
  base::TimeDelta three_quarters_of_an_hour = base::TimeDelta::FromMinutes(45);
  filter.SetFilterTime(tested_time);
  filter.SetFilterWidth(three_quarters_of_an_hour);
  backend_->QueryFilteredURLs(request1, 100, filter, false);

  ASSERT_EQ(4U, get_filtered_list().size());
  EXPECT_EQ(std::string(google), get_filtered_list()[0].url.spec());
  EXPECT_EQ(std::string(yahoo_sports_soccer),
            get_filtered_list()[1].url.spec());
  EXPECT_EQ(std::string(yahoo), get_filtered_list()[2].url.spec());
  EXPECT_EQ(std::string(yahoo_sports),
            get_filtered_list()[3].url.spec());

  // Time limit is between |tested_time| and |tested_time| + 2 hours.
  scoped_refptr<QueryFilteredURLsRequest> request2 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request2);
  filter.SetFilterTime(tested_time + one_hour);
  filter.SetFilterWidth(one_hour);
  backend_->QueryFilteredURLs(request2, 100, filter, false);

  ASSERT_EQ(3U, get_filtered_list().size());
  EXPECT_EQ(std::string(google), get_filtered_list()[0].url.spec());
  EXPECT_EQ(std::string(yahoo), get_filtered_list()[1].url.spec());
  EXPECT_EQ(std::string(yahoo_sports), get_filtered_list()[2].url.spec());

  // Time limit is between |tested_time| - 2 hours and |tested_time|.
  scoped_refptr<QueryFilteredURLsRequest> request3 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request3);
  filter.SetFilterTime(tested_time - one_hour);
  filter.SetFilterWidth(one_hour);
  backend_->QueryFilteredURLs(request3, 100, filter, false);

  ASSERT_EQ(3U, get_filtered_list().size());
  EXPECT_EQ(std::string(google), get_filtered_list()[0].url.spec());
  EXPECT_EQ(std::string(yahoo_sports_soccer),
            get_filtered_list()[1].url.spec());
  EXPECT_EQ(std::string(yahoo_sports), get_filtered_list()[2].url.spec());

  filter.ClearFilters();
  base::Time::Exploded exploded_time;
  tested_time.LocalExplode(&exploded_time);

  // Today.
  scoped_refptr<QueryFilteredURLsRequest> request4 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request4);
  filter.SetFilterTime(tested_time);
  filter.SetDayOfTheWeekFilter(static_cast<int>(exploded_time.day_of_week));
  backend_->QueryFilteredURLs(request4, 100, filter, false);

  ASSERT_EQ(2U, get_filtered_list().size());
  EXPECT_EQ(std::string(google), get_filtered_list()[0].url.spec());
  EXPECT_EQ(std::string(yahoo_sports_soccer),
            get_filtered_list()[1].url.spec());

  // Today + time limit - only yahoo_sports_soccer should fit.
  scoped_refptr<QueryFilteredURLsRequest> request5 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request5);
  filter.SetFilterTime(tested_time - base::TimeDelta::FromMinutes(40));
  filter.SetFilterWidth(base::TimeDelta::FromMinutes(20));
  backend_->QueryFilteredURLs(request5, 100, filter, false);

  ASSERT_EQ(1U, get_filtered_list().size());
  EXPECT_EQ(std::string(yahoo_sports_soccer),
            get_filtered_list()[0].url.spec());

  // Make sure we get debug data if we request it.
  scoped_refptr<QueryFilteredURLsRequest> request6 =
      new history::QueryFilteredURLsRequest(
          base::Bind(&HistoryBackendTest::OnQueryFiltered,
                     base::Unretained(static_cast<HistoryBackendTest*>(this))));
  cancellable_request.MockScheduleOfRequest<QueryFilteredURLsRequest>(
      request6);
  filter.SetFilterTime(tested_time);
  filter.SetFilterWidth(one_hour * 2);
  backend_->QueryFilteredURLs(request6, 100, filter, true);

  // If the SegmentID is used by QueryFilteredURLs when generating the debug
  // data instead of the URLID, the |total_visits| for the |yahoo_sports_soccer|
  // entry will be zero instead of 1.
  ASSERT_GE(get_filtered_list().size(), 2U);
  EXPECT_EQ(std::string(google), get_filtered_list()[0].url.spec());
  EXPECT_EQ(std::string(yahoo_sports_soccer),
      get_filtered_list()[1].url.spec());
  EXPECT_EQ(4U, get_filtered_list()[0].extended_info.total_visits);
  EXPECT_EQ(1U, get_filtered_list()[1].extended_info.total_visits);
}

TEST_F(HistoryBackendTest, UpdateVisitDuration) {
  // This unit test will test adding and deleting visit details information.
  ASSERT_TRUE(backend_.get());

  GURL url1("http://www.cnn.com");
  std::vector<VisitInfo> visit_info1, visit_info2;
  Time start_ts = Time::Now() - base::TimeDelta::FromDays(5);
  Time end_ts = start_ts + base::TimeDelta::FromDays(2);
  visit_info1.push_back(VisitInfo(start_ts, content::PAGE_TRANSITION_LINK));

  GURL url2("http://www.example.com");
  visit_info2.push_back(VisitInfo(Time::Now() - base::TimeDelta::FromDays(10),
                                  content::PAGE_TRANSITION_LINK));

  // Clear all history.
  backend_->DeleteAllHistory();

  // Add the visits.
  backend_->AddVisits(url1, visit_info1, history::SOURCE_BROWSED);
  backend_->AddVisits(url2, visit_info2, history::SOURCE_BROWSED);

  // Verify the entries for both visits were added in visit_details.
  VisitVector visits1, visits2;
  URLRow row;
  URLID url_id1 = backend_->db()->GetRowForURL(url1, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(url_id1, &visits1));
  ASSERT_EQ(1U, visits1.size());
  EXPECT_EQ(0, visits1[0].visit_duration.ToInternalValue());

  URLID url_id2 = backend_->db()->GetRowForURL(url2, &row);
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(url_id2, &visits2));
  ASSERT_EQ(1U, visits2.size());
  EXPECT_EQ(0, visits2[0].visit_duration.ToInternalValue());

  // Update the visit to cnn.com.
  backend_->UpdateVisitDuration(visits1[0].visit_id, end_ts);

  // Check the duration for visiting cnn.com was correctly updated.
  ASSERT_TRUE(backend_->db()->GetVisitsForURL(url_id1, &visits1));
  ASSERT_EQ(1U, visits1.size());
  base::TimeDelta expected_duration = end_ts - start_ts;
  EXPECT_EQ(expected_duration.ToInternalValue(),
            visits1[0].visit_duration.ToInternalValue());

  // Remove the visit to cnn.com.
  ASSERT_TRUE(backend_->RemoveVisits(visits1));
}

// Test for migration of adding visit_duration column.
TEST_F(HistoryBackendTest, MigrationVisitDuration) {
  ASSERT_TRUE(backend_.get());
  backend_->Closing();
  backend_ = NULL;

  base::FilePath old_history_path, old_history, old_archived;
  ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &old_history_path));
  old_history_path = old_history_path.AppendASCII("History");
  old_history = old_history_path.AppendASCII("HistoryNoDuration");
  old_archived = old_history_path.AppendASCII("ArchivedNoDuration");

  // Copy history database file to current directory so that it will be deleted
  // in Teardown.
  base::FilePath new_history_path(getTestDir());
  file_util::Delete(new_history_path, true);
  file_util::CreateDirectory(new_history_path);
  base::FilePath new_history_file =
      new_history_path.Append(chrome::kHistoryFilename);
  base::FilePath new_archived_file =
      new_history_path.Append(chrome::kArchivedHistoryFilename);
  ASSERT_TRUE(file_util::CopyFile(old_history, new_history_file));
  ASSERT_TRUE(file_util::CopyFile(old_archived, new_archived_file));

  backend_ = new HistoryBackend(new_history_path,
                                0,
                                new HistoryBackendTestDelegate(this),
                                &bookmark_model_);
  backend_->Init(std::string(), false);
  backend_->Closing();
  backend_ = NULL;

  // Now both history and archived_history databases should already be migrated.

  // Check version in history database first.
  int cur_version = HistoryDatabase::GetCurrentVersion();
  sql::Connection db;
  ASSERT_TRUE(db.Open(new_history_file));
  sql::Statement s(db.GetUniqueStatement(
      "SELECT value FROM meta WHERE key = 'version'"));
  ASSERT_TRUE(s.Step());
  int file_version = s.ColumnInt(0);
  EXPECT_EQ(cur_version, file_version);

  // Check visit_duration column in visits table is created and set to 0.
  s.Assign(db.GetUniqueStatement(
      "SELECT visit_duration FROM visits LIMIT 1"));
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(0, s.ColumnInt(0));

  // Repeat version and visit_duration checks in archived history database
  // also.
  cur_version = ArchivedDatabase::GetCurrentVersion();
  sql::Connection archived_db;
  ASSERT_TRUE(archived_db.Open(new_archived_file));
  sql::Statement s1(archived_db.GetUniqueStatement(
      "SELECT value FROM meta WHERE key = 'version'"));
  ASSERT_TRUE(s1.Step());
  file_version = s1.ColumnInt(0);
  EXPECT_EQ(cur_version, file_version);

  // Check visit_duration column in visits table is created and set to 0.
  s1.Assign(archived_db.GetUniqueStatement(
      "SELECT visit_duration FROM visits LIMIT 1"));
  ASSERT_TRUE(s1.Step());
  EXPECT_EQ(0, s1.ColumnInt(0));
}

TEST_F(HistoryBackendTest, AddPageNoVisitForBookmark) {
  ASSERT_TRUE(backend_.get());

  GURL url("http://www.google.com");
  string16 title(UTF8ToUTF16("Bookmark title"));
  backend_->AddPageNoVisitForBookmark(url, title);

  URLRow row;
  backend_->GetURL(url, &row);
  EXPECT_EQ(url, row.url());
  EXPECT_EQ(title, row.title());
  EXPECT_EQ(0, row.visit_count());

  backend_->DeleteURL(url);
  backend_->AddPageNoVisitForBookmark(url, string16());
  backend_->GetURL(url, &row);
  EXPECT_EQ(url, row.url());
  EXPECT_EQ(UTF8ToUTF16(url.spec()), row.title());
  EXPECT_EQ(0, row.visit_count());
}

TEST_F(HistoryBackendTest, ExpireHistoryForTimes) {
  ASSERT_TRUE(backend_.get());

  HistoryAddPageArgs args[10];
  for (size_t i = 0; i < arraysize(args); ++i) {
    args[i].url = GURL("http://example" +
                       std::string((i % 2 == 0 ? ".com" : ".net")));
    args[i].time = base::Time::FromInternalValue(i);
    backend_->AddPage(args[i]);
  }
  EXPECT_EQ(base::Time(), backend_->GetFirstRecordedTimeForTest());

  URLRow row;
  for (size_t i = 0; i < arraysize(args); ++i) {
    EXPECT_TRUE(backend_->GetURL(args[i].url, &row));
  }

  std::set<base::Time> times;
  times.insert(args[5].time);
  backend_->ExpireHistoryForTimes(times,
                                  base::Time::FromInternalValue(2),
                                  base::Time::FromInternalValue(8));

  EXPECT_EQ(base::Time::FromInternalValue(0),
            backend_->GetFirstRecordedTimeForTest());

  // Visits to http://example.com are untouched.
  VisitVector visit_vector;
  EXPECT_TRUE(backend_->GetVisitsForURL(
      backend_->db_->GetRowForURL(GURL("http://example.com"), NULL),
      &visit_vector));
  ASSERT_EQ(5u, visit_vector.size());
  EXPECT_EQ(base::Time::FromInternalValue(0), visit_vector[0].visit_time);
  EXPECT_EQ(base::Time::FromInternalValue(2), visit_vector[1].visit_time);
  EXPECT_EQ(base::Time::FromInternalValue(4), visit_vector[2].visit_time);
  EXPECT_EQ(base::Time::FromInternalValue(6), visit_vector[3].visit_time);
  EXPECT_EQ(base::Time::FromInternalValue(8), visit_vector[4].visit_time);

  // Visits to http://example.net between [2,8] are removed.
  visit_vector.clear();
  EXPECT_TRUE(backend_->GetVisitsForURL(
      backend_->db_->GetRowForURL(GURL("http://example.net"), NULL),
      &visit_vector));
  ASSERT_EQ(2u, visit_vector.size());
  EXPECT_EQ(base::Time::FromInternalValue(1), visit_vector[0].visit_time);
  EXPECT_EQ(base::Time::FromInternalValue(9), visit_vector[1].visit_time);

  EXPECT_EQ(base::Time::FromInternalValue(0),
            backend_->GetFirstRecordedTimeForTest());
}

TEST_F(HistoryBackendTest, ExpireHistory) {
  ASSERT_TRUE(backend_.get());
  // Since history operations are dependent on the local timezone, make all
  // entries relative to a fixed, local reference time.
  base::Time reference_time = base::Time::UnixEpoch().LocalMidnight() +
                              base::TimeDelta::FromHours(12);

  // Insert 4 entries into the database.
  HistoryAddPageArgs args[4];
  for (size_t i = 0; i < arraysize(args); ++i) {
    args[i].url = GURL("http://example" + base::IntToString(i) + ".com");
    args[i].time = reference_time + base::TimeDelta::FromDays(i);
    backend_->AddPage(args[i]);
  }

  URLRow url_rows[4];
  for (unsigned int i = 0; i < arraysize(args); ++i)
    ASSERT_TRUE(backend_->GetURL(args[i].url, &url_rows[i]));

  std::vector<ExpireHistoryArgs> expire_list;
  VisitVector visits;

  // Passing an empty map should be a no-op.
  backend_->ExpireHistory(expire_list);
  backend_->db()->GetAllVisitsInRange(base::Time(), base::Time(), 0, &visits);
  EXPECT_EQ(4U, visits.size());

  // Trying to delete an unknown URL with the time of the first visit should
  // also be a no-op.
  expire_list.resize(expire_list.size() + 1);
  expire_list[0].SetTimeRangeForOneDay(args[0].time);
  expire_list[0].urls.insert(GURL("http://google.does-not-exist"));
  backend_->ExpireHistory(expire_list);
  backend_->db()->GetAllVisitsInRange(base::Time(), base::Time(), 0, &visits);
  EXPECT_EQ(4U, visits.size());

  // Now add the first URL with the same time -- it should get deleted.
  expire_list.back().urls.insert(url_rows[0].url());
  backend_->ExpireHistory(expire_list);

  backend_->db()->GetAllVisitsInRange(base::Time(), base::Time(), 0, &visits);
  ASSERT_EQ(3U, visits.size());
  EXPECT_EQ(visits[0].url_id, url_rows[1].id());
  EXPECT_EQ(visits[1].url_id, url_rows[2].id());
  EXPECT_EQ(visits[2].url_id, url_rows[3].id());

  // The first recorded time should also get updated.
  EXPECT_EQ(backend_->GetFirstRecordedTimeForTest(), args[1].time);

  // Now delete the rest of the visits in one call.
  for (unsigned int i = 1; i < arraysize(args); ++i) {
    expire_list.resize(expire_list.size() + 1);
    expire_list[i].SetTimeRangeForOneDay(args[i].time);
    expire_list[i].urls.insert(args[i].url);
  }
  backend_->ExpireHistory(expire_list);

  backend_->db()->GetAllVisitsInRange(base::Time(), base::Time(), 0, &visits);
  ASSERT_EQ(0U, visits.size());
}

class HistoryBackendSegmentDurationTest : public HistoryBackendTest {
 public:
  HistoryBackendSegmentDurationTest() {}

  virtual void SetUp() {
    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kTrackActiveVisitTime);
    HistoryBackendTest::SetUp();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryBackendSegmentDurationTest);
};

// Assertions around segment durations.
TEST_F(HistoryBackendSegmentDurationTest, SegmentDuration) {
  const GURL url1("http://www.google.com");
  const GURL url2("http://www.foo.com/m");
  const std::string segment1(VisitSegmentDatabase::ComputeSegmentName(url1));
  const std::string segment2(VisitSegmentDatabase::ComputeSegmentName(url2));

  Time segment_time(VisitSegmentDatabase::SegmentTime(Time::Now()));
  URLRow url_info1(url1);
  url_info1.set_visit_count(0);
  url_info1.set_typed_count(0);
  url_info1.set_last_visit(segment_time);
  url_info1.set_hidden(false);
  const URLID url1_id = backend_->db()->AddURL(url_info1);
  EXPECT_NE(0, url1_id);

  URLRow url_info2(url2);
  url_info2.set_visit_count(0);
  url_info2.set_typed_count(0);
  url_info2.set_last_visit(Time());
  url_info2.set_hidden(false);
  const URLID url2_id = backend_->db()->AddURL(url_info2);
  EXPECT_NE(0, url2_id);
  EXPECT_NE(url1_id, url2_id);

  // Should not have any segments for the urls.
  EXPECT_EQ(0, backend_->db()->GetSegmentNamed(segment1));
  EXPECT_EQ(0, backend_->db()->GetSegmentNamed(segment2));

  // Update the duration, which should implicitly create the segments.
  const TimeDelta segment1_time_delta(TimeDelta::FromHours(1));
  const TimeDelta segment2_time_delta(TimeDelta::FromHours(2));
  backend_->IncreaseSegmentDuration(url1, segment_time, segment1_time_delta);
  backend_->IncreaseSegmentDuration(url2, segment_time, segment2_time_delta);

  // Get the ids of the segments that were created.
  const SegmentID segment1_id = backend_->db()->GetSegmentNamed(segment1);
  EXPECT_NE(0, segment1_id);
  const SegmentID segment2_id = backend_->db()->GetSegmentNamed(segment2);
  EXPECT_NE(0, segment2_id);
  EXPECT_NE(segment1_id, segment2_id);

  // Make sure the values made it to the db.
  SegmentDurationID segment1_duration_id;
  TimeDelta fetched_delta;
  EXPECT_TRUE(backend_->db()->GetSegmentDuration(
                  segment1_id, segment_time, &segment1_duration_id,
                  &fetched_delta));
  EXPECT_NE(0, segment1_duration_id);
  EXPECT_EQ(segment1_time_delta.InHours(), fetched_delta.InHours());

  SegmentDurationID segment2_duration_id;
  EXPECT_TRUE(backend_->db()->GetSegmentDuration(
                  segment2_id, segment_time, &segment2_duration_id,
                  &fetched_delta));
  EXPECT_NE(0, segment2_duration_id);
  EXPECT_NE(segment1_duration_id, segment2_duration_id);
  EXPECT_EQ(segment2_time_delta.InHours(), fetched_delta.InHours());

  // Query by duration. |url2| should be first as it has a longer view time.
  ScopedVector<PageUsageData> data;
  backend_->db()->QuerySegmentDuration(segment_time, 10, &data.get());
  ASSERT_EQ(2u, data.size());
  EXPECT_EQ(url2.spec(), data[0]->GetURL().spec());
  EXPECT_EQ(url2_id, data[0]->GetID());
  EXPECT_EQ(segment2_time_delta.InHours(), data[0]->duration().InHours());

  EXPECT_EQ(url1.spec(), data[1]->GetURL().spec());
  EXPECT_EQ(url1_id, data[1]->GetID());
  EXPECT_EQ(segment1_time_delta.InHours(), data[1]->duration().InHours());
}

// Simple test that removes a bookmark. This test exercises the code paths in
// History that block till bookmark bar model is loaded.
TEST_F(HistoryBackendTest, RemoveNotification) {
  scoped_ptr<TestingProfile> profile(new TestingProfile());

  profile->CreateHistoryService(false, false);
  profile->CreateBookmarkModel(true);
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile.get());
  ui_test_utils::WaitForBookmarkModelToLoad(model);

  // Add a URL.
  GURL url("http://www.google.com");
  bookmark_utils::AddIfNotBookmarked(model, url, base::string16());

  HistoryService* service = HistoryServiceFactory::GetForProfile(
      profile.get(), Profile::EXPLICIT_ACCESS);

  service->AddPage(
      url, base::Time::Now(), NULL, 1, GURL(), RedirectList(),
      content::PAGE_TRANSITION_TYPED, SOURCE_BROWSED, false);

  // This won't actually delete the URL, rather it'll empty out the visits.
  // This triggers blocking on the BookmarkModel.
  service->DeleteURL(url);
}

}  // namespace history
