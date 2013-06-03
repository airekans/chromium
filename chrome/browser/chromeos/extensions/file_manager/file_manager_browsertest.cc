// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Browser test for basic Chrome OS file manager functionality:
//  - The file list is updated when a file is added externally to the Downloads
//    folder.
//  - Selecting a file and copy-pasting it with the keyboard copies the file.
//  - Selecting a file and pressing delete deletes it.

#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/time.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/extensions/file_manager/drive_test_util.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/google_apis/fake_drive_service.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/browser_context.h"
#include "webkit/browser/fileapi/external_mount_points.h"

namespace {

// Test suffixes appended to the Javascript tests' names.
const char kDownloadsVolume[] = "Downloads";

enum EntryType {
  FILE,
  DIRECTORY,
};

enum SharedOption {
  NONE,
  SHARED,
};

enum GuestMode {
  NOT_IN_GUEST_MODE,
  IN_GUEST_MODE
};

// This global operator is used from Google Test to format error messages.
std::ostream& operator<<(std::ostream& os, const GuestMode& guest_mode) {
  return os << (guest_mode == IN_GUEST_MODE ?
                "IN_GUEST_MODE" : "NOT_IN_GUEST_MODE");
}

struct TestEntryInfo {
  EntryType type;
  const char* source_file_name;  // Source file name to be used as a prototype.
  const char* target_name;  // Target file or directory name.
  const char* mime_type;
  SharedOption shared_option;
  const char* last_modified_time_as_string;
};

TestEntryInfo kTestEntrySetCommon[] = {
  { FILE, "text.txt", "hello.txt", "text/plain", NONE, "4 Sep 1998 12:34:56" },
  { FILE, "image.png", "My Desktop Background.png", "text/plain", NONE,
    "18 Jan 2038 01:02:03" },
  { FILE, "music.ogg", "Beautiful Song.ogg", "text/plain", NONE,
    "12 Nov 2086 12:00:00" },
  { FILE, "video.ogv", "world.ogv", "text/plain", NONE,
    "4 July 2012 10:35:00" },
  { DIRECTORY, "", "photos", NULL, NONE, "1 Jan 1980 23:59:59" },
  { DIRECTORY, "", ".warez", NULL, NONE, "26 Oct 1985 13:39" }
};

TestEntryInfo kTestEntrySetDriveOnly[] = {
  { FILE, "", "Test Document", "application/vnd.google-apps.document", NONE,
    "10 Apr 2013 16:20:00" },
  { FILE, "", "Test Shared Document", "application/vnd.google-apps.document",
    SHARED, "20 Mar 2013 22:40:00" }
};

// The base class of volumes for test.
// Sub-classes of this class are used by test cases and provide operations such
// as creating files for each type of test volume.
class TestVolume {
 public:
  virtual ~TestVolume() {}

  // Creates an entry with given information.
  virtual void CreateEntry(const TestEntryInfo& entry) = 0;
};

// The local volume class for test.
// This class provides the operations for a test volume that simulates local
// drive.
class LocalTestVolume : public TestVolume {
 public:
  explicit LocalTestVolume(const std::string& mount_name)
      : mount_name_(mount_name) {
  }

  // Adds this volume to the file system as a local volume. Returns true on
  // success.
  bool Mount(Profile* profile) {
    if (local_path_.empty()) {
      if (!tmp_dir_.CreateUniqueTempDir())
        return false;
      local_path_ = tmp_dir_.path().Append(mount_name_);
    }
    fileapi::ExternalMountPoints* const mount_points =
        content::BrowserContext::GetMountPoints(profile);
    mount_points->RevokeFileSystem(mount_name_);
    if (!mount_points->RegisterFileSystem(
            mount_name_,
            fileapi::kFileSystemTypeNativeLocal,
            local_path_)) {
      return false;
    }
    if (!file_util::CreateDirectory(local_path_))
      return false;
    return true;
  }

  virtual void CreateEntry(const TestEntryInfo& entry) OVERRIDE {
    if (entry.type == DIRECTORY) {
      CreateDirectory(entry.target_name ,
                      entry.last_modified_time_as_string);
    } else if (entry.type == FILE) {
      CreateFile(entry.source_file_name, entry.target_name,
                 entry.last_modified_time_as_string);
    } else {
      NOTREACHED();
    }
  }

