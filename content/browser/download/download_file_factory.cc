// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/download_file_factory.h"

#include "content/browser/download/download_file_impl.h"
#include "content/browser/power_save_blocker.h"

namespace content {

DownloadFileFactory::~DownloadFileFactory() {}

DownloadFile* DownloadFileFactory::CreateFile(
    scoped_ptr<DownloadSaveInfo> save_info,
    const FilePath& default_downloads_directory,
    const GURL& url,
    const GURL& referrer_url,
    int64 received_bytes,
    bool calculate_hash,
    scoped_ptr<ByteStreamReader> stream,
    const net::BoundNetLog& bound_net_log,
    base::WeakPtr<DownloadDestinationObserver> observer) {
  scoped_ptr<PowerSaveBlocker> psb(
      new PowerSaveBlocker(
          PowerSaveBlocker::kPowerSaveBlockPreventAppSuspension,
          "Download in progress"));
  return new DownloadFileImpl(
      save_info.Pass(), default_downloads_directory, url, referrer_url,
      received_bytes, calculate_hash, stream.Pass(), bound_net_log,
      psb.Pass(), observer);
}

}  // namespace content
