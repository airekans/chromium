// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_REGISTRATION_SERVICE_H_
#define CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_REGISTRATION_SERVICE_H_

#include <map>
#include <string>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/prefs/pref_change_registrar.h"
#include "base/string16.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "sync/api/syncable_service.h"

class GoogleServiceAuthError;
class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

// Holds the state necessary for registering a new managed user with the
// management server and associating it with its custodian. It is owned by the
// custodian's profile.
class ManagedUserRegistrationService : public BrowserContextKeyedService,
                                       public syncer::SyncableService {
 public:
  // Callback for Register() below. If registration is successful, |token| will
  // contain an OAuth2 refresh token for the newly registered managed user,
  // otherwise |token| will be empty and |error| will contain the authentication
  // error for the custodian.
  typedef base::Callback<void(const GoogleServiceAuthError& /* error */,
                              const std::string& /* token */)>
      RegistrationCallback;

  explicit ManagedUserRegistrationService(PrefService* prefs);
  virtual ~ManagedUserRegistrationService();

  static void RegisterUserPrefs(user_prefs::PrefRegistrySyncable* registry);

  // Registers a new managed user with the server. |name| is the display name of
  // the user. |callback| is called with the result of the registration.
  void Register(const string16& name, const RegistrationCallback& callback);

  // Convenience method that registers a new managed user with the server and
  // initializes it locally. The callback allows it to be run after a new
  // profile has been created:
  //   ProfileManager::CreateMultiProfileAsync(
  //       name, icon,
  //       managed_user_registration_service->GetRegistrationAndInitCallback(),
  //       managed_user);
  ProfileManager::CreateCallback GetRegistrationAndInitCallback();

  // ProfileKeyedService implementation:
  virtual void Shutdown() OVERRIDE;

  // SyncableService implementation:
  virtual syncer::SyncMergeResult MergeDataAndStartSyncing(
      syncer::ModelType type,
      const syncer::SyncDataList& initial_sync_data,
      scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
      scoped_ptr<syncer::SyncErrorFactory> error_handler) OVERRIDE;
  virtual void StopSyncing(syncer::ModelType type) OVERRIDE;
  virtual syncer::SyncDataList GetAllSyncData(syncer::ModelType type) const
      OVERRIDE;
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;

 private:
  void OnLastSignedInUsernameChange();

  // Called when the Sync server has acknowledged a newly created managed user.
  void OnManagedUserAcknowledged(const std::string& managed_user_id);

  // Called when we have received a token for the managed user.
  void OnReceivedToken(const std::string& token);

  // Dispatches the callback if all the conditions have been met.
  void DispatchCallbackIfReady();

  void CancelPendingRegistration();

  // Dispatches the callback with the saved token (which may be empty) and the
  // given |error|.
  void DispatchCallback(const GoogleServiceAuthError& error);

  void OnProfileCreated(Profile* profile, Profile::CreateStatus status);

  base::WeakPtrFactory<ManagedUserRegistrationService> weak_ptr_factory_;
  PrefService* prefs_;
  PrefChangeRegistrar pref_change_registrar_;

  scoped_ptr<syncer::SyncChangeProcessor> sync_processor_;
  scoped_ptr<syncer::SyncErrorFactory> error_handler_;

  std::string pending_managed_user_id_;
  std::string pending_managed_user_token_;
  bool pending_managed_user_acknowledged_;
  RegistrationCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(ManagedUserRegistrationService);
};

#endif  // CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_REGISTRATION_SERVICE_H_
