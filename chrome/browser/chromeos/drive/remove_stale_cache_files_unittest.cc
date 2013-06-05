// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/remove_stale_cache_files.h"
#include "chrome/browser/chromeos/drive/resource_metadata.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/test_util.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace internal {
namespace {

const int64 kLotsOfSpace = kMinFreeSpace * 10;

}  // namespace

class RemoveStaleCacheFilesTest : public testing::Test {
 protected:
  RemoveStaleCacheFilesTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_) {
  }

  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    cache_.reset(new FileCache(temp_dir_.path(),
                               message_loop_.message_loop_proxy(),
                               fake_free_disk_space_getter_.get()));

    resource_metadata_.reset(new ResourceMetadata(
        cache_->GetCacheDirectoryPath(FileCache::CACHE_TYPE_META),
        message_loop_.message_loop_proxy()));

    bool success = false;
    cache_->RequestInitialize(
        google_apis::test_util::CreateCopyResultCallback(&success));
    message_loop_.RunUntilIdle();
    ASSERT_TRUE(success);

    FileError error = FILE_ERROR_FAILED;
    resource_metadata_->Initialize(
        google_apis::test_util::CreateCopyResultCallback(&error));
    message_loop_.RunUntilIdle();
    ASSERT_EQ(FILE_ERROR_OK, error);
  }

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  base::ScopedTempDir temp_dir_;

  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<ResourceMetadata, test_util::DestroyHelperForTests>
      resource_metadata_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
};

TEST_F(RemoveStaleCacheFilesTest, RemoveStaleCacheFiles) {
  base::FilePath dummy_file =
      google_apis::test_util::GetTestFilePath("chromeos/gdata/root_feed.json");
  std::string resource_id("pdf:1a2b3c");
  std::string md5("abcdef0123456789");

  // Create a stale cache file.
  EXPECT_EQ(FILE_ERROR_OK,
            cache_->Store(resource_id, md5, dummy_file,
                          FileCache::FILE_OPERATION_COPY));

  // Verify that the cache entry exists.
  FileCacheEntry cache_entry;
  EXPECT_TRUE(cache_->GetCacheEntry(resource_id, md5, &cache_entry));

  ResourceEntry entry;
  EXPECT_EQ(FILE_ERROR_NOT_FOUND,
            resource_metadata_->GetResourceEntryById(resource_id, &entry));

  // Remove stale cache files.
  RemoveStaleCacheFiles(cache_.get(), resource_metadata_.get());

  // Verify that the cache entry is deleted.
  EXPECT_FALSE(cache_->GetCacheEntry(resource_id, md5, &cache_entry));
}

}  // namespace internal
}  // namespace drive
