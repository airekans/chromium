// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/update_screen.h"

#include <algorithm>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/chromeos/login/screen_observer.h"
#include "chrome/browser/chromeos/login/update_screen_actor.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace chromeos {

namespace {

// Progress bar stages. Each represents progress bar value
// at the beginning of each stage.
// TODO(nkostylev): Base stage progress values on approximate time.
// TODO(nkostylev): Animate progress during each state.
const int kBeforeUpdateCheckProgress = 7;
const int kBeforeDownloadProgress = 14;
const int kBeforeVerifyingProgress = 74;
const int kBeforeFinalizingProgress = 81;
const int kProgressComplete = 100;

// Defines what part of update progress does download part takes.
const int kDownloadProgressIncrement = 60;

// Considering 10px shadow from each side.
const int kUpdateScreenWidth = 580;
const int kUpdateScreenHeight = 305;

const char kUpdateDeadlineFile[] = "/tmp/update-check-response-deadline";

// Minimum timestep between two consecutive measurements for the
// download rate.
const base::TimeDelta kMinTimeStep = base::TimeDelta::FromSeconds(1);

// Minimum allowed progress between two consecutive ETAs.
const double kMinProgressStep = 1e-3;

// Smooth factor that is used for the average downloading speed
// estimation.
const double kDownloadSpeedSmoothFactor = 0.005;

// Minumum allowed value for the average downloading speed.
const double kDownloadAverageSpeedDropBound = 1e-8;

// An upper bound for possible downloading time left estimations.
const double kMaxTimeLeft = 24 * 60 * 60;

// Invoked from call to RequestUpdateCheck upon completion of the DBus call.
void StartUpdateCallback(UpdateScreen* screen,
                         UpdateEngineClient::UpdateCheckResult result) {
  VLOG(1) << "Callback from RequestUpdateCheck, result " << result;
  if (UpdateScreen::HasInstance(screen)) {
    if (result == UpdateEngineClient::UPDATE_RESULT_SUCCESS)
      screen->SetIgnoreIdleStatus(false);
    else
      screen->ExitUpdate(UpdateScreen::REASON_UPDATE_INIT_FAILED);
  }
}

}  // anonymous namespace

// static
UpdateScreen::InstanceSet& UpdateScreen::GetInstanceSet() {
  CR_DEFINE_STATIC_LOCAL(std::set<UpdateScreen*>, instance_set, ());
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));  // not threadsafe.
  return instance_set;
}

// static
bool UpdateScreen::HasInstance(UpdateScreen* inst) {
  InstanceSet& instance_set = GetInstanceSet();
  InstanceSet::iterator found = instance_set.find(inst);
  return (found != instance_set.end());
}

UpdateScreen::UpdateScreen(ScreenObserver* screen_observer,
                           UpdateScreenActor* actor)
    : WizardScreen(screen_observer),
      reboot_check_delay_(0),
      is_checking_for_update_(true),
      is_downloading_update_(false),
      is_ignore_update_deadlines_(false),
      is_shown_(false),
      ignore_idle_status_(true),
      actor_(actor) {
  actor_->SetDelegate(this);
  GetInstanceSet().insert(this);
}

UpdateScreen::~UpdateScreen() {
  DBusThreadManager::Get()->GetUpdateEngineClient()->RemoveObserver(this);
  GetInstanceSet().erase(this);
  if (actor_)
    actor_->SetDelegate(NULL);
}

