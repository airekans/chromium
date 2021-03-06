// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/strings/sys_string_conversions.h"
#include "base/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/browser/fileapi/async_file_test_helper.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_file_util.h"
#include "webkit/browser/fileapi/file_system_operation_context.h"
#include "webkit/browser/fileapi/local_file_util.h"
#include "webkit/browser/fileapi/mock_file_system_context.h"
#include "webkit/browser/fileapi/native_file_util.h"
#include "webkit/common/fileapi/file_system_types.h"

namespace fileapi {

namespace {

const GURL kOrigin("http://foo/");
const FileSystemType kFileSystemType = kFileSystemTypeTest;

}  // namespace

class LocalFileUtilTest : public testing::Test {
 public:
  LocalFileUtilTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(data_dir_.CreateUniqueTempDir());
    file_system_context_ = CreateFileSystemContextForTesting(
        NULL, data_dir_.path());
  }

  virtual void TearDown() {
    file_system_context_ = NULL;
    base::MessageLoop::current()->RunUntilIdle();
  }

 protected:
  FileSystemOperationContext* NewContext() {
    FileSystemOperationContext* context =
        new FileSystemOperationContext(file_system_context_.get());
    context->set_update_observers(
        *file_system_context_->GetUpdateObservers(kFileSystemType));
    context->set_root_path(data_dir_.path());
    return context;
  }

  LocalFileUtil* file_util() {
    return static_cast<LocalFileUtil*>(
        file_system_context_->GetFileUtil(kFileSystemType));
  }

  FileSystemURL CreateURL(const std::string& file_name) {
    return file_system_context_->CreateCrackedFileSystemURL(
        kOrigin, kFileSystemType, base::FilePath().FromUTF8Unsafe(file_name));
  }

  base::FilePath LocalPath(const char *file_name) {
    base::FilePath path;
    scoped_ptr<FileSystemOperationContext> context(NewContext());
    file_util()->GetLocalFilePath(context.get(), CreateURL(file_name), &path);
    return path;
  }

  bool FileExists(const char *file_name) {
    return file_util::PathExists(LocalPath(file_name)) &&
        !file_util::DirectoryExists(LocalPath(file_name));
  }

  bool DirectoryExists(const char *file_name) {
    return file_util::DirectoryExists(LocalPath(file_name));
  }

  int64 GetSize(const char *file_name) {
    base::PlatformFileInfo info;
    file_util::GetFileInfo(LocalPath(file_name), &info);
    return info.size;
  }

  base::PlatformFileError CreateFile(const char* file_name,
                                     base::PlatformFile* file_handle,
                                     bool* created) {
    int file_flags = base::PLATFORM_FILE_CREATE |
        base::PLATFORM_FILE_WRITE | base::PLATFORM_FILE_ASYNC;

    scoped_ptr<FileSystemOperationContext> context(NewContext());
    return file_util()->CreateOrOpen(
        context.get(),
        CreateURL(file_name),
        file_flags, file_handle, created);
  }

  base::PlatformFileError EnsureFileExists(const char* file_name,
      bool* created) {
    scoped_ptr<FileSystemOperationContext> context(NewContext());
    return file_util()->EnsureFileExists(
        context.get(),
        CreateURL(file_name), created);
  }

  FileSystemContext* file_system_context() {
    return file_system_context_.get();
  }

 private:
  base::MessageLoop message_loop_;
  scoped_refptr<FileSystemContext> file_system_context_;
  base::ScopedTempDir data_dir_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileUtilTest);
};

TEST_F(LocalFileUtilTest, CreateAndClose) {
  const char *file_name = "test_file";
  base::PlatformFile file_handle;
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            CreateFile(file_name, &file_handle, &created));
  ASSERT_TRUE(created);

  EXPECT_TRUE(FileExists(file_name));
  EXPECT_EQ(0, GetSize(file_name));

  scoped_ptr<FileSystemOperationContext> context(NewContext());
  EXPECT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Close(context.get(), file_handle));
}