  void CreateFile(const std::string& source_file_name,
                  const std::string& target_name,
                  const std::string& modification_time) {
    std::string content_data;
    base::FilePath test_file_path =
        google_apis::test_util::GetTestFilePath("chromeos/file_manager").
            AppendASCII(source_file_name);

    base::FilePath path = local_path_.AppendASCII(target_name);
    ASSERT_TRUE(file_util::PathExists(test_file_path))
        << "Test file doesn't exist: " << test_file_path.value();
    ASSERT_TRUE(file_util::CopyFile(test_file_path, path));
    ASSERT_TRUE(file_util::PathExists(path))
        << "Copying to: " << path.value() << " failed.";
    base::Time time;
    ASSERT_TRUE(base::Time::FromString(modification_time.c_str(), &time));
    ASSERT_TRUE(file_util::SetLastModifiedTime(path, time));
  }

  void CreateDirectory(const std::string& target_name,
                       const std::string& modification_time) {
    base::FilePath path = local_path_.AppendASCII(target_name);
    ASSERT_TRUE(file_util::CreateDirectory(path)) <<
        "Failed to create a directory: " << target_name;
    base::Time time;
    ASSERT_TRUE(base::Time::FromString(modification_time.c_str(), &time));
    ASSERT_TRUE(file_util::SetLastModifiedTime(path, time));
  }

 private:
  std::string mount_name_;
  base::FilePath local_path_;
  base::ScopedTempDir tmp_dir_;
};

// The drive volume class for test.
// This class provides the operations for a test volume that simulates Google
// drive.
class DriveTestVolume : public TestVolume {
 public:
  DriveTestVolume() : fake_drive_service_(NULL),
                      integration_service_(NULL) {
  }

  // Send request to add this volume to the file system as Google drive.
  // This method must be calld at SetUp method of FileManagerBrowserTestBase.
  // Returns true on success.
  bool SetUp() {
    if (!test_cache_root_.CreateUniqueTempDir())
      return false;
    drive::DriveIntegrationServiceFactory::SetFactoryForTest(
        base::Bind(&DriveTestVolume::CreateDriveIntegrationService,
                   base::Unretained(this)));
    return true;
  }

  virtual void CreateEntry(const TestEntryInfo& entry) OVERRIDE {
    if (entry.type == DIRECTORY) {
      CreateDirectory(entry.target_name,
                      entry.last_modified_time_as_string);
    } else if (entry.type == FILE) {
      CreateFile(entry.source_file_name,
                 entry.target_name,
                 entry.mime_type,
                 entry.shared_option == SHARED,
                 entry.last_modified_time_as_string);
    } else {
      NOTREACHED();
    }
  }

