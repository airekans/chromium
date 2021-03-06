// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_FILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_FILE_SYNC_SERVICE_H_

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/google_apis/drive_notification_observer.h"
#include "chrome/browser/sync_file_system/drive/api_util_interface.h"
#include "chrome/browser/sync_file_system/drive_metadata_store.h"
#include "chrome/browser/sync_file_system/local_change_processor.h"
#include "chrome/browser/sync_file_system/local_sync_operation_resolver.h"
#include "chrome/browser/sync_file_system/remote_change_handler.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_file_system.pb.h"
#include "webkit/browser/fileapi/syncable/file_change.h"
#include "webkit/browser/fileapi/syncable/sync_action.h"
#include "webkit/browser/fileapi/syncable/sync_callbacks.h"
#include "webkit/browser/fileapi/syncable/sync_direction.h"
#include "webkit/browser/fileapi/syncable/sync_status_code.h"

class ExtensionService;

namespace google_apis {
class ResourceList;
}

namespace tracked_objects {
class Location;
}

namespace sync_file_system {

namespace drive {
class LocalChangeProcessorDelegate;
}

class DriveFileSyncTaskManager;

// Maintains remote file changes.
// Owned by SyncFileSystemService (which is a per-profile object).
class DriveFileSyncService : public RemoteFileSyncService,
                             public LocalChangeProcessor,
                             public drive::APIUtilObserver,
                             public base::NonThreadSafe,
                             public base::SupportsWeakPtr<DriveFileSyncService>,
                             public google_apis::DriveNotificationObserver {
 public:
  enum ConflictResolutionResult {
    CONFLICT_RESOLUTION_MARK_CONFLICT,
    CONFLICT_RESOLUTION_LOCAL_WIN,
    CONFLICT_RESOLUTION_REMOTE_WIN,
  };

  typedef base::Callback<void(const SyncStatusCallback& callback)> Task;

  static ConflictResolutionPolicy kDefaultPolicy;

  virtual ~DriveFileSyncService();

  // Creates DriveFileSyncService.
  static scoped_ptr<DriveFileSyncService> Create(Profile* profile);

  // Creates DriveFileSyncService instance for testing.
  // |metadata_store| must be initialized beforehand.
  static scoped_ptr<DriveFileSyncService> CreateForTesting(
      Profile* profile,
      const base::FilePath& base_dir,
      scoped_ptr<drive::APIUtilInterface> api_util,
      scoped_ptr<DriveMetadataStore> metadata_store);

  // Destroys |sync_service| and passes the ownership of |sync_client| to caller
  // for testing.
  static scoped_ptr<drive::APIUtilInterface> DestroyAndPassAPIUtilForTesting(
      scoped_ptr<DriveFileSyncService> sync_service);

  // RemoteFileSyncService overrides.
  virtual void AddServiceObserver(Observer* observer) OVERRIDE;
  virtual void AddFileStatusObserver(FileStatusObserver* observer) OVERRIDE;
  virtual void RegisterOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void UnregisterOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void EnableOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void DisableOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void UninstallOrigin(
      const GURL& origin,
      const SyncStatusCallback& callback) OVERRIDE;
  virtual void ProcessRemoteChange(const SyncFileCallback& callback) OVERRIDE;
  virtual void SetRemoteChangeProcessor(
      RemoteChangeProcessor* processor) OVERRIDE;
  virtual LocalChangeProcessor* GetLocalChangeProcessor() OVERRIDE;
  virtual bool IsConflicting(const fileapi::FileSystemURL& url) OVERRIDE;
  virtual RemoteServiceState GetCurrentState() const OVERRIDE;
  virtual void SetSyncEnabled(bool enabled) OVERRIDE;
  virtual SyncStatusCode SetConflictResolutionPolicy(
      ConflictResolutionPolicy resolution) OVERRIDE;
  virtual ConflictResolutionPolicy GetConflictResolutionPolicy() const OVERRIDE;

  // LocalChangeProcessor overrides.
  virtual void ApplyLocalChange(
      const FileChange& change,
      const base::FilePath& local_file_path,
      const SyncFileMetadata& local_file_metadata,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback) OVERRIDE;

  // DriveFileSyncClientObserver overrides.
  virtual void OnAuthenticated() OVERRIDE;
  virtual void OnNetworkConnected() OVERRIDE;

  // google_apis::DriveNotificationObserver implementation.
  virtual void OnNotificationReceived() OVERRIDE;
  virtual void OnPushNotificationEnabled(bool enabled) OVERRIDE;

  // Called from DriveFileSyncTaskManager.
  // TODO: factor out as an interface.
  void MaybeScheduleNextTask();
  void NotifyLastOperationStatus(
      SyncStatusCode sync_status,
      google_apis::GDataErrorCode gdata_error);

  static std::string PathToTitle(const base::FilePath& path);
  static base::FilePath TitleToPath(const std::string& title);
  static DriveMetadata::ResourceType SyncFileTypeToDriveMetadataResourceType(
      SyncFileType file_type);
  static SyncFileType DriveMetadataResourceTypeToSyncFileType(
      DriveMetadata::ResourceType resource_type);

 private:
  friend class DriveFileSyncTaskManager;
  friend class drive::LocalChangeProcessorDelegate;

  friend class DriveFileSyncServiceMockTest;
  friend class DriveFileSyncServiceSyncTest;
  friend class DriveFileSyncServiceTest;
  struct ApplyLocalChangeParam;
  struct ProcessRemoteChangeParam;

  typedef base::Callback<void(const base::Time& time,
                              SyncFileType remote_file_type,
                              SyncStatusCode status)> UpdatedTimeCallback;
  typedef base::Callback<
      void(SyncStatusCode status,
           const std::string& resource_id)> ResourceIdCallback;

  explicit DriveFileSyncService(Profile* profile);

  void Initialize(scoped_ptr<DriveFileSyncTaskManager> task_manager,
                  const SyncStatusCallback& callback);
  void InitializeForTesting(scoped_ptr<DriveFileSyncTaskManager> task_manager,
                            const base::FilePath& base_dir,
                            scoped_ptr<drive::APIUtilInterface> sync_client,
                            scoped_ptr<DriveMetadataStore> metadata_store,
                            const SyncStatusCallback& callback);

  void DidInitializeMetadataStore(const SyncStatusCallback& callback,
                                  SyncStatusCode status,
                                  bool created);
  void DidGetDriveRootResourceId(const SyncStatusCallback& callback,
                                 google_apis::GDataErrorCode error,
                                 const std::string& root_resource_id);

  void UpdateServiceStateFromLastOperationStatus(
      SyncStatusCode sync_status,
      google_apis::GDataErrorCode gdata_error);

  // Updates the service state. Also this may notify observers if the
  // service state has been changed from the original value.
  void UpdateServiceState(RemoteServiceState state,
                          const std::string& description);

  void DoRegisterOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback);
  void DoUnregisterOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback);
  void DoEnableOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback);
  void DoDisableOriginForTrackingChanges(
      const GURL& origin,
      const SyncStatusCallback& callback);
  void DoUninstallOrigin(
      const GURL& origin,
      const SyncStatusCallback& callback);
  void DoProcessRemoteChange(
      const SyncFileCallback& sync_callback,
      const SyncStatusCallback& completion_callback);
  void DoApplyLocalChange(
      const FileChange& change,
      const base::FilePath& local_file_path,
      const SyncFileMetadata& local_file_metadata,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback);

  // Local synchronization related methods.
  ConflictResolutionResult ResolveConflictForLocalSync(
      SyncFileType local_file_type,
      const base::Time& local_update_time,
      SyncFileType remote_file_type,
      const base::Time& remote_update_time);
  void DidApplyLocalChange(const SyncStatusCallback& callback,
                           SyncStatusCode status);

  void UpdateRegisteredOrigins();

  void StartBatchSync(const SyncStatusCallback& callback);
  void GetDriveDirectoryForOrigin(const GURL& origin,
                                  const SyncStatusCallback& callback,
                                  const std::string& sync_root_resource_id);
  void DidGetDriveDirectoryForOrigin(const GURL& origin,
                                     const SyncStatusCallback& callback,
                                     SyncStatusCode status,
                                     const std::string& resource_id);
  void DidUninstallOrigin(const GURL& origin,
                          const SyncStatusCallback& callback,
                          google_apis::GDataErrorCode error);
  void DidGetLargestChangeStampForBatchSync(
      const SyncStatusCallback& callback,
      const GURL& origin,
      const std::string& resource_id,
      google_apis::GDataErrorCode error,
      int64 largest_changestamp);
  void DidGetDirectoryContentForBatchSync(
      const SyncStatusCallback& callback,
      const GURL& origin,
      const std::string& resource_id,
      int64 largest_changestamp,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::ResourceList> feed);

  // Remote synchronization related methods.
  void DidPrepareForProcessRemoteChange(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status,
      const SyncFileMetadata& metadata,
      const FileChangeList& changes);
  void DidResolveConflictToLocalChange(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);
  void DownloadForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param);
  void DidGetTemporaryFileForDownload(
      scoped_ptr<ProcessRemoteChangeParam> param,
      bool success);
  void DidDownloadFileForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      google_apis::GDataErrorCode error,
      const std::string& md5_checksum,
      int64 file_size,
      const base::Time& updated_time);
  void DidApplyRemoteChange(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);
  void DidCleanUpForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      bool success);
  void DeleteMetadataForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param);
  void CompleteRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);
  void AbortRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);
  void FinalizeRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);
  void HandleConflictForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      const base::Time& remote_updated_time,
      SyncFileType remote_file_change,
      SyncStatusCode status);
  void ResolveConflictToLocalForRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param);
  void StartOverRemoteSync(
      scoped_ptr<ProcessRemoteChangeParam> param,
      SyncStatusCode status);

  // Returns true if |pending_changes_| was updated.
  bool AppendRemoteChange(
      const GURL& origin,
      const google_apis::ResourceEntry& entry,
      int64 changestamp,
      RemoteChangeHandler::RemoteSyncType sync_type);
  bool AppendFetchChange(
      const GURL& origin,
      const base::FilePath& path,
      const std::string& resource_id,
      SyncFileType file_type);
  bool AppendRemoteChangeInternal(
      const GURL& origin,
      const base::FilePath& path,
      bool is_deleted,
      const std::string& resource_id,
      int64 changestamp,
      const std::string& remote_file_md5,
      const base::Time& updated_time,
      SyncFileType file_type,
      RemoteChangeHandler::RemoteSyncType sync_type);
  void RemoveRemoteChange(const fileapi::FileSystemURL& url);
  void MaybeMarkAsIncrementalSyncOrigin(const GURL& origin);

  void MarkConflict(
      const fileapi::FileSystemURL& url,
      DriveMetadata* drive_metadata,
      const SyncStatusCallback& callback);
  void DidGetRemoteFileMetadataForRemoteUpdatedTime(
      const UpdatedTimeCallback& callback,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::ResourceEntry> entry);

  // A wrapper implementation to GDataErrorCodeToSyncStatusCode which returns
  // authentication error if the user is not signed in.
  SyncStatusCode GDataErrorCodeToSyncStatusCodeWrapper(
      google_apis::GDataErrorCode error);

  base::FilePath temporary_file_dir_;

  // May start batch sync or incremental sync.
  // This posts either one of following tasks:
  // - StartBatchSyncForOrigin() if it has any pending batch sync origins, or
  // - FetchChangesForIncrementalSync() otherwise.
  //
  // These two methods are called only from this method.
  void MaybeStartFetchChanges();

  void FetchChangesForIncrementalSync(const SyncStatusCallback& callback);
  void DidFetchChangesForIncrementalSync(
      const SyncStatusCallback& callback,
      bool has_new_changes,
      google_apis::GDataErrorCode error,
      scoped_ptr<google_apis::ResourceList> changes);
  bool GetOriginForEntry(const google_apis::ResourceEntry& entry, GURL* origin);
  void NotifyObserversFileStatusChanged(const fileapi::FileSystemURL& url,
                                        SyncFileStatus sync_status,
                                        SyncAction action_taken,
                                        SyncDirection direction);

  void HandleSyncRootDirectoryChange(const google_apis::ResourceEntry& entry);
  void HandleOriginRootDirectoryChange(const google_apis::ResourceEntry& entry);

  void EnsureSyncRootDirectory(const ResourceIdCallback& callback);
  void DidEnsureSyncRoot(const ResourceIdCallback& callback,
                         google_apis::GDataErrorCode error,
                         const std::string& sync_root_resource_id);
  void EnsureOriginRootDirectory(const GURL& origin,
                                 const ResourceIdCallback& callback);
  void DidEnsureSyncRootForOriginRoot(const GURL& origin,
                                      const ResourceIdCallback& callback,
                                      SyncStatusCode status,
                                      const std::string& sync_root_resource_id);
  void DidEnsureOriginRoot(const GURL& origin,
                           const ResourceIdCallback& callback,
                           google_apis::GDataErrorCode error,
                           const std::string& resource_id);

  // This function returns Resouce ID for the sync root directory if available.
  // Returns an empty string 1) when the resource ID has not been initialized
  // yet, and 2) after the service has detected the remote sync root folder was
  // removed.
  std::string sync_root_resource_id();

  scoped_ptr<DriveMetadataStore> metadata_store_;
  scoped_ptr<drive::APIUtilInterface> api_util_;

  Profile* profile_;

  scoped_ptr<DriveFileSyncTaskManager> task_manager_;

  scoped_ptr<drive::LocalChangeProcessorDelegate> running_local_sync_task_;

  // The current remote service state. This does NOT reflect the
  // sync_enabled_ flag, while GetCurrentState() DOES reflect the flag
  // value (i.e. it returns REMOTE_SERVICE_DISABLED when sync_enabled_
  // is false even if state_ is REMOTE_SERVICE_OK).
  RemoteServiceState state_;

  // Indicates if sync is enabled or not. This flag can be turned on or
  // off by SetSyncEnabled() method.  To start synchronization
  // this needs to be true and state_ needs to be REMOTE_SERVICE_OK.
  bool sync_enabled_;

  int64 largest_fetched_changestamp_;

  std::map<GURL, std::string> pending_batch_sync_origins_;

  // Is set to true when there's a fair possibility that we have some
  // remote changes that haven't been fetched yet.
  //
  // This flag is set when:
  // - This gets invalidation notification,
  // - The service is authenticated or becomes online, and
  // - The polling timer is fired.
  //
  // This flag is cleared when:
  // - A batch or incremental sync has been started, and
  // - When all pending batch sync tasks have been finished.
  bool may_have_unfetched_changes_;

  ObserverList<Observer> service_observers_;
  ObserverList<FileStatusObserver> file_status_observers_;

  RemoteChangeHandler remote_change_handler_;
  RemoteChangeProcessor* remote_change_processor_;

  ConflictResolutionPolicy conflict_resolution_;

  DISALLOW_COPY_AND_ASSIGN(DriveFileSyncService);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_FILE_SYNC_SERVICE_H_
