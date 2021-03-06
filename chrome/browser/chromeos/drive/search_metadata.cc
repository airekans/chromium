// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/search_metadata.h"

#include <algorithm>
#include <queue>

#include "base/bind.h"
#include "base/i18n/string_search.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/drive/file_cache.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/escape.h"

using content::BrowserThread;

namespace drive {
namespace internal {

namespace {

// Used to sort the result candidates per the last accessed/modified time. The
// recently accessed/modified files come first.
bool CompareByTimestamp(const ResourceEntry& a, const ResourceEntry& b) {
  const PlatformFileInfoProto& a_file_info = a.file_info();
  const PlatformFileInfoProto& b_file_info = b.file_info();

  if (a_file_info.last_accessed() != b_file_info.last_accessed())
    return a_file_info.last_accessed() > b_file_info.last_accessed();

  // When the entries have the same last access time (which happens quite often
  // because Drive server doesn't set the field until an entry is viewed via
  // drive.google.com), we use last modified time as the tie breaker.
  return a_file_info.last_modified() > b_file_info.last_modified();
}

struct MetadataSearchResultComparator {
  bool operator()(const MetadataSearchResult* a,
                  const MetadataSearchResult* b) const {
    return CompareByTimestamp(a->entry, b->entry);
  }
};

// A wrapper of std::priority_queue which deals with pointers of values.
template<typename T, typename Compare>
class ScopedPriorityQueue {
 public:
  ScopedPriorityQueue() {}

  ~ScopedPriorityQueue() {
    while (!empty())
      pop();
  }

  bool empty() const { return queue_.empty(); }

  size_t size() const { return queue_.size(); }

  const T* top() const { return queue_.top(); }

  void push(T* x) { queue_.push(x); }

  void pop() {
    delete queue_.top();
    queue_.pop();
  }

 private:
  std::priority_queue<T*, std::vector<T*>, Compare> queue_;

