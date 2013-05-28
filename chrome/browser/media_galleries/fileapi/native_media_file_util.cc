// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/fileapi/native_media_file_util.h"

#include "base/file_util.h"
#include "base/memory/scoped_generic_obj.h"
#include "base/string_util.h"
#include "chrome/browser/media_galleries/fileapi/filtering_file_enumerator.h"
#include "chrome/browser/media_galleries/fileapi/media_file_system_mount_point_provider.h"
#include "chrome/browser/media_galleries/fileapi/media_path_filter.h"
#include "googleurl/src/gurl.h"
#include "net/base/mime_sniffer.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_operation_context.h"
#include "webkit/browser/fileapi/file_system_task_runners.h"
#include "webkit/browser/fileapi/native_file_util.h"

using base::PlatformFile;
using base::PlatformFileError;
using base::PlatformFileInfo;
using fileapi::FileSystemOperationContext;
using fileapi::FileSystemURL;
using fileapi::NativeFileUtil;

namespace chrome {

namespace {

// Modelled after ScopedFILEClose.
class ScopedPlatformFileClose {
 public:
  void operator()(base::PlatformFile file) const {
    if (file != base::kInvalidPlatformFileValue)
      base::ClosePlatformFile(file);
  }
};

typedef ScopedGenericObj<base::PlatformFile,
                         ScopedPlatformFileClose> ScopedPlatformFile;

// Returns true if the current thread is capable of doing IO.
bool IsOnTaskRunnerThread(fileapi::FileSystemOperationContext* context) {
  return context->file_system_context()->task_runners()->
      media_task_runner()->RunsTasksOnCurrentThread();
}

MediaPathFilter* GetMediaPathFilter(FileSystemOperationContext* context) {
  return context->GetUserValue<MediaPathFilter*>(
          MediaFileSystemMountPointProvider::kMediaPathFilterKey);
}

}  // namespace

NativeMediaFileUtil::NativeMediaFileUtil() {
}

PlatformFileError NativeMediaFileUtil::CreateOrOpen(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    int file_flags,
    PlatformFile* file_handle,
    bool* created) {
  // Only called by NaCl, which should not have access to media file systems.
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

PlatformFileError NativeMediaFileUtil::EnsureFileExists(
    FileSystemOperationContext* context,
    const FileSystemURL& url, bool* created) {
  base::FilePath file_path;
  PlatformFileError error = GetFilteredLocalFilePath(context, url, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::EnsureFileExists(file_path, created);
}

scoped_ptr<fileapi::FileSystemFileUtil::AbstractFileEnumerator>
NativeMediaFileUtil::CreateFileEnumerator(
    FileSystemOperationContext* context,
    const FileSystemURL& root_url) {
  DCHECK(context);
  return make_scoped_ptr(new FilteringFileEnumerator(
      IsolatedFileUtil::CreateFileEnumerator(context, root_url),
      GetMediaPathFilter(context)))
      .PassAs<FileSystemFileUtil::AbstractFileEnumerator>();
}

PlatformFileError NativeMediaFileUtil::Touch(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  base::FilePath file_path;
  PlatformFileError error = GetFilteredLocalFilePathForExistingFileOrDirectory(
      context,
      url,
      // Touch fails for non-existent paths and filtered paths.
      base::PLATFORM_FILE_ERROR_FAILED,
      &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::Touch(file_path, last_access_time, last_modified_time);
}

PlatformFileError NativeMediaFileUtil::Truncate(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    int64 length) {
  base::FilePath file_path;
  PlatformFileError error = GetFilteredLocalFilePathForExistingFileOrDirectory(
      context,
      url,
      // Cannot truncate paths that do not exist, or are filtered.
      base::PLATFORM_FILE_ERROR_NOT_FOUND,
      &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::Truncate(file_path, length);
}

PlatformFileError NativeMediaFileUtil::CopyOrMoveFile(
    FileSystemOperationContext* context,
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    bool copy) {
  base::FilePath src_file_path;
  PlatformFileError error =
      GetFilteredLocalFilePathForExistingFileOrDirectory(
          context, src_url,
          base::PLATFORM_FILE_ERROR_NOT_FOUND,
          &src_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  if (NativeFileUtil::DirectoryExists(src_file_path))
    return base::PLATFORM_FILE_ERROR_NOT_A_FILE;

  base::FilePath dest_file_path;
  error = GetLocalFilePath(context, dest_url, &dest_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  PlatformFileInfo file_info;
  error = NativeFileUtil::GetFileInfo(dest_file_path, &file_info);
  if (error != base::PLATFORM_FILE_OK &&
      error != base::PLATFORM_FILE_ERROR_NOT_FOUND)
    return error;
  if (error == base::PLATFORM_FILE_OK && file_info.is_directory)
    return base::PLATFORM_FILE_ERROR_INVALID_OPERATION;
  if (!GetMediaPathFilter(context)->Match(dest_file_path))
    return base::PLATFORM_FILE_ERROR_SECURITY;

  return NativeFileUtil::CopyOrMoveFile(src_file_path, dest_file_path, copy);
}

PlatformFileError NativeMediaFileUtil::CopyInForeignFile(
    FileSystemOperationContext* context,
    const base::FilePath& src_file_path,
    const FileSystemURL& dest_url) {
  if (src_file_path.empty())
    return base::PLATFORM_FILE_ERROR_INVALID_OPERATION;

  base::FilePath dest_file_path;
  PlatformFileError error =
      GetFilteredLocalFilePath(context, dest_url, &dest_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::CopyOrMoveFile(src_file_path, dest_file_path, true);
}

PlatformFileError NativeMediaFileUtil::DeleteFile(
    FileSystemOperationContext* context,
    const FileSystemURL& url) {
  base::FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, url, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  PlatformFileInfo file_info;
  error = NativeFileUtil::GetFileInfo(file_path, &file_info);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  if (file_info.is_directory)
    return base::PLATFORM_FILE_ERROR_NOT_A_FILE;
  if (!GetMediaPathFilter(context)->Match(file_path))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  return NativeFileUtil::DeleteFile(file_path);
}

PlatformFileError NativeMediaFileUtil::GetFileInfo(
    FileSystemOperationContext* context,
    const FileSystemURL& url,
    PlatformFileInfo* file_info,
    base::FilePath* platform_path) {
  DCHECK(context);
  DCHECK(GetMediaPathFilter(context));
  DCHECK(file_info);
  DCHECK(platform_path);

  base::PlatformFileError error =
      IsolatedFileUtil::GetFileInfo(context, url, file_info, platform_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  if (file_info->is_directory ||
      GetMediaPathFilter(context)->Match(*platform_path)) {
    return base::PLATFORM_FILE_OK;
  }
  return base::PLATFORM_FILE_ERROR_NOT_FOUND;
}

PlatformFileError NativeMediaFileUtil::GetFilteredLocalFilePath(
    FileSystemOperationContext* context,
    const FileSystemURL& file_system_url,
    base::FilePath* local_file_path) {
  base::FilePath file_path;
  PlatformFileError error =
      IsolatedFileUtil::GetLocalFilePath(context, file_system_url, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  if (!GetMediaPathFilter(context)->Match(file_path))
    return base::PLATFORM_FILE_ERROR_SECURITY;

  *local_file_path = file_path;
  return base::PLATFORM_FILE_OK;
}

PlatformFileError
NativeMediaFileUtil::GetFilteredLocalFilePathForExistingFileOrDirectory(
    FileSystemOperationContext* context,
    const FileSystemURL& file_system_url,
    PlatformFileError failure_error,
    base::FilePath* local_file_path) {
  base::FilePath file_path;
  PlatformFileError error =
      GetLocalFilePath(context, file_system_url, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  if (!file_util::PathExists(file_path))
    return failure_error;
  PlatformFileInfo file_info;
  if (!file_util::GetFileInfo(file_path, &file_info))
    return base::PLATFORM_FILE_ERROR_FAILED;

  if (!file_info.is_directory &&
      !GetMediaPathFilter(context)->Match(file_path)) {
    return failure_error;
  }

  *local_file_path = file_path;
  return base::PLATFORM_FILE_OK;
}

webkit_blob::ScopedFile NativeMediaFileUtil::CreateSnapshotFile(
    fileapi::FileSystemOperationContext* context,
    const fileapi::FileSystemURL& url,
    base::PlatformFileError* error,
    base::PlatformFileInfo* file_info,
    base::FilePath* platform_path) {
  DCHECK(IsOnTaskRunnerThread(context));
  webkit_blob::ScopedFile file;
  file = IsolatedFileUtil::CreateSnapshotFile(
      context, url, error, file_info, platform_path);
  if (*error != base::PLATFORM_FILE_OK)
    return file.Pass();
  *error = IsMediaFile(*platform_path);
  if (*error == base::PLATFORM_FILE_OK)
    return file.Pass();
  return webkit_blob::ScopedFile();
}

// static
base::PlatformFileError NativeMediaFileUtil::IsMediaFile(
    const base::FilePath& path) {
  base::PlatformFile file_handle;
  const int flags = base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ;
  base::PlatformFileError error =
      NativeFileUtil::CreateOrOpen(path, flags, &file_handle, NULL);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  ScopedPlatformFile scoped_platform_file(file_handle);
  char buffer[net::kMaxBytesToSniff];

  // Read as much as net::SniffMimeTypeFromLocalData() will bother looking at.
  int64 len =
      base::ReadPlatformFile(file_handle, 0, buffer, net::kMaxBytesToSniff);
  if (len < 0)
    return base::PLATFORM_FILE_ERROR_FAILED;
  if (len == 0)
    return base::PLATFORM_FILE_ERROR_SECURITY;

  std::string mime_type;
  if (!net::SniffMimeTypeFromLocalData(buffer, len, &mime_type))
    return base::PLATFORM_FILE_ERROR_SECURITY;

  if (StartsWithASCII(mime_type, "image/", true) ||
      StartsWithASCII(mime_type, "audio/", true) ||
      StartsWithASCII(mime_type, "video/", true) ||
      mime_type == "application/x-shockwave-flash") {
    return base::PLATFORM_FILE_OK;
  }
  return base::PLATFORM_FILE_ERROR_SECURITY;
}

}  // namespace chrome
