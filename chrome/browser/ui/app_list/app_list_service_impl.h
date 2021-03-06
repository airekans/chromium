// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_IMPL_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache_observer.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/app_list/profile_loader.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace base {
class FilePath;
}

namespace content {
class NotificationSource;
class NotificationDetails;
}

// Parts of the AppListService implementation shared between platforms.
class AppListServiceImpl : public AppListService,
                           public ProfileInfoCacheObserver,
                           public content::NotificationObserver {
 public:
  static void RecordAppListLaunch();
  static void RecordAppListAppLaunch();
  static void SendAppListStats();

 protected:
  AppListServiceImpl();
  virtual ~AppListServiceImpl();

  Profile* profile() const { return profile_; }
  void SetProfile(Profile* new_profile);
  void InvalidatePendingProfileLoads();
  ProfileLoader& profile_loader() { return profile_loader_; }
  const ProfileLoader& profile_loader() const { return profile_loader_; }

  // Save |profile_file_path| as the app list profile in local state.
  void SaveProfilePathToLocalState(const base::FilePath& profile_file_path);

  // Called in response to observed successful and unsuccessful signin changes.
  virtual void OnSigninStatusChanged();

  // AppListService overrides:
  virtual void Init(Profile* initial_profile) OVERRIDE;
  virtual base::FilePath GetProfilePath(
      const base::FilePath& user_data_dir) OVERRIDE;

  virtual void ShowForSavedProfile() OVERRIDE;
  virtual AppListControllerDelegate* CreateControllerDelegate() OVERRIDE;

 private:
  // Loads a profile asynchronously and calls OnProfileLoaded() when done.
  void LoadProfileAsync(const base::FilePath& profile_file_path);

  // Callback for asynchronous profile load.
  void OnProfileLoaded(int profile_load_sequence_id,
                       Profile* profile,
                       Profile::CreateStatus status);

  // AppListService overrides:
  // Update the profile path stored in local prefs, load it (if not already
  // loaded), and show the app list.
  virtual void SetAppListProfile(
      const base::FilePath& profile_file_path) OVERRIDE;

  virtual Profile* GetCurrentAppListProfile() OVERRIDE;

  // ProfileInfoCacheObserver overrides:
  virtual void OnProfileAdded(const base::FilePath& profilePath) OVERRIDE;
  virtual void OnProfileWillBeRemoved(
      const base::FilePath& profile_path) OVERRIDE;
  virtual void OnProfileWasRemoved(const base::FilePath& profile_path,
                                   const string16& profile_name) OVERRIDE;
  virtual void OnProfileNameChanged(const base::FilePath& profile_path,
                                    const string16& profile_name) OVERRIDE;
  virtual void OnProfileAvatarChanged(
      const base::FilePath& profile_path) OVERRIDE;

  // content::NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // The profile the AppList is currently displaying.
  Profile* profile_;

  // Incremented to indicate that pending profile loads are no longer valid.
  int profile_load_sequence_id_;

  // How many profile loads are pending.
  int pending_profile_loads_;

  base::WeakPtrFactory<AppListServiceImpl> weak_factory_;
  content::NotificationRegistrar registrar_;

  ProfileLoader profile_loader_;

  DISALLOW_COPY_AND_ASSIGN(AppListServiceImpl);
};

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_SERVICE_IMPL_H_