// file_util::CreateSymbolicLink is only supported on POSIX.
#if defined(OS_POSIX)
TEST_F(LocalFileUtilTest, CreateFailForSymlink) {
  // Create symlink target file.
  const char *target_name = "symlink_target";
  base::PlatformFile target_handle;
  bool symlink_target_created = false;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            CreateFile(target_name, &target_handle, &symlink_target_created));
  ASSERT_TRUE(symlink_target_created);
  base::FilePath target_path = LocalPath(target_name);

  // Create symlink where target must be real file.
  const char *symlink_name = "symlink_file";
  base::FilePath symlink_path = LocalPath(symlink_name);
  ASSERT_TRUE(file_util::CreateSymbolicLink(target_path, symlink_path));
  ASSERT_TRUE(FileExists(symlink_name));

  // Try to open the symlink file which should fail.
  scoped_ptr<FileSystemOperationContext> context(NewContext());
  FileSystemURL url = CreateURL(symlink_name);
  int file_flags = base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ;
  base::PlatformFile file_handle;
  bool created = false;
  EXPECT_EQ(base::PLATFORM_FILE_ERROR_NOT_FOUND, file_util()->CreateOrOpen(
      context.get(), url, file_flags, &file_handle, &created));
  EXPECT_FALSE(created);
}
#endif

TEST_F(LocalFileUtilTest, EnsureFileExists) {
  const char *file_name = "foobar";
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(file_name, &created));
  ASSERT_TRUE(created);

  EXPECT_TRUE(FileExists(file_name));
  EXPECT_EQ(0, GetSize(file_name));

  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(file_name, &created));
  EXPECT_FALSE(created);
}

TEST_F(LocalFileUtilTest, TouchFile) {
  const char *file_name = "test_file";
  base::PlatformFile file_handle;
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            CreateFile(file_name, &file_handle, &created));
  ASSERT_TRUE(created);

  scoped_ptr<FileSystemOperationContext> context(NewContext());

  base::PlatformFileInfo info;
  ASSERT_TRUE(file_util::GetFileInfo(LocalPath(file_name), &info));
  const base::Time new_accessed =
      info.last_accessed + base::TimeDelta::FromHours(10);
  const base::Time new_modified =
      info.last_modified + base::TimeDelta::FromHours(5);

  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_util()->Touch(context.get(), CreateURL(file_name),
                              new_accessed, new_modified));

  ASSERT_TRUE(file_util::GetFileInfo(LocalPath(file_name), &info));
  EXPECT_EQ(new_accessed, info.last_accessed);
  EXPECT_EQ(new_modified, info.last_modified);

  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_util()->Close(context.get(), file_handle));
}

TEST_F(LocalFileUtilTest, TouchDirectory) {
  const char *dir_name = "test_dir";
  scoped_ptr<FileSystemOperationContext> context(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            file_util()->CreateDirectory(context.get(),
                                        CreateURL(dir_name),
                                        false /* exclusive */,
                                        false /* recursive */));

  base::PlatformFileInfo info;
  ASSERT_TRUE(file_util::GetFileInfo(LocalPath(dir_name), &info));
  const base::Time new_accessed =
      info.last_accessed + base::TimeDelta::FromHours(10);
  const base::Time new_modified =
      info.last_modified + base::TimeDelta::FromHours(5);

  EXPECT_EQ(base::PLATFORM_FILE_OK,
            file_util()->Touch(context.get(), CreateURL(dir_name),
                              new_accessed, new_modified));

  ASSERT_TRUE(file_util::GetFileInfo(LocalPath(dir_name), &info));
  EXPECT_EQ(new_accessed, info.last_accessed);
  EXPECT_EQ(new_modified, info.last_modified);
}

TEST_F(LocalFileUtilTest, Truncate) {
  const char *file_name = "truncated";
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(file_name, &created));
  ASSERT_TRUE(created);

  scoped_ptr<FileSystemOperationContext> context;

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Truncate(context.get(), CreateURL(file_name), 1020));

  EXPECT_TRUE(FileExists(file_name));
  EXPECT_EQ(1020, GetSize(file_name));
}

