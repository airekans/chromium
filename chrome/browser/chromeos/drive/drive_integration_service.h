// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_INTEGRATION_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_INTEGRATION_SERVICE_H_

#include <string>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/google_apis/drive_notification_observer.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

namespace base {
class FilePath;
}

namespace google_apis {
class DriveServiceInterface;
}

namespace drive {

class DebugInfoCollector;
class DownloadHandler;
class DriveWebAppsRegistry;
class FileSystemInterface;
class FileSystemProxy;
class FileWriteHelper;
class JobListInterface;

namespace internal {
class FileCache;
class ResourceMetadata;
class StaleCacheFilesRemover;
class SyncClient;
}  // namespace internal

// Interface for classes that need to observe events from
// DriveIntegrationService.  All events are notified on UI thread.
class DriveIntegrationServiceObserver {
 public:
  // Triggered when the file system is mounted.
  virtual void OnFileSystemMounted() {
  }

  // Triggered when the file system is being unmounted.
  virtual void OnFileSystemBeingUnmounted() {
  }

 protected:
  virtual ~DriveIntegrationServiceObserver() {}
};

// DriveIntegrationService is used to integrate Drive to Chrome. This class
// exposes the file system representation built on top of Drive and some
// other Drive related objects to the file manager, and some other sub
// systems.
//
// The class is essentially a container that manages lifetime of the objects
// that are used to integrate Drive to Chrome. The object of this class is
// created per-profile.
class DriveIntegrationService
    : public ProfileKeyedService,
      public google_apis::DriveNotificationObserver {
 public:
  // test_drive_service, test_cache_root and test_file_system are used by tests
  // to inject customized instances.
  // Pass NULL or the empty value when not interested.
  DriveIntegrationService(
      Profile* profile,
      google_apis::DriveServiceInterface* test_drive_service,
      const base::FilePath& test_cache_root,
      FileSystemInterface* test_file_system);
  virtual ~DriveIntegrationService();

  // Initializes the object. This function should be called before any
  // other functions.
  void Initialize();

  // ProfileKeyedService override:
  virtual void Shutdown() OVERRIDE;

  // Adds and removes the observer.
  void AddObserver(DriveIntegrationServiceObserver* observer);
  void RemoveObserver(DriveIntegrationServiceObserver* observer);

  // google_apis::DriveNotificationObserver implementation.
  virtual void OnNotificationReceived() OVERRIDE;
  virtual void OnPushNotificationEnabled(bool enabled) OVERRIDE;

  google_apis::DriveServiceInterface* drive_service() {
    return drive_service_.get();
  }

  DebugInfoCollector* debug_info_collector() {
    return debug_info_collector_.get();
  }
  FileSystemInterface* file_system() { return file_system_.get(); }
  FileWriteHelper* file_write_helper() { return file_write_helper_.get(); }
  DownloadHandler* download_handler() { return download_handler_.get(); }
  DriveWebAppsRegistry* webapps_registry() { return webapps_registry_.get(); }
  JobListInterface* job_list() { return scheduler_.get(); }

  // Clears all the local cache files and in-memory data, and remounts the
  // file system. |callback| is called with true when this operation is done
  // successfully. Otherwise, |callback| is called with false.
  // |callback| must not be null.
  void ClearCacheAndRemountFileSystem(
      const base::Callback<void(bool)>& callback);

  // Reloads and remounts the file system.
  void ReloadAndRemountFileSystem();

 private:
  // Returns true if Drive is enabled.
  // Must be called on UI thread.
  bool IsDriveEnabled();

  // Registers remote file system proxy for drive mount point.
  void AddDriveMountPoint();
  // Unregisters drive mount point from File API.
  void RemoveDriveMountPoint();

  // Adds back the drive mount point.
  // Used to implement ClearCacheAndRemountFileSystem().
  void AddBackDriveMountPoint(const base::Callback<void(bool)>& callback,
                              bool success);

  // Called when cache initialization is done. Continues initialization if
  // the cache initialization is successful.
  void InitializeAfterCacheInitialized(bool success);

  // Called when resource metadata initialization is done. Continues
  // initialization if resource metadata initialization is successful.
  void InitializeAfterResourceMetadataInitialized(FileError error);

  // Disables Drive. Used to disable Drive when needed (ex. initialization of
  // the Drive cache failed).
  // Must be called on UI thread.
  void DisableDrive();

  friend class DriveIntegrationServiceFactory;

  Profile* profile_;
  // True if Drive is disabled due to initialization errors.
  bool drive_disabled_;

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  scoped_ptr<internal::FileCache, util::DestroyHelper> cache_;
  scoped_ptr<google_apis::DriveServiceInterface> drive_service_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<DriveWebAppsRegistry> webapps_registry_;
  scoped_ptr<internal::ResourceMetadata,
             util::DestroyHelper> resource_metadata_;
  scoped_ptr<FileSystemInterface> file_system_;
  scoped_ptr<FileWriteHelper> file_write_helper_;
  scoped_ptr<DownloadHandler> download_handler_;
  scoped_ptr<internal::SyncClient> sync_client_;
  scoped_ptr<internal::StaleCacheFilesRemover> stale_cache_files_remover_;
  scoped_refptr<FileSystemProxy> file_system_proxy_;
  scoped_ptr<DebugInfoCollector> debug_info_collector_;

  ObserverList<DriveIntegrationServiceObserver> observers_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  base::WeakPtrFactory<DriveIntegrationService> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(DriveIntegrationService);
};

// Singleton that owns all instances of DriveIntegrationService and
// associates them with Profiles.
class DriveIntegrationServiceFactory : public ProfileKeyedServiceFactory {
 public:
  // Factory function used by tests.
  typedef base::Callback<DriveIntegrationService*(Profile* profile)>
      FactoryCallback;

  // Returns the DriveIntegrationService for |profile|, creating it if it is
  // not yet created.
  //
  // This function starts returning NULL if Drive is disabled, even if this
  // function previously returns a non-NULL object. In other words, clients
  // can assume that Drive is enabled if this function returns a non-NULL
  // object.
  static DriveIntegrationService* GetForProfile(Profile* profile);

  // Similar to GetForProfile(), but returns the instance regardless of if
  // Drive is enabled/disabled.
  static DriveIntegrationService* GetForProfileRegardlessOfStates(
      Profile* profile);

  // Returns the DriveIntegrationService that is already associated with
  // |profile|, if it is not yet created it will return NULL.
  //
  // This function starts returning NULL if Drive is disabled. See also the
  // comment at GetForProfile().
  static DriveIntegrationService* FindForProfile(Profile* profile);

  // Similar to FindForProfile(), but returns the instance regardless of if
  // Drive is enabled/disabled.
  static DriveIntegrationService* FindForProfileRegardlessOfStates(
      Profile* profile);

  // Returns the DriveIntegrationServiceFactory instance.
  static DriveIntegrationServiceFactory* GetInstance();

  // Sets a factory function for tests.
  static void SetFactoryForTest(const FactoryCallback& factory_for_test);

 private:
  friend struct DefaultSingletonTraits<DriveIntegrationServiceFactory>;

  DriveIntegrationServiceFactory();
  virtual ~DriveIntegrationServiceFactory();

  // ProfileKeyedServiceFactory:
  virtual ProfileKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const OVERRIDE;

  FactoryCallback factory_for_test_;
};

}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_DRIVE_INTEGRATION_SERVICE_H_
