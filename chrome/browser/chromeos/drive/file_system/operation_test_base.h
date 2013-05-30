// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_OPERATION_TEST_BASE_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_OPERATION_TEST_BASE_H_

#include <set>

#include "base/files/scoped_temp_dir.h"
#include "base/message_loop.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_system/operation_observer.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

class TestingProfile;

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace google_apis {
class FakeDriveService;
}  // namespace googl_apis

namespace drive {

class FakeFreeDiskSpaceGetter;
class JobScheduler;

namespace internal {
class FileCache;
class ResourceMetadata;
}  // namespace internal

namespace file_system {

// Base fixture class for testing Drive file system operations. It sets up the
// basic set of Drive internal classes (ResourceMetadata, Cache, etc) on top of
// FakeDriveService for testing.
class OperationTestBase : public testing::Test {
 protected:
  // OperationObserver that records all the events.
  class LoggingObserver : public OperationObserver {
   public:
    LoggingObserver();
    ~LoggingObserver();

    // OperationObserver overrides.
    virtual void OnDirectoryChangedByOperation(
        const base::FilePath& path) OVERRIDE;

    // Gets the set of changed paths.
    const std::set<base::FilePath>& get_changed_paths() {
      return changed_paths_;
    }

   private:
    std::set<base::FilePath> changed_paths_;
  };

  OperationTestBase();
  virtual ~OperationTestBase();

  // testing::Test overrides.
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  // Returns the path of the temporary directory for putting test files.
  base::FilePath temp_dir() const { return temp_dir_.path(); }

  // Synchronously gets the resource entry corresponding to the path from local
  // ResourceMetadta.
  FileError GetLocalResourceEntry(const base::FilePath& path,
                                  ResourceEntry* entry);

  // Accessors for the components.
  google_apis::FakeDriveService* fake_service() {
    return fake_drive_service_.get();
  }
  LoggingObserver* observer() { return &observer_; }
  JobScheduler* scheduler() { return scheduler_.get(); }
  base::SequencedTaskRunner* blocking_task_runner() {
    return blocking_task_runner_;
  }
  internal::ResourceMetadata* metadata() { return metadata_.get(); }
  FakeFreeDiskSpaceGetter* fake_free_disk_space_getter() {
    return fake_free_disk_space_getter_.get();
  }
  internal::FileCache* cache() { return cache_.get(); }

 private:
  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  scoped_ptr<TestingProfile> profile_;
  base::ScopedTempDir temp_dir_;

  LoggingObserver observer_;
  scoped_ptr<google_apis::FakeDriveService> fake_drive_service_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
      metadata_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
  scoped_ptr<internal::FileCache, test_util::DestroyHelperForTests> cache_;
};

}  // namespace file_system
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_FILE_SYSTEM_OPERATION_TEST_BASE_H_