  // Creates an empty directory with the given |name| and |modification_time|.
  void CreateDirectory(const std::string& name,
                       const std::string& modification_time) {
    google_apis::GDataErrorCode error = google_apis::GDATA_OTHER_ERROR;
    scoped_ptr<google_apis::ResourceEntry> resource_entry;
    fake_drive_service_->AddNewDirectory(
        fake_drive_service_->GetRootResourceId(),
        name,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_TRUE(error == google_apis::HTTP_CREATED);
    ASSERT_TRUE(resource_entry);

    base::Time time;
    ASSERT_TRUE(base::Time::FromString(modification_time.c_str(), &time));
    fake_drive_service_->SetLastModifiedTime(
        resource_entry->resource_id(),
        time,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_TRUE(error == google_apis::HTTP_SUCCESS);
    ASSERT_TRUE(resource_entry);
    CheckForUpdates();
  }

  // Creates a test file with the given spec.
  // Serves |test_file_name| file. Pass an empty string for an empty file.
  void CreateFile(const std::string& source_file_name,
                  const std::string& target_file_name,
                  const std::string& mime_type,
                  bool shared_with_me,
                  const std::string& modification_time) {
    google_apis::GDataErrorCode error = google_apis::GDATA_OTHER_ERROR;

    std::string content_data;
    if (!source_file_name.empty()) {
      base::FilePath source_file_path =
          google_apis::test_util::GetTestFilePath("chromeos/file_manager").
              AppendASCII(source_file_name);
      ASSERT_TRUE(file_util::ReadFileToString(source_file_path, &content_data));
    }

    scoped_ptr<google_apis::ResourceEntry> resource_entry;
    fake_drive_service_->AddNewFile(
        mime_type,
        content_data,
        fake_drive_service_->GetRootResourceId(),
        target_file_name,
        shared_with_me,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_CREATED, error);
    ASSERT_TRUE(resource_entry);

    base::Time time;
    ASSERT_TRUE(base::Time::FromString(modification_time.c_str(), &time));
    fake_drive_service_->SetLastModifiedTime(
        resource_entry->resource_id(),
        time,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_SUCCESS, error);
    ASSERT_TRUE(resource_entry);

    CheckForUpdates();
  }

  // Notifies FileSystem that the contents in FakeDriveService are
  // changed, hence the new contents should be fetched.
  void CheckForUpdates() {
    if (integration_service_ && integration_service_->file_system()) {
      integration_service_->file_system()->CheckForUpdates();
    }
  }

  drive::DriveIntegrationService* CreateDriveIntegrationService(
      Profile* profile) {
    fake_drive_service_ = new google_apis::FakeDriveService;
    fake_drive_service_->LoadResourceListForWapi(
        "chromeos/gdata/empty_feed.json");
    fake_drive_service_->LoadAccountMetadataForWapi(
        "chromeos/gdata/account_metadata.json");
    fake_drive_service_->LoadAppListForDriveApi("chromeos/drive/applist.json");
    integration_service_ = new drive::DriveIntegrationService(
        profile,
        fake_drive_service_,
        test_cache_root_.path(),
        NULL);
    return integration_service_;
  }

 private:
  base::ScopedTempDir test_cache_root_;
  google_apis::FakeDriveService* fake_drive_service_;
  drive::DriveIntegrationService* integration_service_;
};

// Parameter of FileManagerBrowserTestBase.
// The second value is the case name of javascript.
typedef std::tr1::tuple<GuestMode, const char*> TestParameter;

// The base test class.
class FileManagerBrowserTestBase :
      public ExtensionApiTest,
      public ::testing::WithParamInterface<TestParameter> {
 protected:
  FileManagerBrowserTestBase() :
      local_volume_(new LocalTestVolume(kDownloadsVolume)),
      drive_volume_(std::tr1::get<0>(GetParam()) != IN_GUEST_MODE ?
                    new DriveTestVolume() : NULL),
      guest_mode_(std::tr1::get<0>(GetParam())) {}

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE;

  virtual void SetUpOnMainThread() OVERRIDE;

  // Adds an incognito and guest-mode flags for tests in the guest mode.
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE;

  // Loads our testing extension and sends it a string identifying the current
  // test.
  void StartTest();

  // Creates test files and directories.
  void CreateTestEntries(TestVolume* volume,
                         const TestEntryInfo* entries,
                         size_t num_entries);

 protected:
  const scoped_ptr<LocalTestVolume> local_volume_;
  const scoped_ptr<DriveTestVolume> drive_volume_;

 private:
  GuestMode guest_mode_;
};

void FileManagerBrowserTestBase::SetUpInProcessBrowserTestFixture() {
  ExtensionApiTest::SetUpInProcessBrowserTestFixture();
  extensions::ComponentLoader::EnableBackgroundExtensionsForTesting();
  if (drive_volume_)
    ASSERT_TRUE(drive_volume_->SetUp());
}

void FileManagerBrowserTestBase::SetUpOnMainThread() {
  ExtensionApiTest::SetUpOnMainThread();
  ASSERT_TRUE(local_volume_->Mount(browser()->profile()));
  CreateTestEntries(local_volume_.get(),
                    kTestEntrySetCommon,
                    arraysize(kTestEntrySetCommon));
  if (drive_volume_) {
    CreateTestEntries(drive_volume_.get(),
                      kTestEntrySetCommon,
                      arraysize(kTestEntrySetCommon));
    // For testing Drive, create more entries with Drive specific attributes.
    // TODO(haruki): Add a case for an entry cached by DriveCache.
    CreateTestEntries(drive_volume_.get(),
                      kTestEntrySetDriveOnly,
                      arraysize(kTestEntrySetDriveOnly));
    drive_test_util::WaitUntilDriveMountPointIsAdded(browser()->profile());
  }
}

void FileManagerBrowserTestBase::SetUpCommandLine(CommandLine* command_line) {
  if (guest_mode_ == IN_GUEST_MODE) {
    command_line->AppendSwitch(chromeos::switches::kGuestSession);
    command_line->AppendSwitchNative(chromeos::switches::kLoginUser, "");
    command_line->AppendSwitch(switches::kIncognito);
  }
  ExtensionApiTest::SetUpCommandLine(command_line);
}

void FileManagerBrowserTestBase::StartTest() {
  base::FilePath path = test_data_dir_.AppendASCII("file_manager_browsertest");
  const extensions::Extension* extension = LoadExtensionAsComponent(path);
  ASSERT_TRUE(extension);

  bool in_guest_mode = guest_mode_ == IN_GUEST_MODE;
  ExtensionTestMessageListener listener(
      in_guest_mode ? "which test guest" : "which test non-guest", true);
  ASSERT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(std::tr1::get<1>(GetParam()));
}

void FileManagerBrowserTestBase::CreateTestEntries(
    TestVolume* volume, const TestEntryInfo* entries, size_t num_entries) {
  for (size_t i = 0; i < num_entries; ++i) {
    volume->CreateEntry(entries[i]);
  }
}

class FileManagerBrowserFileDisplayTest : public FileManagerBrowserTestBase {};

IN_PROC_BROWSER_TEST_P(FileManagerBrowserFileDisplayTest, Test) {
  ResultCatcher catcher;
  ASSERT_NO_FATAL_FAILURE(StartTest());

  ExtensionTestMessageListener listener("initial check done", true);
  ASSERT_TRUE(listener.WaitUntilSatisfied());
  const TestEntryInfo entry = {
    FILE,
    "music.ogg",  // Prototype file name.
    "newly added file.ogg",  // Target file name.
    "audio/ogg",
    NONE,
    "4 Sep 1998 00:00:00"
  };
  if (drive_volume_)
    drive_volume_->CreateEntry(entry);
  local_volume_->CreateEntry(entry);
  listener.Reply("file added");

  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

INSTANTIATE_TEST_CASE_P(
    AllTests,
    FileManagerBrowserFileDisplayTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "fileDisplayDownloads"),
                      TestParameter(IN_GUEST_MODE, "fileDisplayDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "fileDisplayDrive")));

// A test class that just executes JavaScript unit test.
class FileManagerBrowserSimpleTest : public FileManagerBrowserTestBase {};

IN_PROC_BROWSER_TEST_P(FileManagerBrowserSimpleTest, Test) {
  ResultCatcher catcher;
  ASSERT_NO_FATAL_FAILURE(StartTest());
  ASSERT_TRUE(catcher.GetNextResult()) << catcher.message();
}

INSTANTIATE_TEST_CASE_P(
    OpenSpecialTypes,
    FileManagerBrowserSimpleTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "videoOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "videoOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "videoOpenDrive"),
                      TestParameter(IN_GUEST_MODE, "audioOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "audioOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "audioOpenDrive"),
                      TestParameter(IN_GUEST_MODE, "galleryOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "galleryOpenDownloads")));
                      // Disabled temporarily since fails on Linux Chromium OS
                      // ASAN Tests (2).  TODO(mtomasz): crbug.com/243611.
                      // TestParameter(NOT_IN_GUEST_MODE, "galleryOpenDrive")));

INSTANTIATE_TEST_CASE_P(
    KeyboardOpeartions,
    FileManagerBrowserSimpleTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "keyboardDeleteDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "keyboardDeleteDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardDeleteDrive"),
                      TestParameter(IN_GUEST_MODE, "keyboardCopyDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardCopyDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardCopyDrive")));

INSTANTIATE_TEST_CASE_P(
    DriveSpecific,
    FileManagerBrowserSimpleTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "openSidebarRecent"),
                      TestParameter(NOT_IN_GUEST_MODE, "openSidebarOffline"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "openSidebarSharedWithMe"),
                      TestParameter(NOT_IN_GUEST_MODE, "autocomplete")));

INSTANTIATE_TEST_CASE_P(
    Transfer,
    FileManagerBrowserSimpleTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromDriveToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromDownloadsToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromSharedToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromSharedToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromRecentToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromRecentToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromOfflineToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromOfflineToDrive")));

}  // namespace
