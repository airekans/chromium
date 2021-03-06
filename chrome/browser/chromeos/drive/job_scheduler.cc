// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/job_scheduler.h"

#include <math.h>

#include "base/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/logging.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {

namespace {

const int kMaxThrottleCount = 5;

const int kMaxRetryCount = kMaxThrottleCount - 1;

// Parameter struct for RunUploadNewFile.
struct UploadNewFileParams {
  std::string parent_resource_id;
  base::FilePath drive_file_path;
  base::FilePath local_file_path;
  std::string title;
  std::string content_type;
  google_apis::UploadCompletionCallback callback;
  google_apis::ProgressCallback progress_callback;
};

// Helper function to work around the arity limitation of base::Bind.
void RunUploadNewFile(google_apis::DriveUploaderInterface* uploader,
                      const UploadNewFileParams& params) {
  uploader->UploadNewFile(params.parent_resource_id,
                          params.drive_file_path,
                          params.local_file_path,
                          params.title,
                          params.content_type,
                          params.callback,
                          params.progress_callback);
}

// Parameter struct for RunUploadExistingFile.
struct UploadExistingFileParams {
  std::string resource_id;
  base::FilePath drive_file_path;
  base::FilePath local_file_path;
  std::string content_type;
  std::string etag;
  google_apis::UploadCompletionCallback callback;
  google_apis::ProgressCallback progress_callback;
};

// Helper function to work around the arity limitation of base::Bind.
void RunUploadExistingFile(google_apis::DriveUploaderInterface* uploader,
                           const UploadExistingFileParams& params) {
  uploader->UploadExistingFile(params.resource_id,
                               params.drive_file_path,
                               params.local_file_path,
                               params.content_type,
                               params.etag,
                               params.callback,
                               params.progress_callback);
}

}  // namespace

const int JobScheduler::kMaxJobCount[] = {
  5,  // METADATA_QUEUE
  1,  // FILE_QUEUE
};

JobScheduler::JobEntry::JobEntry(JobType type)
    : job_info(type),
      context(ClientContext(USER_INITIATED)),
      retry_count(0) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

JobScheduler::JobEntry::~JobEntry() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

struct JobScheduler::ResumeUploadParams {
  base::FilePath drive_file_path;
  base::FilePath local_file_path;
  std::string content_type;
};

JobScheduler::JobScheduler(
    Profile* profile,
    google_apis::DriveServiceInterface* drive_service)
    : throttle_count_(0),
      disable_throttling_(false),
      drive_service_(drive_service),
      uploader_(new google_apis::DriveUploader(drive_service)),
      profile_(profile),
      weak_ptr_factory_(this) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  for (int i = 0; i < NUM_QUEUES; ++i)
    queue_[i].reset(new JobQueue(kMaxJobCount[i], NUM_CONTEXT_TYPES));

  net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
}

JobScheduler::~JobScheduler() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  size_t num_queued_jobs = 0;
  for (int i = 0; i < NUM_QUEUES; ++i)
    num_queued_jobs += queue_[i]->GetNumberOfJobs();
  DCHECK_EQ(num_queued_jobs, job_map_.size());

  net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
}

std::vector<JobInfo> JobScheduler::GetJobInfoList() {
  std::vector<JobInfo> job_info_list;
  for (JobIDMap::iterator iter(&job_map_); !iter.IsAtEnd(); iter.Advance())
    job_info_list.push_back(iter.GetCurrentValue()->job_info);
  return job_info_list;
}

void JobScheduler::AddObserver(JobListObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observer_list_.AddObserver(observer);
}

void JobScheduler::RemoveObserver(JobListObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observer_list_.RemoveObserver(observer);
}

void JobScheduler::CancelJob(JobID job_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // TODO(kinaba): Move the cancellation feature from DriveService
  // to JobScheduler. In particular, implement cancel based on job_id.
  // crbug.com/231029
  JobEntry* job = job_map_.Lookup(job_id);
  if (job)
    drive_service_->CancelForFilePath(job->job_info.file_path);
}

void JobScheduler::CancelAllJobs() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // TODO(kinaba): Move the cancellation feature from DriveService
  // to JobScheduler.
  drive_service_->CancelAll();
}

