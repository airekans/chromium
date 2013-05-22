// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_PICASA_PICASA_FILE_UTIL_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_PICASA_PICASA_FILE_UTIL_H_

#include "chrome/browser/media_galleries/fileapi/native_media_file_util.h"

namespace picasa {

class PicasaDataProvider;

extern const char kPicasaDirAlbums[];
extern const char kPicasaDirFolders[];

class PicasaFileUtil : public chrome::NativeMediaFileUtil {
 public:
  PicasaFileUtil();
  virtual ~PicasaFileUtil();

  // Overrides from NativeMediaFileUtil
  virtual base::PlatformFileError GetFileInfo(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      base::PlatformFileInfo* file_info,
      base::FilePath* platform_path) OVERRIDE;

  // Paths are enumerated in lexicographical order.
  virtual scoped_ptr<AbstractFileEnumerator> CreateFileEnumerator(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url) OVERRIDE;

  virtual base::PlatformFileError GetLocalFilePath(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      base::FilePath* local_file_path) OVERRIDE;

 private:
  virtual PicasaDataProvider* DataProvider();

  DISALLOW_COPY_AND_ASSIGN(PicasaFileUtil);
};

}  // namespace picasa

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_PICASA_PICASA_FILE_UTIL_H_
