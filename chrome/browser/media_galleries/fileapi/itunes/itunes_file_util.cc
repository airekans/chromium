// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/fileapi/itunes/itunes_file_util.h"

#include "webkit/browser/fileapi/file_system_file_util.h"
#include "webkit/browser/fileapi/file_system_operation_context.h"
#include "webkit/browser/fileapi/file_system_url.h"
#include "webkit/common/fileapi/file_system_util.h"

namespace itunes {

ItunesFileUtil::ItunesFileUtil() {}

ItunesFileUtil::~ItunesFileUtil() {}

base::PlatformFileError ItunesFileUtil::GetFileInfoSync(
    fileapi::FileSystemOperationContext* context,
    const fileapi::FileSystemURL& url,
    base::PlatformFileInfo* file_info,
    base::FilePath* platform_path) {
  return base::PLATFORM_FILE_ERROR_NOT_FOUND;
}

base::PlatformFileError ItunesFileUtil::ReadDirectorySync(
    fileapi::FileSystemOperationContext* context,
    const fileapi::FileSystemURL& url,
    EntryList* file_list) {
  DCHECK(context);
  DCHECK(file_list);
  DCHECK(file_list->empty());

  return base::PLATFORM_FILE_OK;
}

base::PlatformFileError ItunesFileUtil::GetLocalFilePath(
    fileapi::FileSystemOperationContext* context,
    const fileapi::FileSystemURL& url,
    base::FilePath* local_file_path) {
  DCHECK(local_file_path);
  DCHECK(url.is_valid());

  NOTREACHED();
  return base::PLATFORM_FILE_ERROR_INVALID_OPERATION;
}

}  // namespace itunes