TEST_F(LocalFileUtilTest, CopyFile) {
  const char *from_file = "fromfile";
  const char *to_file1 = "tofile1";
  const char *to_file2 = "tofile2";
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(from_file, &created));
  ASSERT_TRUE(created);

  scoped_ptr<FileSystemOperationContext> context;
  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Truncate(context.get(), CreateURL(from_file), 1020));

  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));

  ASSERT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::Copy(file_system_context(),
                                      CreateURL(from_file),
                                      CreateURL(to_file1)));

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::Copy(file_system_context(),
                                      CreateURL(from_file),
                                      CreateURL(to_file2)));

  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));
  EXPECT_TRUE(FileExists(to_file1));
  EXPECT_EQ(1020, GetSize(to_file1));
  EXPECT_TRUE(FileExists(to_file2));
  EXPECT_EQ(1020, GetSize(to_file2));
}

TEST_F(LocalFileUtilTest, CopyDirectory) {
  const char *from_dir = "fromdir";
  const char *from_file = "fromdir/fromfile";
  const char *to_dir = "todir";
  const char *to_file = "todir/fromfile";
  bool created;
  scoped_ptr<FileSystemOperationContext> context;

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->CreateDirectory(context.get(), CreateURL(from_dir),
                                   false, false));
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(from_file, &created));
  ASSERT_TRUE(created);

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Truncate(context.get(), CreateURL(from_file), 1020));

  EXPECT_TRUE(DirectoryExists(from_dir));
  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));
  EXPECT_FALSE(DirectoryExists(to_dir));

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::Copy(file_system_context(),
                                      CreateURL(from_dir), CreateURL(to_dir)));

  EXPECT_TRUE(DirectoryExists(from_dir));
  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));
  EXPECT_TRUE(DirectoryExists(to_dir));
  EXPECT_TRUE(FileExists(to_file));
  EXPECT_EQ(1020, GetSize(to_file));
}

TEST_F(LocalFileUtilTest, MoveFile) {
  const char *from_file = "fromfile";
  const char *to_file = "tofile";
  bool created;
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(from_file, &created));
  ASSERT_TRUE(created);
  scoped_ptr<FileSystemOperationContext> context;

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Truncate(context.get(), CreateURL(from_file), 1020));

  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::Move(file_system_context(),
                                      CreateURL(from_file),
                                      CreateURL(to_file)));

  EXPECT_FALSE(FileExists(from_file));
  EXPECT_TRUE(FileExists(to_file));
  EXPECT_EQ(1020, GetSize(to_file));
}

TEST_F(LocalFileUtilTest, MoveDirectory) {
  const char *from_dir = "fromdir";
  const char *from_file = "fromdir/fromfile";
  const char *to_dir = "todir";
  const char *to_file = "todir/fromfile";
  bool created;
  scoped_ptr<FileSystemOperationContext> context;

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->CreateDirectory(context.get(), CreateURL(from_dir),
                                   false, false));
  ASSERT_EQ(base::PLATFORM_FILE_OK, EnsureFileExists(from_file, &created));
  ASSERT_TRUE(created);

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
      file_util()->Truncate(context.get(), CreateURL(from_file), 1020));

  EXPECT_TRUE(DirectoryExists(from_dir));
  EXPECT_TRUE(FileExists(from_file));
  EXPECT_EQ(1020, GetSize(from_file));
  EXPECT_FALSE(DirectoryExists(to_dir));

  context.reset(NewContext());
  ASSERT_EQ(base::PLATFORM_FILE_OK,
            AsyncFileTestHelper::Move(file_system_context(),
                                      CreateURL(from_dir),
                                      CreateURL(to_dir)));

  EXPECT_FALSE(DirectoryExists(from_dir));
  EXPECT_TRUE(DirectoryExists(to_dir));
  EXPECT_TRUE(FileExists(to_file));
  EXPECT_EQ(1020, GetSize(to_file));
}

}  // namespace fileapi
