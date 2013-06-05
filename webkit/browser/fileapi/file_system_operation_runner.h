// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_BROWSER_FILEAPI_FILE_SYSTEM_OPERATION_RUNNER_H_
#define WEBKIT_BROWSER_FILEAPI_FILE_SYSTEM_OPERATION_RUNNER_H_

#include "base/basictypes.h"
#include "base/id_map.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "webkit/browser/fileapi/file_system_operation.h"
#include "webkit/storage/webkit_storage_export.h"

namespace fileapi {

class FileSystemURL;
class FileSystemContext;

// A central interface for running FileSystem API operations.
// All operation methods take callback and returns OperationID, which is
// an integer value which can be used for cancelling an operation.
// All operation methods return kErrorOperationID if running (posting) an
// operation fails, in addition to dispatching the callback with an error
// code (therefore in most cases the caller does not need to check the
// returned operation ID).
class WEBKIT_STORAGE_EXPORT FileSystemOperationRunner
    : public base::SupportsWeakPtr<FileSystemOperationRunner> {
 public:
  typedef FileSystemOperation::GetMetadataCallback GetMetadataCallback;
  typedef FileSystemOperation::ReadDirectoryCallback ReadDirectoryCallback;
  typedef FileSystemOperation::SnapshotFileCallback SnapshotFileCallback;
  typedef FileSystemOperation::StatusCallback StatusCallback;
  typedef FileSystemOperation::WriteCallback WriteCallback;
  typedef FileSystemOperation::OpenFileCallback OpenFileCallback;

  typedef int OperationID;

  static const OperationID kErrorOperationID;

  virtual ~FileSystemOperationRunner();

  // Creates a file at |url|. If |exclusive| is true, an error is raised
  // in case a file is already present at the URL.
  OperationID CreateFile(const FileSystemURL& url,
                         bool exclusive,
                         const StatusCallback& callback);

  OperationID CreateDirectory(const FileSystemURL& url,
                              bool exclusive,
                              bool recursive,
                              const StatusCallback& callback);

  // Copies a file or directory from |src_url| to |dest_url|. If
  // |src_url| is a directory, the contents of |src_url| are copied to
  // |dest_url| recursively. A new file or directory is created at
  // |dest_url| as needed.
  OperationID Copy(const FileSystemURL& src_url,
                   const FileSystemURL& dest_url,
                   const StatusCallback& callback);

  // Moves a file or directory from |src_url| to |dest_url|. A new file
  // or directory is created at |dest_url| as needed.
  OperationID Move(const FileSystemURL& src_url,
                   const FileSystemURL& dest_url,
                   const StatusCallback& callback);

  // Checks if a directory is present at |url|.
  OperationID DirectoryExists(const FileSystemURL& url,
                              const StatusCallback& callback);

  // Checks if a file is present at |url|.
  OperationID FileExists(const FileSystemURL& url,
                         const StatusCallback& callback);

  // Gets the metadata of a file or directory at |url|.
  OperationID GetMetadata(const FileSystemURL& url,
                          const GetMetadataCallback& callback);

  // Reads contents of a directory at |url|.
  OperationID ReadDirectory(const FileSystemURL& url,
                            const ReadDirectoryCallback& callback);

  // Removes a file or directory at |url|. If |recursive| is true, remove
  // all files and directories under the directory at |url| recursively.
  OperationID Remove(const FileSystemURL& url, bool recursive,
                     const StatusCallback& callback);

  // Writes contents of |blob_url| to |url| at |offset|.
  // |url_request_context| is used to read contents in |blob_url|.
  OperationID Write(const net::URLRequestContext* url_request_context,
                    const FileSystemURL& url,
                    const GURL& blob_url,
                    int64 offset,
                    const WriteCallback& callback);

  // Truncates a file at |url| to |length|. If |length| is larger than
  // the original file size, the file will be extended, and the extended
  // part is filled with null bytes.
  OperationID Truncate(const FileSystemURL& url, int64 length,
                       const StatusCallback& callback);

  // Tries to cancel the operation |id| [we support cancelling write or
  // truncate only]. Reports failure for the current operation, then reports
  // success for the cancel operation itself via the |callback|.
  void Cancel(OperationID id, const StatusCallback& callback);

  // Modifies timestamps of a file or directory at |url| with
  // |last_access_time| and |last_modified_time|. The function DOES NOT
  // create a file unlike 'touch' command on Linux.
  //
  // This function is used only by Pepper as of writing.
  OperationID TouchFile(const FileSystemURL& url,
                        const base::Time& last_access_time,
                        const base::Time& last_modified_time,
                        const StatusCallback& callback);

  // Opens a file at |url| with |file_flags|, where flags are OR'ed
  // values of base::PlatformFileFlags.
  //
  // |peer_handle| is the process handle of a pepper plugin process, which
  // is necessary for underlying IPC calls with Pepper plugins.
  //
  // This function is used only by Pepper as of writing.
  OperationID OpenFile(const FileSystemURL& url,
                       int file_flags,
                       base::ProcessHandle peer_handle,
                       const OpenFileCallback& callback);

  // Creates a local snapshot file for a given |url| and returns the
  // metadata and platform url of the snapshot file via |callback|.
  // In local filesystem cases the implementation may simply return
  // the metadata of the file itself (as well as GetMetadata does),
  // while in remote filesystem case the backend may want to download the file
  // into a temporary snapshot file and return the metadata of the
  // temporary file.  Or if the implementaiton already has the local cache
  // data for |url| it can simply return the url to the cache.
  OperationID CreateSnapshotFile(const FileSystemURL& url,
                                 const SnapshotFileCallback& callback);

 private:
  friend class FileSystemContext;
  explicit FileSystemOperationRunner(FileSystemContext* file_system_context);

  void DidFinish(OperationID id,
                 const StatusCallback& callback,
                 base::PlatformFileError rv);
  void DidGetMetadata(OperationID id,
                      const GetMetadataCallback& callback,
                      base::PlatformFileError rv,
                      const base::PlatformFileInfo& file_info,
                      const base::FilePath& platform_path);
  void DidReadDirectory(OperationID id,
                        const ReadDirectoryCallback& callback,
                        base::PlatformFileError rv,
                        const std::vector<DirectoryEntry>& entries,
                        bool has_more);
  void DidWrite(OperationID id,
                const WriteCallback& callback,
                base::PlatformFileError rv,
                int64 bytes,
                bool complete);
  void DidOpenFile(
      OperationID id,
      const OpenFileCallback& callback,
      base::PlatformFileError rv,
      base::PlatformFile file,
      const base::Closure& on_close_callback,
      base::ProcessHandle peer_handle);
  void DidCreateSnapshot(
      OperationID id,
      const SnapshotFileCallback& callback,
      base::PlatformFileError rv,
      const base::PlatformFileInfo& file_info,
      const base::FilePath& platform_path,
      const scoped_refptr<webkit_blob::ShareableFileReference>& file_ref);

  // Not owned; file_system_context owns this.
  FileSystemContext* file_system_context_;

  // IDMap<FileSystemOperation, IDMapOwnPointer> operations_;
  IDMap<FileSystemOperation> operations_;

  DISALLOW_COPY_AND_ASSIGN(FileSystemOperationRunner);
};

}  // namespace fileapi

#endif  // WEBKIT_BROWSER_FILEAPI_FILE_SYSTEM_OPERATION_RUNNER_H_