  DISALLOW_COPY_AND_ASSIGN(ScopedPriorityQueue);
};

// Returns true if |entry| is eligible for the search |options| and should be
// tested for the match with the query.  If
// SEARCH_METADATA_EXCLUDE_HOSTED_DOCUMENTS is requested, the hosted documents
// are skipped. If SEARCH_METADATA_EXCLUDE_DIRECTORIES is requested, the
// directories are skipped. If SEARCH_METADATA_SHARED_WITH_ME is requested, only
// the entries with shared-with-me label will be tested. If
// SEARCH_METADATA_OFFLINE is requested, only hosted documents and cached files
// match with the query. This option can not be used with other options.
bool IsEligibleEntry(const ResourceEntry& entry,
                     internal::FileCache* cache,
                     int options) {
  if ((options & SEARCH_METADATA_EXCLUDE_HOSTED_DOCUMENTS) &&
      entry.file_specific_info().is_hosted_document())
    return false;

  if ((options & SEARCH_METADATA_EXCLUDE_DIRECTORIES) &&
      entry.file_info().is_directory())
    return false;

  if (options & SEARCH_METADATA_SHARED_WITH_ME)
    return entry.shared_with_me();

  if (options & SEARCH_METADATA_OFFLINE) {
    if (entry.file_specific_info().is_hosted_document())
      return true;
    FileCacheEntry cache_entry;
    cache->GetCacheEntry(entry.resource_id(),
                         std::string(),
                         &cache_entry);
    return cache_entry.is_present();
  }

  // Exclude "drive", "drive/root", and "drive/other".
  if (entry.resource_id() == util::kDriveGrandRootSpecialResourceId ||
      entry.parent_resource_id() == util::kDriveGrandRootSpecialResourceId) {
    return false;
  }

  return true;
}

// Used to implement SearchMetadata.
// Adds entry to the result when appropriate.
void MaybeAddEntryToResult(
    ResourceMetadata* resource_metadata,
    FileCache* cache,
    const std::string& query,
    int options,
    size_t at_most_num_matches,
    ScopedPriorityQueue<MetadataSearchResult,
                        MetadataSearchResultComparator>* result_candidates,
    const ResourceEntry& entry) {
  DCHECK_GE(at_most_num_matches, result_candidates->size());

  // If the candidate set is already full, and this |entry| is old, do nothing.
  // We perform this check first in order to avoid the costly find-and-highlight
  // or FilePath lookup as much as possible.
  if (result_candidates->size() == at_most_num_matches &&
      !CompareByTimestamp(entry, result_candidates->top()->entry))
    return;

  // Add |entry| to the result if the entry is eligible for the given
  // |options| and matches the query. The base name of the entry must
  // contain |query| to match the query.
  std::string highlighted;
  if (!IsEligibleEntry(entry, cache, options) ||
      !FindAndHighlight(entry.base_name(), query, &highlighted))
    return;

  base::FilePath path = resource_metadata->GetFilePath(entry.resource_id());
  if (path.empty())
    return;

  // Make space for |entry| when appropriate.
  if (result_candidates->size() == at_most_num_matches)
    result_candidates->pop();
  result_candidates->push(new MetadataSearchResult(path, entry, highlighted));
}

// Implements SearchMetadata().
scoped_ptr<MetadataSearchResultVector> SearchMetadataOnBlockingPool(
    ResourceMetadata* resource_metadata,
    FileCache* cache,
    const std::string& query,
    int options,
    int at_most_num_matches) {
  ScopedPriorityQueue<MetadataSearchResult,
                      MetadataSearchResultComparator> result_candidates;

  // Iterate over entries.
  scoped_ptr<ResourceMetadata::Iterator> it = resource_metadata->GetIterator();
  for (; !it->IsAtEnd(); it->Advance()) {
    MaybeAddEntryToResult(resource_metadata, cache, query, options,
                          at_most_num_matches, &result_candidates, it->Get());
  }

  // Prepare the result.
  scoped_ptr<MetadataSearchResultVector> results(
      new MetadataSearchResultVector);
  for (; !result_candidates.empty(); result_candidates.pop())
    results->push_back(*result_candidates.top());

  // Reverse the order here because |result_candidates| puts the most
  // uninterested candidate at the top.
  std::reverse(results->begin(), results->end());

  return results.Pass();
}
}  // namespace

void SearchMetadata(
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner,
    ResourceMetadata* resource_metadata,
    FileCache* cache,
    const std::string& query,
    int options,
    int at_most_num_matches,
    const SearchMetadataCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_LE(0, at_most_num_matches);
  DCHECK(!callback.is_null());

  // TODO(hashimoto): Report error code from ResourceMetadata::IterateEntries
  // and stop binding FILE_ERROR_OK to |callback|.
  base::PostTaskAndReplyWithResult(
      blocking_task_runner,
      FROM_HERE,
      base::Bind(&SearchMetadataOnBlockingPool,
                 resource_metadata,
                 cache,
                 query,
                 options,
                 at_most_num_matches),
      base::Bind(callback, FILE_ERROR_OK));
}

bool FindAndHighlight(const std::string& text,
                      const std::string& query,
                      std::string* highlighted_text) {
  DCHECK(highlighted_text);
  highlighted_text->clear();

  // For empty query, any filename matches with no highlighted text.
  if (query.empty())
    return true;

  string16 text16 = base::UTF8ToUTF16(text);
  string16 query16 = base::UTF8ToUTF16(query);
  size_t match_start = 0;
  size_t match_length = 0;
  if (!base::i18n::StringSearchIgnoringCaseAndAccents(
      query16, text16, &match_start, &match_length)) {
    return false;
  }
  string16 pre = text16.substr(0, match_start);
  string16 match = text16.substr(match_start, match_length);
  string16 post = text16.substr(match_start + match_length);
  highlighted_text->append(net::EscapeForHTML(UTF16ToUTF8(pre)));
  highlighted_text->append("<b>");
  highlighted_text->append(net::EscapeForHTML(UTF16ToUTF8(match)));
  highlighted_text->append("</b>");
  highlighted_text->append(net::EscapeForHTML(UTF16ToUTF8(post)));
  return true;
}

}  // namespace internal
}  // namespace drive
