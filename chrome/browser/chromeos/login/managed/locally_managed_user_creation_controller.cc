// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/managed/locally_managed_user_creation_controller.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/chromeos/login/managed/locally_managed_user_constants.h"
#include "chrome/browser/chromeos/login/mount_manager.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/managed_mode/managed_user_registration_service_factory.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager_client.h"
#include "content/public/browser/browser_thread.h"
#include "google_apis/gaia/google_service_auth_error.h"

namespace chromeos {

namespace {

bool StoreManagedUserFiles(const std::string& token,
                           const base::FilePath& base_path) {
  base::FilePath token_file = base_path.Append(kManagedUserTokenFilename);
  int bytes = file_util::WriteFile(token_file, token.c_str(), token.length());
  return bytes >= 0;
}

} // namespace

LocallyManagedUserCreationController::StatusConsumer::~StatusConsumer() {}

LocallyManagedUserCreationController::UserCreationContext::UserCreationContext()
    : token_acquired(false),
      token_succesfully_written(false),
      manager_profile(NULL) {}

LocallyManagedUserCreationController::UserCreationContext::
    ~UserCreationContext() {}

// static
LocallyManagedUserCreationController*
    LocallyManagedUserCreationController::current_controller_ = NULL;

LocallyManagedUserCreationController::LocallyManagedUserCreationController(
    LocallyManagedUserCreationController::StatusConsumer* consumer)
    : consumer_(consumer),
      weak_factory_(this) {
  DCHECK(!current_controller_) << "More than one controller exist.";
  current_controller_ = this;
}

LocallyManagedUserCreationController::~LocallyManagedUserCreationController() {
  current_controller_ = NULL;
}

void LocallyManagedUserCreationController::SetUpCreation(string16 display_name,
                                                         std::string password) {
  DCHECK(creation_context_);
  creation_context_->display_name = display_name;
  creation_context_->password = password;
}

void LocallyManagedUserCreationController::SetManagerProfile(
    Profile* manager_profile) {
  creation_context_.reset(
      new LocallyManagedUserCreationController::UserCreationContext());
  creation_context_->manager_profile = manager_profile;
}

void LocallyManagedUserCreationController::StartCreation() {
  DCHECK(creation_context_);
  UserManager::Get()->StartLocallyManagedUserCreationTransaction(
      creation_context_->display_name);

  std::string new_id = UserManager::Get()->GenerateUniqueLocallyManagedUserId();

  const User* user = UserManager::Get()->CreateLocallyManagedUserRecord(
      new_id, creation_context_->display_name);

  creation_context_->user_id = user->email();

  UserManager::Get()->SetLocallyManagedUserCreationTransactionUserId(
      creation_context_->user_id);

  authenticator_ = new ManagedUserAuthenticator(this);
  authenticator_->AuthenticateToCreate(user->email(),
                                       creation_context_->password);
}

void LocallyManagedUserCreationController::OnAuthenticationFailure(
    ManagedUserAuthenticator::AuthState error) {
  ErrorCode code = NO_ERROR;
  switch (error) {
    case ManagedUserAuthenticator::NO_MOUNT:
      code = CRYPTOHOME_NO_MOUNT;
      break;
    case ManagedUserAuthenticator::FAILED_MOUNT:
      code = CRYPTOHOME_FAILED_MOUNT;
      break;
    case ManagedUserAuthenticator::FAILED_TPM:
      code = CRYPTOHOME_FAILED_TPM;
      break;
    default:
      NOTREACHED();
  }
  if (consumer_)
    consumer_->OnCreationError(code);
}

void LocallyManagedUserCreationController::OnMountSuccess(
    const std::string& mount_hash) {
  creation_context_->mount_hash = mount_hash;

  ManagedUserRegistrationService* service =
      ManagedUserRegistrationServiceFactory::GetForProfile(
          creation_context_->manager_profile);

  service->Register(
      creation_context_->display_name,
      base::Bind(&LocallyManagedUserCreationController::RegistrationCallback,
                 weak_factory_.GetWeakPtr()));
}

void LocallyManagedUserCreationController::RegistrationCallback(
    const GoogleServiceAuthError& error,
    const std::string& token) {
  if (error.state() == GoogleServiceAuthError::NONE) {
    TokenFetched(token);
  } else {
    if (consumer_)
      consumer_->OnCreationError(CLOUD_SERVER_ERROR);
  }
}

void LocallyManagedUserCreationController::FinishCreation() {
  chrome::AttemptUserExit();
}

std::string LocallyManagedUserCreationController::GetManagedUserId() {
  DCHECK(creation_context_);
  return creation_context_->user_id;
}

void LocallyManagedUserCreationController::TokenFetched(
    const std::string& token) {
  creation_context_->token_acquired = true;
  creation_context_->token = token;

  PostTaskAndReplyWithResult(
      content::BrowserThread::GetBlockingPool(),
      FROM_HERE,
      base::Bind(&StoreManagedUserFiles,
                 creation_context_->token,
                 MountManager::GetHomeDir(creation_context_->mount_hash)),
      base::Bind(
           &LocallyManagedUserCreationController::OnManagedUserFilesStored,
           weak_factory_.GetWeakPtr()));
}

void LocallyManagedUserCreationController::OnManagedUserFilesStored(
    bool success) {
  if (!success) {
    if (consumer_)
      consumer_->OnCreationError(TOKEN_WRITE_FAILED);
    return;
  }
  UserManager::Get()->CommitLocallyManagedUserCreationTransaction();
  if (consumer_)
    consumer_->OnCreationSuccess();
}

}  // namespace chromeos