void UpdateScreen::UpdateStatusChanged(
    const UpdateEngineClient::Status& status) {
  if (is_checking_for_update_ &&
      status.status > UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE) {
    is_checking_for_update_ = false;
  }
  if (ignore_idle_status_ && status.status >
      UpdateEngineClient::UPDATE_STATUS_IDLE) {
    ignore_idle_status_ = false;
  }

  switch (status.status) {
    case UpdateEngineClient::UPDATE_STATUS_CHECKING_FOR_UPDATE:
      // Do nothing in these cases, we don't want to notify the user of the
      // check unless there is an update.
      break;
    case UpdateEngineClient::UPDATE_STATUS_UPDATE_AVAILABLE:
      MakeSureScreenIsShown();
      actor_->SetProgress(kBeforeDownloadProgress);
      actor_->ShowEstimatedTimeLeft(false);
      if (!HasCriticalUpdate()) {
        LOG(INFO) << "Noncritical update available: "
                  << status.new_version;
        ExitUpdate(REASON_UPDATE_NON_CRITICAL);
      } else {
        LOG(INFO) << "Critical update available: "
                  << status.new_version;
        actor_->ShowPreparingUpdatesInfo(true);
        actor_->ShowCurtain(false);
      }
      break;
    case UpdateEngineClient::UPDATE_STATUS_DOWNLOADING:
      {
        MakeSureScreenIsShown();
        if (!is_downloading_update_) {
          // Because update engine doesn't send UPDATE_STATUS_UPDATE_AVAILABLE
          // we need to is update critical on first downloading notification.
          is_downloading_update_ = true;
          download_start_time_ = download_last_time_ = base::Time::Now();
          download_start_progress_ = status.download_progress;
          download_last_progress_ = status.download_progress;
          is_download_average_speed_computed_ = false;
          download_average_speed_ = 0.0;
          if (!HasCriticalUpdate()) {
            LOG(INFO) << "Non-critical update available: "
                      << status.new_version;
            ExitUpdate(REASON_UPDATE_NON_CRITICAL);
          } else {
            LOG(INFO) << "Critical update available: "
                      << status.new_version;
            actor_->ShowPreparingUpdatesInfo(false);
            actor_->ShowCurtain(false);
          }
        }
        UpdateDownloadingStats(status);
      }
      break;
    case UpdateEngineClient::UPDATE_STATUS_VERIFYING:
      MakeSureScreenIsShown();
      actor_->SetProgress(kBeforeVerifyingProgress);
      actor_->ShowEstimatedTimeLeft(false);
      break;
    case UpdateEngineClient::UPDATE_STATUS_FINALIZING:
      MakeSureScreenIsShown();
      actor_->SetProgress(kBeforeFinalizingProgress);
      actor_->ShowEstimatedTimeLeft(false);
      break;
    case UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT:
      MakeSureScreenIsShown();
      // Make sure that first OOBE stage won't be shown after reboot.
      WizardController::MarkOobeCompleted();
      actor_->SetProgress(kProgressComplete);
      actor_->ShowEstimatedTimeLeft(false);
      if (HasCriticalUpdate()) {
        actor_->ShowCurtain(false);
        VLOG(1) << "Initiate reboot after update";
        DBusThreadManager::Get()->GetUpdateEngineClient()->RebootAfterUpdate();
        reboot_timer_.Start(FROM_HERE,
                            base::TimeDelta::FromSeconds(reboot_check_delay_),
                            this,
                            &UpdateScreen::OnWaitForRebootTimeElapsed);
      } else {
        ExitUpdate(REASON_UPDATE_NON_CRITICAL);
      }
      break;
    case UpdateEngineClient::UPDATE_STATUS_IDLE:
      if (ignore_idle_status_) {
        // It is first IDLE status that is sent before we initiated the check.
        break;
      }
      // else no break

    case UpdateEngineClient::UPDATE_STATUS_ERROR:
    case UpdateEngineClient::UPDATE_STATUS_REPORTING_ERROR_EVENT:
      ExitUpdate(REASON_UPDATE_ENDED);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void UpdateScreen::StartUpdate() {
  DBusThreadManager::Get()->GetUpdateEngineClient()->AddObserver(this);
  VLOG(1) << "Initiate update check";
  DBusThreadManager::Get()->GetUpdateEngineClient()->RequestUpdateCheck(
      base::Bind(StartUpdateCallback, this));
}

void UpdateScreen::CancelUpdate() {
  VLOG(1) << "Forced update cancel";
  ExitUpdate(REASON_UPDATE_CANCELED);
}

void UpdateScreen::Show() {
  is_shown_ = true;
  actor_->Show();
  actor_->SetProgress(kBeforeUpdateCheckProgress);
}

void UpdateScreen::Hide() {
  actor_->Hide();
  is_shown_ = false;
}

std::string UpdateScreen::GetName() const {
  return WizardController::kUpdateScreenName;
}

void UpdateScreen::PrepareToShow() {
  actor_->PrepareToShow();
}

void UpdateScreen::ExitUpdate(UpdateScreen::ExitReason reason) {
  DBusThreadManager::Get()->GetUpdateEngineClient()->RemoveObserver(this);

  switch (reason) {
    case REASON_UPDATE_CANCELED:
      get_screen_observer()->OnExit(ScreenObserver::UPDATE_NOUPDATE);
      break;
    case REASON_UPDATE_INIT_FAILED:
      get_screen_observer()->OnExit(
          ScreenObserver::UPDATE_ERROR_CHECKING_FOR_UPDATE);
      break;
    case REASON_UPDATE_NON_CRITICAL:
    case REASON_UPDATE_ENDED:
      {
        UpdateEngineClient* update_engine_client =
            DBusThreadManager::Get()->GetUpdateEngineClient();
        switch (update_engine_client->GetLastStatus().status) {
          case UpdateEngineClient::UPDATE_STATUS_UPDATE_AVAILABLE:
          case UpdateEngineClient::UPDATE_STATUS_UPDATED_NEED_REBOOT:
          case UpdateEngineClient::UPDATE_STATUS_DOWNLOADING:
          case UpdateEngineClient::UPDATE_STATUS_FINALIZING:
          case UpdateEngineClient::UPDATE_STATUS_VERIFYING:
            DCHECK(!HasCriticalUpdate());
            // Noncritical update, just exit screen as if there is no update.
            // no break
          case UpdateEngineClient::UPDATE_STATUS_IDLE:
            get_screen_observer()->OnExit(ScreenObserver::UPDATE_NOUPDATE);
            break;
          case UpdateEngineClient::UPDATE_STATUS_ERROR:
          case UpdateEngineClient::UPDATE_STATUS_REPORTING_ERROR_EVENT:
            get_screen_observer()->OnExit(is_checking_for_update_ ?
                ScreenObserver::UPDATE_ERROR_CHECKING_FOR_UPDATE :
                ScreenObserver::UPDATE_ERROR_UPDATING);
            break;
          default:
            NOTREACHED();
        }
      }
      break;
    default:
      NOTREACHED();
  }
}

void UpdateScreen::OnWaitForRebootTimeElapsed() {
  LOG(ERROR) << "Unable to reboot - asking user for a manual reboot.";
  MakeSureScreenIsShown();
  actor_->ShowManualRebootInfo();
}

void UpdateScreen::MakeSureScreenIsShown() {
  if (!is_shown_)
    get_screen_observer()->ShowCurrentScreen();
}

void UpdateScreen::SetRebootCheckDelay(int seconds) {
  if (seconds <= 0)
    reboot_timer_.Stop();
  DCHECK(!reboot_timer_.IsRunning());
  reboot_check_delay_ = seconds;
}

void UpdateScreen::SetIgnoreIdleStatus(bool ignore_idle_status) {
  ignore_idle_status_ = ignore_idle_status;
}

void UpdateScreen::UpdateDownloadingStats(
    const UpdateEngineClient::Status& status) {
  base::Time download_current_time = base::Time::Now();
  if (download_current_time >= download_last_time_ + kMinTimeStep &&
      status.download_progress >=
      download_last_progress_ + kMinProgressStep) {
    // Estimate downloading rate.
    double progress_delta =
        std::max(status.download_progress - download_last_progress_, 0.0);
    double time_delta =
        (download_current_time - download_last_time_).InSecondsF();
    double download_rate = status.new_size * progress_delta / time_delta;

    download_last_time_ = download_current_time;
    download_last_progress_ = status.download_progress;

    // Estimate time left.
    double progress_left = std::max(1.0 - status.download_progress, 0.0);
    if (!is_download_average_speed_computed_) {
      download_average_speed_ = download_rate;
      is_download_average_speed_computed_ = true;
    }
    download_average_speed_ =
        kDownloadSpeedSmoothFactor * download_rate +
        (1.0 - kDownloadSpeedSmoothFactor) * download_average_speed_;
    if (download_average_speed_ < kDownloadAverageSpeedDropBound) {
      time_delta =
          (download_current_time - download_start_time_).InSecondsF();
      download_average_speed_ =
          status.new_size *
          (status.download_progress - download_start_progress_) /
          time_delta;
    }
    double work_left = progress_left * status.new_size;
    double time_left = work_left / download_average_speed_;
    // |time_left| may be large enough or even +infinity. So we must
    // |bound possible estimations.
    time_left = std::min(time_left, kMaxTimeLeft);

    actor_->ShowEstimatedTimeLeft(true);
    actor_->SetEstimatedTimeLeft(
        base::TimeDelta::FromSeconds(static_cast<int64>(time_left)));
  }

  int download_progress = static_cast<int>(
      status.download_progress * kDownloadProgressIncrement);
  actor_->SetProgress(kBeforeDownloadProgress + download_progress);
}

bool UpdateScreen::HasCriticalUpdate() {
  if (is_ignore_update_deadlines_)
    return true;

  std::string deadline;
  // Checking for update flag file causes us to do blocking IO on UI thread.
  // Temporarily allow it until we fix http://crosbug.com/11106
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  FilePath update_deadline_file_path(kUpdateDeadlineFile);
  if (!file_util::ReadFileToString(update_deadline_file_path, &deadline) ||
      deadline.empty()) {
    return false;
  }

  // TODO(dpolukhin): Analyze file content. Now we can just assume that
  // if the file exists and not empty, there is critical update.
  return true;
}

void UpdateScreen::OnActorDestroyed(UpdateScreenActor* actor) {
  if (actor_ == actor)
    actor_ = NULL;
}

}  // namespace chromeos