void JobScheduler::GetAboutResource(
    const google_apis::GetAboutResourceCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_GET_ABOUT_RESOURCE);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetAboutResource,
      base::Unretained(drive_service_),
      base::Bind(&JobScheduler::OnGetAboutResourceJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::GetAppList(
    const google_apis::GetAppListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_GET_APP_LIST);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetAppList,
      base::Unretained(drive_service_),
      base::Bind(&JobScheduler::OnGetAppListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::GetAllResourceList(
    const google_apis::GetResourceListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_GET_ALL_RESOURCE_LIST);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetAllResourceList,
      base::Unretained(drive_service_),
      base::Bind(&JobScheduler::OnGetResourceListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::GetResourceListInDirectory(
    const std::string& directory_resource_id,
    const google_apis::GetResourceListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(
      TYPE_GET_RESOURCE_LIST_IN_DIRECTORY);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetResourceListInDirectory,
      base::Unretained(drive_service_),
      directory_resource_id,
      base::Bind(&JobScheduler::OnGetResourceListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::Search(
    const std::string& search_query,
    const google_apis::GetResourceListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_SEARCH);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::Search,
      base::Unretained(drive_service_),
      search_query,
      base::Bind(&JobScheduler::OnGetResourceListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::GetChangeList(
    int64 start_changestamp,
    const google_apis::GetResourceListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_GET_CHANGE_LIST);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetChangeList,
      base::Unretained(drive_service_),
      start_changestamp,
      base::Bind(&JobScheduler::OnGetResourceListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::ContinueGetResourceList(
    const GURL& next_url,
    const google_apis::GetResourceListCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_CONTINUE_GET_RESOURCE_LIST);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::ContinueGetResourceList,
      base::Unretained(drive_service_),
      next_url,
      base::Bind(&JobScheduler::OnGetResourceListJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::GetResourceEntry(
    const std::string& resource_id,
    const ClientContext& context,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_GET_RESOURCE_ENTRY);
  new_job->context = context;
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::GetResourceEntry,
      base::Unretained(drive_service_),
      resource_id,
      base::Bind(&JobScheduler::OnGetResourceEntryJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::DeleteResource(
    const std::string& resource_id,
    const google_apis::EntryActionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_DELETE_RESOURCE);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::DeleteResource,
      base::Unretained(drive_service_),
      resource_id,
      "",  // etag
      base::Bind(&JobScheduler::OnEntryActionJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::CopyResource(
    const std::string& resource_id,
    const std::string& parent_resource_id,
    const std::string& new_name,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_COPY_RESOURCE);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::CopyResource,
      base::Unretained(drive_service_),
      resource_id,
      parent_resource_id,
      new_name,
      base::Bind(&JobScheduler::OnGetResourceEntryJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::CopyHostedDocument(
    const std::string& resource_id,
    const std::string& new_name,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_COPY_HOSTED_DOCUMENT);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::CopyHostedDocument,
      base::Unretained(drive_service_),
      resource_id,
      new_name,
      base::Bind(&JobScheduler::OnGetResourceEntryJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::RenameResource(
    const std::string& resource_id,
    const std::string& new_name,
    const google_apis::EntryActionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_RENAME_RESOURCE);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::RenameResource,
      base::Unretained(drive_service_),
      resource_id,
      new_name,
      base::Bind(&JobScheduler::OnEntryActionJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::TouchResource(
    const std::string& resource_id,
    const base::Time& modified_date,
    const base::Time& last_viewed_by_me_date,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_TOUCH_RESOURCE);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::TouchResource,
      base::Unretained(drive_service_),
      resource_id,
      modified_date,
      last_viewed_by_me_date,
      base::Bind(&JobScheduler::OnGetResourceEntryJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::AddResourceToDirectory(
    const std::string& parent_resource_id,
    const std::string& resource_id,
    const google_apis::EntryActionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  JobEntry* new_job = CreateNewJob(TYPE_ADD_RESOURCE_TO_DIRECTORY);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::AddResourceToDirectory,
      base::Unretained(drive_service_),
      parent_resource_id,
      resource_id,
      base::Bind(&JobScheduler::OnEntryActionJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::RemoveResourceFromDirectory(
    const std::string& parent_resource_id,
    const std::string& resource_id,
    const google_apis::EntryActionCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* new_job = CreateNewJob(TYPE_REMOVE_RESOURCE_FROM_DIRECTORY);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::RemoveResourceFromDirectory,
      base::Unretained(drive_service_),
      parent_resource_id,
      resource_id,
      base::Bind(&JobScheduler::OnEntryActionJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

void JobScheduler::AddNewDirectory(
    const std::string& parent_resource_id,
    const std::string& directory_name,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* new_job = CreateNewJob(TYPE_ADD_NEW_DIRECTORY);
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::AddNewDirectory,
      base::Unretained(drive_service_),
      parent_resource_id,
      directory_name,
      base::Bind(&JobScheduler::OnGetResourceEntryJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 callback));
  StartJob(new_job);
}

JobID JobScheduler::DownloadFile(
    const base::FilePath& virtual_path,
    const base::FilePath& local_cache_path,
    const GURL& download_url,
    const ClientContext& context,
    const google_apis::DownloadActionCallback& download_action_callback,
    const google_apis::GetContentCallback& get_content_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* new_job = CreateNewJob(TYPE_DOWNLOAD_FILE);
  new_job->job_info.file_path = virtual_path;
  new_job->context = context;
  new_job->task = base::Bind(
      &google_apis::DriveServiceInterface::DownloadFile,
      base::Unretained(drive_service_),
      virtual_path,
      local_cache_path,
      download_url,
      base::Bind(&JobScheduler::OnDownloadActionJobDone,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id,
                 download_action_callback),
      get_content_callback,
      base::Bind(&JobScheduler::UpdateProgress,
                 weak_ptr_factory_.GetWeakPtr(),
                 new_job->job_info.job_id));

  StartJob(new_job);
  return new_job->job_info.job_id;
}

void JobScheduler::UploadNewFile(
    const std::string& parent_resource_id,
    const base::FilePath& drive_file_path,
    const base::FilePath& local_file_path,
    const std::string& title,
    const std::string& content_type,
    const ClientContext& context,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* new_job = CreateNewJob(TYPE_UPLOAD_NEW_FILE);
  new_job->job_info.file_path = drive_file_path;
  new_job->context = context;

  UploadNewFileParams params;
  params.parent_resource_id = parent_resource_id;
  params.drive_file_path = drive_file_path;
  params.local_file_path = local_file_path;
  params.title = title;
  params.content_type = content_type;

  ResumeUploadParams resume_params;
  resume_params.drive_file_path = params.drive_file_path;
  resume_params.local_file_path = params.local_file_path;
  resume_params.content_type = params.content_type;

  params.callback = base::Bind(&JobScheduler::OnUploadCompletionJobDone,
                               weak_ptr_factory_.GetWeakPtr(),
                               new_job->job_info.job_id,
                               resume_params,
                               callback);
  params.progress_callback = base::Bind(&JobScheduler::UpdateProgress,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        new_job->job_info.job_id);
  new_job->task = base::Bind(&RunUploadNewFile, uploader_.get(), params);

  StartJob(new_job);
}

void JobScheduler::UploadExistingFile(
    const std::string& resource_id,
    const base::FilePath& drive_file_path,
    const base::FilePath& local_file_path,
    const std::string& content_type,
    const std::string& etag,
    const ClientContext& context,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* new_job = CreateNewJob(TYPE_UPLOAD_EXISTING_FILE);
  new_job->job_info.file_path = drive_file_path;
  new_job->context = context;

  UploadExistingFileParams params;
  params.resource_id = resource_id;
  params.drive_file_path = drive_file_path;
  params.local_file_path = local_file_path;
  params.content_type = content_type;
  params.etag = etag;

  ResumeUploadParams resume_params;
  resume_params.drive_file_path = params.drive_file_path;
  resume_params.local_file_path = params.local_file_path;
  resume_params.content_type = params.content_type;

  params.callback = base::Bind(&JobScheduler::OnUploadCompletionJobDone,
                               weak_ptr_factory_.GetWeakPtr(),
                               new_job->job_info.job_id,
                               resume_params,
                               callback);
  params.progress_callback = base::Bind(&JobScheduler::UpdateProgress,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        new_job->job_info.job_id);
  new_job->task = base::Bind(&RunUploadExistingFile, uploader_.get(), params);

  StartJob(new_job);
}

void JobScheduler::CreateFile(
    const std::string& parent_resource_id,
    const base::FilePath& drive_file_path,
    const std::string& title,
    const std::string& content_type,
    const ClientContext& context,
    const google_apis::GetResourceEntryCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const base::FilePath kDevNull(FILE_PATH_LITERAL("/dev/null"));

  JobEntry* new_job = CreateNewJob(TYPE_CREATE_FILE);
  new_job->job_info.file_path = drive_file_path;
  new_job->context = context;

  UploadNewFileParams params;
  params.parent_resource_id = parent_resource_id;
  params.drive_file_path = drive_file_path;
  params.local_file_path = kDevNull;  // Upload an empty file.
  params.title = title;
  params.content_type = content_type;

  ResumeUploadParams resume_params;
  resume_params.drive_file_path = params.drive_file_path;
  resume_params.local_file_path = params.local_file_path;
  resume_params.content_type = params.content_type;

  params.callback = base::Bind(&JobScheduler::OnUploadCompletionJobDone,
                               weak_ptr_factory_.GetWeakPtr(),
                               new_job->job_info.job_id,
                               resume_params,
                               callback);
  params.progress_callback = google_apis::ProgressCallback();

  new_job->task = base::Bind(&RunUploadNewFile, uploader_.get(), params);

  StartJob(new_job);
}

JobScheduler::JobEntry* JobScheduler::CreateNewJob(JobType type) {
  JobEntry* job = new JobEntry(type);
  job->job_info.job_id = job_map_.Add(job);  // Takes the ownership of |job|.
  return job;
}

void JobScheduler::StartJob(JobEntry* job) {
  DCHECK(!job->task.is_null());

  QueueJob(job->job_info.job_id);
  NotifyJobAdded(job->job_info);
  DoJobLoop(GetJobQueueType(job->job_info.job_type));
}

void JobScheduler::QueueJob(JobID job_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* job_entry = job_map_.Lookup(job_id);
  DCHECK(job_entry);
  const JobInfo& job_info = job_entry->job_info;

  QueueType queue_type = GetJobQueueType(job_info.job_type);
  queue_[queue_type]->Push(job_id, job_entry->context.type);

  util::Log("Job queued: %s - %s", job_info.ToString().c_str(),
            GetQueueInfo(queue_type).c_str());
}

void JobScheduler::DoJobLoop(QueueType queue_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobID job_id = -1;
  if (!queue_[queue_type]->PopForRun(GetCurrentAcceptedPriority(queue_type),
                                     &job_id)) {
    return;
  }

  JobEntry* entry = job_map_.Lookup(job_id);
  DCHECK(entry);

  JobInfo* job_info = &entry->job_info;
  job_info->state = STATE_RUNNING;
  job_info->start_time = base::Time::Now();
  NotifyJobUpdated(*job_info);

  entry->task.Run();

  util::Log("Job started: %s - %s",
            job_info->ToString().c_str(),
            GetQueueInfo(queue_type).c_str());
}

int JobScheduler::GetCurrentAcceptedPriority(QueueType queue_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const int kNoJobShouldRun = -1;

  // Should stop if the gdata feature was disabled while running the fetch
  // loop.
  if (profile_->GetPrefs()->GetBoolean(prefs::kDisableDrive))
    return kNoJobShouldRun;

  // Should stop if the network is not online.
  if (net::NetworkChangeNotifier::IsOffline())
    return kNoJobShouldRun;

  // For the file queue, if it is on cellular network, only user initiated
  // operations are allowed to start.
  if (queue_type == FILE_QUEUE &&
      profile_->GetPrefs()->GetBoolean(prefs::kDisableDriveOverCellular) &&
      net::NetworkChangeNotifier::IsConnectionCellular(
          net::NetworkChangeNotifier::GetConnectionType()))
    return USER_INITIATED;

  // Otherwise, every operations including background tasks are allowed.
  return BACKGROUND;
}

void JobScheduler::ThrottleAndContinueJobLoop(QueueType queue_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (throttle_count_ < kMaxThrottleCount)
    throttle_count_++;

  base::TimeDelta delay;
  if (disable_throttling_) {
    delay = base::TimeDelta::FromSeconds(0);
  } else {
    delay =
      base::TimeDelta::FromSeconds(pow(2, throttle_count_ - 1)) +
      base::TimeDelta::FromMilliseconds(base::RandInt(0, 1000));
  }
  VLOG(1) << "Throttling for " << delay.InMillisecondsF();

  const bool posted = base::MessageLoopProxy::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&JobScheduler::DoJobLoop,
                 weak_ptr_factory_.GetWeakPtr(),
                 queue_type),
      delay);
  DCHECK(posted);
}

void JobScheduler::ResetThrottleAndContinueJobLoop(QueueType queue_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Post a task to continue the job loop.  This allows us to finish handling
  // the current job before starting the next one.
  throttle_count_ = 0;
  base::MessageLoopProxy::current()->PostTask(FROM_HERE,
      base::Bind(&JobScheduler::DoJobLoop,
                 weak_ptr_factory_.GetWeakPtr(),
                 queue_type));
}

bool JobScheduler::OnJobDone(JobID job_id, google_apis::GDataErrorCode error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  JobEntry* job_entry = job_map_.Lookup(job_id);
  DCHECK(job_entry);
  JobInfo* job_info = &job_entry->job_info;
  QueueType queue_type = GetJobQueueType(job_info->job_type);
  queue_[queue_type]->MarkFinished(job_id);

  const base::TimeDelta elapsed = base::Time::Now() - job_info->start_time;
  util::Log("Job done: %s => %s (elapsed time: %sms) - %s",
            job_info->ToString().c_str(),
            GDataErrorCodeToString(error).c_str(),
            base::Int64ToString(elapsed.InMilliseconds()).c_str(),
            GetQueueInfo(queue_type).c_str());

  // Retry, depending on the error.
  if ((error == google_apis::HTTP_SERVICE_UNAVAILABLE ||
      error == google_apis::HTTP_INTERNAL_SERVER_ERROR) &&
      job_entry->retry_count < kMaxRetryCount) {
    job_info->state = STATE_RETRY;
    NotifyJobUpdated(*job_info);

    ++job_entry->retry_count;

    // Requeue the job.
    QueueJob(job_id);

    ThrottleAndContinueJobLoop(queue_type);
    return false;
  } else {
    NotifyJobDone(*job_info, error);
    // The job has finished, no retry will happen in the scheduler. Now we can
    // remove the job info from the map. This is the only place of the removal.
    job_map_.Remove(job_id);

    ResetThrottleAndContinueJobLoop(queue_type);
    return true;
  }
}

void JobScheduler::OnGetResourceListJobDone(
    JobID job_id,
    const google_apis::GetResourceListCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceList> resource_list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error, resource_list.Pass());
}

void JobScheduler::OnGetResourceEntryJobDone(
    JobID job_id,
    const google_apis::GetResourceEntryCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error, entry.Pass());
}

void JobScheduler::OnGetAboutResourceJobDone(
    JobID job_id,
    const google_apis::GetAboutResourceCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::AboutResource> about_resource) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error, about_resource.Pass());
}

void JobScheduler::OnGetAppListJobDone(
    JobID job_id,
    const google_apis::GetAppListCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::AppList> app_list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error, app_list.Pass());
}

void JobScheduler::OnEntryActionJobDone(
    JobID job_id,
    const google_apis::EntryActionCallback& callback,
    google_apis::GDataErrorCode error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error);
}

void JobScheduler::OnDownloadActionJobDone(
    JobID job_id,
    const google_apis::DownloadActionCallback& callback,
    google_apis::GDataErrorCode error,
    const base::FilePath& temp_file) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (OnJobDone(job_id, error))
    callback.Run(error, temp_file);
}

void JobScheduler::OnUploadCompletionJobDone(
    JobID job_id,
    const ResumeUploadParams& resume_params,
    const google_apis::GetResourceEntryCallback& callback,
    google_apis::GDataErrorCode error,
    const GURL& upload_location,
    scoped_ptr<google_apis::ResourceEntry> resource_entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());

  if (!upload_location.is_empty()) {
    // If upload_location is available, update the task to resume the
    // upload process from the terminated point.
    // When we need to retry, the error code should be HTTP_SERVICE_UNAVAILABLE
    // so OnJobDone called below will be in charge to re-queue the job.
    JobEntry* job_entry = job_map_.Lookup(job_id);
    DCHECK(job_entry);
    job_entry->task = base::Bind(
        &google_apis::DriveUploaderInterface::ResumeUploadFile,
        base::Unretained(uploader_.get()),
        upload_location,
        resume_params.drive_file_path,
        resume_params.local_file_path,
        resume_params.content_type,
        base::Bind(&JobScheduler::OnUploadCompletionJobDone,
                   weak_ptr_factory_.GetWeakPtr(),
                   job_id,
                   resume_params,
                   callback),
        base::Bind(&JobScheduler::UpdateProgress,
                   weak_ptr_factory_.GetWeakPtr(),
                   job_id));
  }

  if (OnJobDone(job_id, error))
    callback.Run(error, resource_entry.Pass());
}

void JobScheduler::UpdateProgress(JobID job_id, int64 progress, int64 total) {
  JobEntry* job_entry = job_map_.Lookup(job_id);
  DCHECK(job_entry);

  job_entry->job_info.num_completed_bytes = progress;
  job_entry->job_info.num_total_bytes = total;
  NotifyJobUpdated(job_entry->job_info);
}

void JobScheduler::OnConnectionTypeChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Resume the job loop if the network is back online. Note that we don't
  // need to check the type of the network as it will be checked in
  // ShouldStopJobLoop() as soon as the loop is resumed.
  if (!net::NetworkChangeNotifier::IsOffline()) {
    for (int i = METADATA_QUEUE; i < NUM_QUEUES; ++i) {
      DoJobLoop(static_cast<QueueType>(i));
    }
  }
}

JobScheduler::QueueType JobScheduler::GetJobQueueType(JobType type) {
  switch (type) {
    case TYPE_GET_ABOUT_RESOURCE:
    case TYPE_GET_APP_LIST:
    case TYPE_GET_ALL_RESOURCE_LIST:
    case TYPE_GET_RESOURCE_LIST_IN_DIRECTORY:
    case TYPE_SEARCH:
    case TYPE_GET_CHANGE_LIST:
    case TYPE_CONTINUE_GET_RESOURCE_LIST:
    case TYPE_GET_RESOURCE_ENTRY:
    case TYPE_DELETE_RESOURCE:
    case TYPE_COPY_RESOURCE:
    case TYPE_COPY_HOSTED_DOCUMENT:
    case TYPE_RENAME_RESOURCE:
    case TYPE_TOUCH_RESOURCE:
    case TYPE_ADD_RESOURCE_TO_DIRECTORY:
    case TYPE_REMOVE_RESOURCE_FROM_DIRECTORY:
    case TYPE_ADD_NEW_DIRECTORY:
    case TYPE_CREATE_FILE:
      return METADATA_QUEUE;

    case TYPE_DOWNLOAD_FILE:
    case TYPE_UPLOAD_NEW_FILE:
    case TYPE_UPLOAD_EXISTING_FILE:
      return FILE_QUEUE;
  }
  NOTREACHED();
  return FILE_QUEUE;
}

void JobScheduler::NotifyJobAdded(const JobInfo& job_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FOR_EACH_OBSERVER(JobListObserver, observer_list_, OnJobAdded(job_info));
}

void JobScheduler::NotifyJobDone(const JobInfo& job_info,
                                 google_apis::GDataErrorCode error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FOR_EACH_OBSERVER(JobListObserver, observer_list_,
                    OnJobDone(job_info, util::GDataToFileError(error)));
}

void JobScheduler::NotifyJobUpdated(const JobInfo& job_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  FOR_EACH_OBSERVER(JobListObserver, observer_list_, OnJobUpdated(job_info));
}

std::string JobScheduler::GetQueueInfo(QueueType type) const {
  return QueueTypeToString(type) + " " + queue_[type]->ToString();
}

// static
std::string JobScheduler::QueueTypeToString(QueueType type) {
  switch (type) {
    case METADATA_QUEUE:
      return "METADATA_QUEUE";
    case FILE_QUEUE:
      return "FILE_QUEUE";
    case NUM_QUEUES:
      break;  // This value is just a sentinel. Should never be used.
  }
  NOTREACHED();
  return "";
}

}  // namespace drive
