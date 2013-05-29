// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_METADATA_DB_MIGRATION_UTIL_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_METADATA_DB_MIGRATION_UTIL_H_

#include <string>

#include "chrome/browser/sync_file_system/sync_file_system.pb.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"
#include "webkit/browser/fileapi/syncable/sync_status_code.h"

namespace sync_file_system {
namespace drive {

// Adds "file:" prefix to WAPI resource ID.
// EXAMPLE:  "xxx" => "file:xxx"
std::string AddWapiFilePrefix(const std::string& resource_id);

// Adds "folder:" prefix to WAPI resource ID.
// EXAMPLE:  "xxx" => "folder:xxx"
std::string AddWapiFolderPrefix(const std::string& resource_id);

// Adds a prefix corresponding to the given |type|.
std::string AddWapiIdPrefix(const std::string& resource_id,
                            DriveMetadata_ResourceType type);

// Removes a prefix from WAPI resource ID.
// EXAMPLE:
//   "file:xxx"    =>  "xxx"
//   "folder:yyy"  =>  "yyy"
//   "zzz"         =>  "zzz"
std::string RemoveWapiIdPrefix(const std::string& resource_id);

// Migrate |db| scheme from version 1 to version 2.
SyncStatusCode MigrateDatabaseFromV1ToV2(leveldb::DB* db);

}  // namespace drive
}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_METADATA_DB_MIGRATION_UTIL_H_
