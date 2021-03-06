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
#include "base/timer.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "sync/api/syncable_service.h"

class GoogleServiceAuthError;
class ManagedUserRefreshTokenFetcher;
class PrefService;

namespace browser_sync {
class DeviceInfo;
}

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

  ManagedUserRegistrationService(
      PrefService* prefs,
      scoped_ptr<ManagedUserRefreshTokenFetcher> token_fetcher);
  virtual ~ManagedUserRegistrationService();

  static void RegisterUserPrefs(user_prefs::PrefRegistrySyncable* registry);

  // Registers a new managed user with the server. |name| is the display name of
  // the user. |callback| is called with the result of the registration.
  void Register(const string16& name, const RegistrationCallback& callback);

  // Cancels any registration currently in progress and calls the callback with
  // an appropriate error.
  void CancelPendingRegistration();

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

  // Fetches the managed user token when we have the device info.
  void FetchToken(const string16& name,
                  const browser_sync::DeviceInfo& device_info);

  // Called when we have received a token for the managed user.
  void OnReceivedToken(const GoogleServiceAuthError& error,
                       const std::string& token);

  // Dispatches the callback if all the conditions have been met.
  void DispatchCallbackIfReady();

  // Cancels any registration currently in progress and calls the callback
  // specified when Register was called with the given error.
  void CancelPendingRegistrationImpl(const GoogleServiceAuthError& error);

  // Dispatches the callback with the saved token (which may be empty) and the
  // given |error|.
  void DispatchCallback(const GoogleServiceAuthError& error);

  base::WeakPtrFactory<ManagedUserRegistrationService> weak_ptr_factory_;
  PrefService* prefs_;
  PrefChangeRegistrar pref_change_registrar_;
  scoped_ptr<ManagedUserRefreshTokenFetcher> token_fetcher_;

  scoped_ptr<syncer::SyncChangeProcessor> sync_processor_;
  scoped_ptr<syncer::SyncErrorFactory> error_handler_;

  // Provides a timeout during profile creation.
  base::OneShotTimer<ManagedUserRegistrationService> registration_timer_;

  std::string pending_managed_user_id_;
  std::string pending_managed_user_token_;
  bool pending_managed_user_acknowledged_;
  RegistrationCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(ManagedUserRegistrationService);
};

#endif  // CHROME_BROWSER_MANAGED_MODE_MANAGED_USER_REGISTRATION_SERVICE_H_
