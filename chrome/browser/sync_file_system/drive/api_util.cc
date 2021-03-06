// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive/api_util.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/drive_api_service.h"
#include "chrome/browser/google_apis/drive_uploader.h"
#include "chrome/browser/google_apis/gdata_wapi_service.h"
#include "chrome/browser/google_apis/gdata_wapi_url_generator.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync_file_system/drive_file_sync_util.h"
#include "chrome/browser/sync_file_system/logger.h"
#include "chrome/common/extensions/extension.h"
#include "extensions/common/constants.h"
#include "net/base/escape.h"
#include "net/base/mime_util.h"
#include "webkit/browser/fileapi/syncable/syncable_file_system_util.h"

namespace sync_file_system {
namespace drive {

namespace {

enum ParentType {
  PARENT_TYPE_ROOT_OR_EMPTY,
  PARENT_TYPE_DIRECTORY,
};

const char kSyncRootDirectoryName[] = "Chrome Syncable FileSystem";
const char kSyncRootDirectoryNameDev[] = "Chrome Syncable FileSystem Dev";
const char kMimeTypeOctetStream[] = "application/octet-stream";

// This path is not actually used but is required by DriveUploaderInterface.
const base::FilePath::CharType kDummyDrivePath[] =
    FILE_PATH_LITERAL("/dummy/drive/path");

void EmptyGDataErrorCodeCallback(google_apis::GDataErrorCode error) {}

bool HasParentLinkTo(const ScopedVector<google_apis::Link>& links,
                     GURL parent_link,
                     ParentType parent_type) {
  bool has_parent = false;

  for (ScopedVector<google_apis::Link>::const_iterator itr = links.begin();
       itr != links.end(); ++itr) {
    if ((*itr)->type() == google_apis::Link::LINK_PARENT) {
      has_parent = true;
      if ((*itr)->href().GetOrigin() == parent_link.GetOrigin() &&
          (*itr)->href().path() == parent_link.path())
        return true;
    }
  }

  return parent_type == PARENT_TYPE_ROOT_OR_EMPTY && !has_parent;
}

struct TitleAndParentQuery
    : std::unary_function<const google_apis::ResourceEntry*, bool> {
  TitleAndParentQuery(const std::string& title,
                      const GURL& parent_link,
                      ParentType parent_type)
      : title(title), parent_link(parent_link), parent_type(parent_type) {}

  bool operator()(const google_apis::ResourceEntry* entry) const {
    return entry->title() == title &&
           HasParentLinkTo(entry->links(), parent_link, parent_type);
  }

  const std::string& title;
  const GURL& parent_link;
  ParentType parent_type;
};

void FilterEntriesByTitleAndParent(
    ScopedVector<google_apis::ResourceEntry>* entries,
    const std::string& title,
    const GURL& parent_link,
    ParentType parent_type) {
  typedef ScopedVector<google_apis::ResourceEntry>::iterator iterator;
  iterator itr =
      std::partition(entries->begin(),
                     entries->end(),
                     TitleAndParentQuery(title, parent_link, parent_type));
  entries->erase(itr, entries->end());
}

google_apis::ResourceEntry* GetDocumentByTitleAndParent(
    const ScopedVector<google_apis::ResourceEntry>& entries,
    const std::string& title,
    const GURL& parent_link,
    ParentType parent_type) {
  typedef ScopedVector<google_apis::ResourceEntry>::const_iterator iterator;
  iterator found =
      std::find_if(entries.begin(),
                   entries.end(),
                   TitleAndParentQuery(title, parent_link, parent_type));
  if (found != entries.end())
    return *found;
  return NULL;
}

void EntryAdapterForEnsureTitleUniqueness(
    scoped_ptr<google_apis::ResourceEntry> entry,
    const APIUtil::EnsureUniquenessCallback& callback,
    APIUtil::EnsureUniquenessStatus status,
    google_apis::GDataErrorCode error) {
  callback.Run(error, status, entry.Pass());
}

void UploadResultAdapter(const APIUtil::ResourceEntryCallback& callback,
                         google_apis::GDataErrorCode error,
                         const GURL& upload_location,
                         scoped_ptr<google_apis::ResourceEntry> entry) {
  callback.Run(error, entry.Pass());
}

}  // namespace

APIUtil::APIUtil(Profile* profile)
    : wapi_url_generator_(
          GURL(google_apis::GDataWapiUrlGenerator::kBaseUrlForProduction)),
      drive_api_url_generator_(
          GURL(google_apis::DriveApiUrlGenerator::kBaseUrlForProduction)),
      upload_next_key_(0) {
  if (IsDriveAPIEnabled()) {
    drive_service_.reset(new google_apis::DriveAPIService(
        profile->GetRequestContext(),
        GURL(google_apis::DriveApiUrlGenerator::kBaseUrlForProduction),
        std::string() /* custom_user_agent */));
  } else {
    drive_service_.reset(new google_apis::GDataWapiService(
        profile->GetRequestContext(),
        GURL(google_apis::GDataWapiUrlGenerator::kBaseUrlForProduction),
        std::string() /* custom_user_agent */));
  }

  drive_service_->Initialize(profile);
  drive_service_->AddObserver(this);
  net::NetworkChangeNotifier::AddConnectionTypeObserver(this);

  drive_uploader_.reset(new google_apis::DriveUploader(drive_service_.get()));
}

scoped_ptr<APIUtil> APIUtil::CreateForTesting(
    Profile* profile,
    const GURL& base_url,
    scoped_ptr<google_apis::DriveServiceInterface> drive_service,
    scoped_ptr<google_apis::DriveUploaderInterface> drive_uploader) {
  return make_scoped_ptr(new APIUtil(
      profile, base_url, drive_service.Pass(), drive_uploader.Pass()));
}

APIUtil::APIUtil(Profile* profile,
                 const GURL& base_url,
                 scoped_ptr<google_apis::DriveServiceInterface> drive_service,
                 scoped_ptr<google_apis::DriveUploaderInterface> drive_uploader)
    : wapi_url_generator_(base_url),
      drive_api_url_generator_(base_url),
      upload_next_key_(0) {
  drive_service_ = drive_service.Pass();
  drive_service_->Initialize(profile);
  drive_service_->AddObserver(this);
  net::NetworkChangeNotifier::AddConnectionTypeObserver(this);

  drive_uploader_ = drive_uploader.Pass();
}

APIUtil::~APIUtil() {
  DCHECK(CalledOnValidThread());
  net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
  drive_service_->RemoveObserver(this);
  drive_service_->CancelAll();
}

void APIUtil::AddObserver(APIUtilObserver* observer) {
  DCHECK(CalledOnValidThread());
  observers_.AddObserver(observer);
}

void APIUtil::RemoveObserver(APIUtilObserver* observer) {
  DCHECK(CalledOnValidThread());
  observers_.RemoveObserver(observer);
}

void APIUtil::GetDriveRootResourceId(const GDataErrorCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(IsDriveAPIEnabled());
  DVLOG(2) << "Getting resource id for Drive root";

  drive_service_->GetAboutResource(
      base::Bind(&APIUtil::DidGetDriveRootResourceId, AsWeakPtr(), callback));
}

void APIUtil::DidGetDriveRootResourceId(
    const GDataErrorCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::AboutResource> about_resource) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting resource id for Drive root: " << error;
    callback.Run(error);
    return;
  }

  DCHECK(about_resource);
  root_resource_id_ = about_resource->root_folder_id();
  DCHECK(!root_resource_id_.empty());
  DVLOG(2) << "Got resource id for Drive root: " << root_resource_id_;
  callback.Run(error);
}

void APIUtil::GetDriveDirectoryForSyncRoot(const ResourceIdCallback& callback) {
  DCHECK(CalledOnValidThread());

  if (GetRootResourceId().empty()) {
    GetDriveRootResourceId(
        base::Bind(&APIUtil::DidGetDriveRootResourceIdForGetSyncRoot,
                                      AsWeakPtr(), callback));
    return;
  }

  DVLOG(2) << "Getting Drive directory for SyncRoot";
  std::string directory_name(GetSyncRootDirectoryName());
  SearchByTitle(directory_name,
                std::string(),
                base::Bind(&APIUtil::DidGetDirectory,
                           AsWeakPtr(),
                           std::string(),
                           directory_name,
                           callback));
}

void APIUtil::DidGetDriveRootResourceIdForGetSyncRoot(
    const ResourceIdCallback& callback,
    google_apis::GDataErrorCode error) {
  DCHECK(CalledOnValidThread());
  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting Drive directory for SyncRoot: " << error;
    callback.Run(error, std::string());
    return;
  }
  GetDriveDirectoryForSyncRoot(callback);
}

void APIUtil::GetDriveDirectoryForOrigin(
    const std::string& sync_root_resource_id,
    const GURL& origin,
    const ResourceIdCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Getting Drive directory for Origin: " << origin;

  std::string directory_name(OriginToDirectoryTitle(origin));
  SearchByTitle(directory_name,
                sync_root_resource_id,
                base::Bind(&APIUtil::DidGetDirectory,
                           AsWeakPtr(),
                           sync_root_resource_id,
                           directory_name,
                           callback));
}

void APIUtil::DidGetDirectory(const std::string& parent_resource_id,
                              const std::string& directory_name,
                              const ResourceIdCallback& callback,
                              google_apis::GDataErrorCode error,
                              scoped_ptr<google_apis::ResourceList> feed) {
  DCHECK(CalledOnValidThread());
  DCHECK(IsStringASCII(directory_name));

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting Drive directory: " << error;
    callback.Run(error, std::string());
    return;
  }

  GURL parent_link;
  ParentType parent_type = PARENT_TYPE_DIRECTORY;
  if (parent_resource_id.empty()) {
    parent_link = ResourceIdToResourceLink(GetRootResourceId());
    parent_type = PARENT_TYPE_ROOT_OR_EMPTY;
  } else {
    parent_link = ResourceIdToResourceLink(parent_resource_id);
  }
  std::string title(directory_name);
  google_apis::ResourceEntry* entry = GetDocumentByTitleAndParent(
      feed->entries(), title, parent_link, parent_type);
  if (!entry) {
    DVLOG(2) << "Directory not found. Creating: " << directory_name;

    // If the |parent_resource_id| is empty, create a directory under the root
    // directory. So here we use the result of GetRootResourceId() for such a
    // case.
    std::string resource_id =
        parent_type == PARENT_TYPE_ROOT_OR_EMPTY ? GetRootResourceId()
                                                 : parent_resource_id;
    drive_service_->AddNewDirectory(resource_id,
                                    directory_name,
                                    base::Bind(&APIUtil::DidCreateDirectory,
                                               AsWeakPtr(),
                                               parent_resource_id,
                                               title,
                                               callback));
    return;
  }
  DVLOG(2) << "Found Drive directory.";

  // TODO(tzik): Handle error.
  DCHECK_EQ(google_apis::ENTRY_KIND_FOLDER, entry->kind());
  DCHECK_EQ(directory_name, entry->title());

  if (entry->title() == GetSyncRootDirectoryName())
    EnsureSyncRootIsNotInMyDrive(entry->resource_id());

  callback.Run(error, entry->resource_id());
}

void APIUtil::DidCreateDirectory(const std::string& parent_resource_id,
                                 const std::string& title,
                                 const ResourceIdCallback& callback,
                                 google_apis::GDataErrorCode error,
                                 scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS &&
      error != google_apis::HTTP_CREATED) {
    DVLOG(2) << "Error on creating Drive directory: " << error;
    callback.Run(error, std::string());
    return;
  }
  DVLOG(2) << "Created Drive directory.";

  DCHECK(entry);
  // Check if any other client creates a directory with same title.
  EnsureTitleUniqueness(
      parent_resource_id,
      title,
      base::Bind(&APIUtil::DidEnsureUniquenessForCreateDirectory,
                 AsWeakPtr(),
                 callback));
}

void APIUtil::DidEnsureUniquenessForCreateDirectory(
    const ResourceIdCallback& callback,
    google_apis::GDataErrorCode error,
    EnsureUniquenessStatus status,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    callback.Run(error, std::string());
    return;
  }

  if (status == NO_DUPLICATES_FOUND)
    error = google_apis::HTTP_CREATED;

  DCHECK(entry) << "No entry: " << error;

  if (!entry->is_folder()) {
    // TODO(kinuko): Fix this. http://crbug.com/237090
    util::Log(
        logging::LOG_ERROR,
        FROM_HERE,
        "A file is left for CreateDirectory due to file-folder conflict!");
    callback.Run(google_apis::HTTP_CONFLICT, std::string());
    return;
  }

  if (entry->title() == GetSyncRootDirectoryName())
    EnsureSyncRootIsNotInMyDrive(entry->resource_id());

  callback.Run(error, entry->resource_id());
}

void APIUtil::GetLargestChangeStamp(const ChangeStampCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Getting largest change id";

  drive_service_->GetAboutResource(
      base::Bind(&APIUtil::DidGetLargestChangeStamp, AsWeakPtr(), callback));
}

void APIUtil::GetResourceEntry(const std::string& resource_id,
                               const ResourceEntryCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Getting ResourceEntry for: " << resource_id;

  drive_service_->GetResourceEntry(
      resource_id,
      base::Bind(&APIUtil::DidGetResourceEntry, AsWeakPtr(), callback));
}

void APIUtil::DidGetLargestChangeStamp(
    const ChangeStampCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::AboutResource> about_resource) {
  DCHECK(CalledOnValidThread());

  int64 largest_change_id = 0;
  if (error == google_apis::HTTP_SUCCESS) {
    DCHECK(about_resource);
    largest_change_id = about_resource->largest_change_id();
    root_resource_id_ = about_resource->root_folder_id();
    DVLOG(2) << "Got largest change id: " << largest_change_id;
  } else {
    DVLOG(2) << "Error on getting largest change id: " << error;
  }

  callback.Run(error, largest_change_id);
}

void APIUtil::SearchByTitle(const std::string& title,
                            const std::string& directory_resource_id,
                            const ResourceListCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!title.empty());
  DVLOG(2) << "Searching resources in the directory [" << directory_resource_id
           << "] with title [" << title << "]";

  drive_service_->SearchByTitle(
      title,
      directory_resource_id,
      base::Bind(&APIUtil::DidGetResourceList, AsWeakPtr(), callback));
}

void APIUtil::ListFiles(const std::string& directory_resource_id,
                        const ResourceListCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Listing resources in the directory [" << directory_resource_id
           << "]";

  drive_service_->GetResourceListInDirectory(directory_resource_id, callback);
}

void APIUtil::ListChanges(int64 start_changestamp,
                          const ResourceListCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Listing changes since: " << start_changestamp;

  drive_service_->GetChangeList(
      start_changestamp,
      base::Bind(&APIUtil::DidGetResourceList, AsWeakPtr(), callback));
}

void APIUtil::ContinueListing(const GURL& feed_url,
                              const ResourceListCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Continue listing on feed: " << feed_url;

  drive_service_->ContinueGetResourceList(
      feed_url,
      base::Bind(&APIUtil::DidGetResourceList, AsWeakPtr(), callback));
}

void APIUtil::DownloadFile(const std::string& resource_id,
                           const std::string& local_file_md5,
                           const base::FilePath& local_file_path,
                           const DownloadFileCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Downloading file [" << resource_id << "]";

  drive_service_->GetResourceEntry(
      resource_id,
      base::Bind(&APIUtil::DidGetResourceEntry,
                 AsWeakPtr(),
                 base::Bind(&APIUtil::DownloadFileInternal,
                            AsWeakPtr(),
                            local_file_md5,
                            local_file_path,
                            callback)));
}

void APIUtil::UploadNewFile(const std::string& directory_resource_id,
                            const base::FilePath& local_file_path,
                            const std::string& title,
                            const UploadFileCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Uploading new file into the directory [" << directory_resource_id
           << "] with title [" << title << "]";

  std::string mime_type;
  if (!net::GetWellKnownMimeTypeFromExtension(local_file_path.Extension(),
                                              &mime_type))
    mime_type = kMimeTypeOctetStream;

  UploadKey upload_key = RegisterUploadCallback(callback);
  ResourceEntryCallback did_upload_callback =
      base::Bind(&APIUtil::DidUploadNewFile,
                 AsWeakPtr(),
                 directory_resource_id,
                 title,
                 upload_key);
  drive_uploader_->UploadNewFile(
      directory_resource_id,
      base::FilePath(kDummyDrivePath),
      local_file_path,
      title,
      mime_type,
      base::Bind(&UploadResultAdapter, did_upload_callback),
      google_apis::ProgressCallback());
}

void APIUtil::UploadExistingFile(const std::string& resource_id,
                                 const std::string& remote_file_md5,
                                 const base::FilePath& local_file_path,
                                 const UploadFileCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Uploading existing file [" << resource_id << "]";
  drive_service_->GetResourceEntry(
      resource_id,
      base::Bind(&APIUtil::DidGetResourceEntry,
                 AsWeakPtr(),
                 base::Bind(&APIUtil::UploadExistingFileInternal,
                            AsWeakPtr(),
                            remote_file_md5,
                            local_file_path,
                            callback)));
}

void APIUtil::CreateDirectory(const std::string& parent_resource_id,
                              const std::string& title,
                              const ResourceIdCallback& callback) {
  DCHECK(CalledOnValidThread());
  // TODO(kinuko): This will call EnsureTitleUniqueness and will delete
  // directories if there're duplicated directories. This must be ok
  // for current design but we'll need to merge directories when we support
  // 'real' directories.
  drive_service_->AddNewDirectory(parent_resource_id,
                                  title,
                                  base::Bind(&APIUtil::DidCreateDirectory,
                                             AsWeakPtr(),
                                             parent_resource_id,
                                             title,
                                             callback));
}

void APIUtil::DeleteFile(const std::string& resource_id,
                         const std::string& remote_file_md5,
                         const GDataErrorCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Deleting file: " << resource_id;

  // Load actual remote_file_md5 to check for conflict before deletion.
  if (!remote_file_md5.empty()) {
    drive_service_->GetResourceEntry(
        resource_id,
        base::Bind(&APIUtil::DidGetResourceEntry,
                   AsWeakPtr(),
                   base::Bind(&APIUtil::DeleteFileInternal,
                              AsWeakPtr(),
                              remote_file_md5,
                              callback)));
    return;
  }

  // Expected remote_file_md5 is empty so do a force delete.
  drive_service_->DeleteResource(
      resource_id,
      std::string(),
      base::Bind(&APIUtil::DidDeleteFile, AsWeakPtr(), callback));
  return;
}

GURL APIUtil::ResourceIdToResourceLink(const std::string& resource_id) const {
  return IsDriveAPIEnabled() ? drive_api_url_generator_.GetFileUrl(resource_id)
                             : wapi_url_generator_.GenerateEditUrl(resource_id);
}

void APIUtil::EnsureSyncRootIsNotInMyDrive(
    const std::string& sync_root_resource_id) {
  DCHECK(CalledOnValidThread());

  if (GetRootResourceId().empty()) {
    GetDriveRootResourceId(
        base::Bind(&APIUtil::DidGetDriveRootResourceIdForEnsureSyncRoot,
                   AsWeakPtr(), sync_root_resource_id));
    return;
  }

  DVLOG(2) << "Ensuring the sync root directory is not in 'My Drive'.";
  drive_service_->RemoveResourceFromDirectory(
      GetRootResourceId(),
      sync_root_resource_id,
      base::Bind(&EmptyGDataErrorCodeCallback));
}

void APIUtil::DidGetDriveRootResourceIdForEnsureSyncRoot(
    const std::string& sync_root_resource_id,
    google_apis::GDataErrorCode error) {
  DCHECK(CalledOnValidThread());
  // We don't have to check |error| since we can continue to process regardless
  // of it.
  EnsureSyncRootIsNotInMyDrive(sync_root_resource_id);
}

// static
// TODO(calvinlo): Delete this when Sync Directory Operations are supported by
// default.
std::string APIUtil::GetSyncRootDirectoryName() {
  return IsSyncFSDirectoryOperationEnabled() ? kSyncRootDirectoryNameDev
                                             : kSyncRootDirectoryName;
}

// static
std::string APIUtil::OriginToDirectoryTitle(const GURL& origin) {
  DCHECK(origin.SchemeIs(extensions::kExtensionScheme));
  return origin.host();
}

// static
GURL APIUtil::DirectoryTitleToOrigin(const std::string& title) {
  return extensions::Extension::GetBaseURLFromExtensionId(title);
}

void APIUtil::OnReadyToPerformOperations() {
  DCHECK(CalledOnValidThread());
  FOR_EACH_OBSERVER(APIUtilObserver, observers_, OnAuthenticated());
}

void APIUtil::OnConnectionTypeChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  DCHECK(CalledOnValidThread());
  if (type != net::NetworkChangeNotifier::CONNECTION_NONE) {
    FOR_EACH_OBSERVER(APIUtilObserver, observers_, OnNetworkConnected());
    return;
  }
  // We're now disconnected, reset the drive_uploader_ to force stop
  // uploading, otherwise the uploader may get stuck.
  // TODO(kinuko): Check the uploader behavior if it's the expected behavior
  // (http://crbug.com/223818)
  CancelAllUploads(google_apis::GDATA_NO_CONNECTION);
}

void APIUtil::DidGetResourceList(
    const ResourceListCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceList> resource_list) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on listing resource: " << error;
    callback.Run(error, scoped_ptr<google_apis::ResourceList>());
    return;
  }

  DVLOG(2) << "Got resource list";
  DCHECK(resource_list);
  callback.Run(error, resource_list.Pass());
}

void APIUtil::DidGetResourceEntry(
    const ResourceEntryCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting resource entry:" << error;
    callback.Run(error, scoped_ptr<google_apis::ResourceEntry>());
    return;
  }

  DVLOG(2) << "Got resource entry";
  DCHECK(entry);
  callback.Run(error, entry.Pass());
}

void APIUtil::DownloadFileInternal(
    const std::string& local_file_md5,
    const base::FilePath& local_file_path,
    const DownloadFileCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting resource entry for download";
    callback.Run(error, std::string(), 0, base::Time());
    return;
  }
  DCHECK(entry);

  DVLOG(2) << "Got resource entry for download";
  // If local file and remote file are same, cancel the download.
  if (local_file_md5 == entry->file_md5()) {
    callback.Run(google_apis::HTTP_NOT_MODIFIED,
                 local_file_md5,
                 entry->file_size(),
                 entry->updated_time());
    return;
  }

  DVLOG(2) << "Downloading file: " << entry->resource_id();
  const GURL& download_url = entry->download_url();
  drive_service_->DownloadFile(base::FilePath(kDummyDrivePath),
                               local_file_path,
                               download_url,
                               base::Bind(&APIUtil::DidDownloadFile,
                                          AsWeakPtr(),
                                          base::Passed(&entry),
                                          callback),
                               google_apis::GetContentCallback(),
                               google_apis::ProgressCallback());
}

void APIUtil::DidDownloadFile(scoped_ptr<google_apis::ResourceEntry> entry,
                              const DownloadFileCallback& callback,
                              google_apis::GDataErrorCode error,
                              const base::FilePath& downloaded_file_path) {
  DCHECK(CalledOnValidThread());
  if (error == google_apis::HTTP_SUCCESS)
    DVLOG(2) << "Download completed";
  else
    DVLOG(2) << "Error on downloading file: " << error;

  callback.Run(
      error, entry->file_md5(), entry->file_size(), entry->updated_time());
}

void APIUtil::DidUploadNewFile(const std::string& parent_resource_id,
                               const std::string& title,
                               UploadKey upload_key,
                               google_apis::GDataErrorCode error,
                               scoped_ptr<google_apis::ResourceEntry> entry) {
  UploadFileCallback callback = GetAndUnregisterUploadCallback(upload_key);
  DCHECK(!callback.is_null());
  if (error != google_apis::HTTP_SUCCESS &&
      error != google_apis::HTTP_CREATED) {
    DVLOG(2) << "Error on uploading new file: " << error;
    callback.Run(error, std::string(), std::string());
    return;
  }

  DVLOG(2) << "Upload completed";
  EnsureTitleUniqueness(parent_resource_id,
                        title,
                        base::Bind(&APIUtil::DidEnsureUniquenessForCreateFile,
                                   AsWeakPtr(),
                                   entry->resource_id(),
                                   callback));
}

void APIUtil::DidEnsureUniquenessForCreateFile(
    const std::string& expected_resource_id,
    const UploadFileCallback& callback,
    google_apis::GDataErrorCode error,
    EnsureUniquenessStatus status,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on uploading new file: " << error;
    callback.Run(error, std::string(), std::string());
    return;
  }

  switch (status) {
    case NO_DUPLICATES_FOUND:
      // The file was uploaded successfully and no conflict was detected.
      DCHECK(entry);
      DVLOG(2) << "No conflict detected on uploading new file";
      callback.Run(
          google_apis::HTTP_CREATED, entry->resource_id(), entry->file_md5());
      return;

    case RESOLVED_DUPLICATES:
      // The file was uploaded successfully but a conflict was detected.
      // The duplicated file was deleted successfully.
      DCHECK(entry);
      if (entry->resource_id() != expected_resource_id) {
        // TODO(kinuko): We should check local vs remote md5 here.
        DVLOG(2) << "Conflict detected on uploading new file";
        callback.Run(google_apis::HTTP_CONFLICT,
                     entry->resource_id(),
                     entry->file_md5());
        return;
      }

      DVLOG(2) << "Conflict detected on uploading new file and resolved";
      callback.Run(
          google_apis::HTTP_CREATED, entry->resource_id(), entry->file_md5());
      return;

    default:
      NOTREACHED() << "Unknown status from EnsureTitleUniqueness:" << status
                   << " for " << expected_resource_id;
  }
}

void APIUtil::UploadExistingFileInternal(
    const std::string& remote_file_md5,
    const base::FilePath& local_file_path,
    const UploadFileCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on uploading existing file: " << error;
    callback.Run(error, std::string(), std::string());
    return;
  }
  DCHECK(entry);

  // If remote file's hash value is different from the expected one, conflict
  // might have occurred.
  if (!remote_file_md5.empty() && remote_file_md5 != entry->file_md5()) {
    DVLOG(2) << "Conflict detected before uploading existing file";
    callback.Run(google_apis::HTTP_CONFLICT, std::string(), std::string());
    return;
  }

  std::string mime_type;
  if (!net::GetWellKnownMimeTypeFromExtension(local_file_path.Extension(),
                                              &mime_type))
    mime_type = kMimeTypeOctetStream;

  UploadKey upload_key = RegisterUploadCallback(callback);
  ResourceEntryCallback did_upload_callback =
      base::Bind(&APIUtil::DidUploadExistingFile, AsWeakPtr(), upload_key);
  drive_uploader_->UploadExistingFile(
      entry->resource_id(),
      base::FilePath(kDummyDrivePath),
      local_file_path,
      mime_type,
      entry->etag(),
      base::Bind(&UploadResultAdapter, did_upload_callback),
      google_apis::ProgressCallback());
}

bool APIUtil::IsAuthenticated() const {
  return drive_service_->HasRefreshToken();
}

void APIUtil::DidUploadExistingFile(
    UploadKey upload_key,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());
  UploadFileCallback callback = GetAndUnregisterUploadCallback(upload_key);
  DCHECK(!callback.is_null());
  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on uploading existing file: " << error;
    callback.Run(error, std::string(), std::string());
    return;
  }

  DCHECK(entry);
  DVLOG(2) << "Upload completed";
  callback.Run(error, entry->resource_id(), entry->file_md5());
}

void APIUtil::DeleteFileInternal(const std::string& remote_file_md5,
                                 const GDataErrorCallback& callback,
                                 google_apis::GDataErrorCode error,
                                 scoped_ptr<google_apis::ResourceEntry> entry) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on getting resource entry for deleting file: " << error;
    callback.Run(error);
    return;
  }
  DCHECK(entry);

  // If remote file's hash value is different from the expected one, conflict
  // might have occurred.
  if (!remote_file_md5.empty() && remote_file_md5 != entry->file_md5()) {
    DVLOG(2) << "Conflict detected before deleting file";
    callback.Run(google_apis::HTTP_CONFLICT);
    return;
  }
  DVLOG(2) << "Got resource entry for deleting file";

  // Move the file to trash (don't delete it completely).
  drive_service_->DeleteResource(
      entry->resource_id(),
      entry->etag(),
      base::Bind(&APIUtil::DidDeleteFile, AsWeakPtr(), callback));
}

void APIUtil::DidDeleteFile(const GDataErrorCallback& callback,
                            google_apis::GDataErrorCode error) {
  DCHECK(CalledOnValidThread());
  if (error == google_apis::HTTP_SUCCESS)
    DVLOG(2) << "Deletion completed";
  else
    DVLOG(2) << "Error on deleting file: " << error;

  callback.Run(error);
}

void APIUtil::EnsureTitleUniqueness(const std::string& parent_resource_id,
                                    const std::string& expected_title,
                                    const EnsureUniquenessCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Checking if there's no conflict on entry creation";

  const google_apis::GetResourceListCallback& bound_callback =
      base::Bind(&APIUtil::DidListEntriesToEnsureUniqueness,
                 AsWeakPtr(),
                 parent_resource_id,
                 expected_title,
                 callback);

  SearchByTitle(expected_title, parent_resource_id, bound_callback);
}

void APIUtil::DidListEntriesToEnsureUniqueness(
    const std::string& parent_resource_id,
    const std::string& expected_title,
    const EnsureUniquenessCallback& callback,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceList> feed) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS) {
    DVLOG(2) << "Error on listing resource for ensuring title uniqueness";
    callback.Run(
        error, NO_DUPLICATES_FOUND, scoped_ptr<google_apis::ResourceEntry>());
    return;
  }
  DVLOG(2) << "Got resource list for ensuring title uniqueness";

  // This filtering is needed only on WAPI. Once we move to Drive API we can
  // drop this.
  GURL parent_link;
  ParentType parent_type = PARENT_TYPE_DIRECTORY;
  if (parent_resource_id.empty()) {
    parent_link = ResourceIdToResourceLink(GetRootResourceId());
    parent_type = PARENT_TYPE_ROOT_OR_EMPTY;
  } else {
    parent_link = ResourceIdToResourceLink(parent_resource_id);
  }
  ScopedVector<google_apis::ResourceEntry> entries;
  entries.swap(*feed->mutable_entries());
  FilterEntriesByTitleAndParent(
      &entries, expected_title, parent_link, parent_type);

  if (entries.empty()) {
    DVLOG(2) << "Uploaded file is not found";
    callback.Run(google_apis::HTTP_NOT_FOUND,
                 NO_DUPLICATES_FOUND,
                 scoped_ptr<google_apis::ResourceEntry>());
    return;
  }

  if (entries.size() >= 2) {
    DVLOG(2) << "Conflict detected on creating entry";
    for (size_t i = 0; i < entries.size() - 1; ++i) {
      // TODO(tzik): Replace published_time with creation time after we move to
      // Drive API.
      if (entries[i]->published_time() < entries.back()->published_time())
        std::swap(entries[i], entries.back());
    }

    scoped_ptr<google_apis::ResourceEntry> earliest_entry(entries.back());
    entries.back() = NULL;
    entries.get().pop_back();

    DeleteEntriesForEnsuringTitleUniqueness(
        entries.Pass(),
        base::Bind(&EntryAdapterForEnsureTitleUniqueness,
                   base::Passed(&earliest_entry),
                   callback,
                   RESOLVED_DUPLICATES));
    return;
  }

  DVLOG(2) << "no conflict detected";
  DCHECK_EQ(1u, entries.size());
  scoped_ptr<google_apis::ResourceEntry> entry(entries.front());
  entries.weak_clear();

  callback.Run(google_apis::HTTP_SUCCESS, NO_DUPLICATES_FOUND, entry.Pass());
}

void APIUtil::DeleteEntriesForEnsuringTitleUniqueness(
    ScopedVector<google_apis::ResourceEntry> entries,
    const GDataErrorCallback& callback) {
  DCHECK(CalledOnValidThread());
  DVLOG(2) << "Cleaning up conflict on entry creation";

  if (entries.empty()) {
    callback.Run(google_apis::HTTP_SUCCESS);
    return;
  }

  scoped_ptr<google_apis::ResourceEntry> entry(entries.back());
  entries.back() = NULL;
  entries.get().pop_back();

  // We don't care conflicts here as other clients may be also deleting this
  // file, so passing an empty etag.
  drive_service_->DeleteResource(
      entry->resource_id(),
      std::string(),  // empty etag
      base::Bind(&APIUtil::DidDeleteEntriesForEnsuringTitleUniqueness,
                 AsWeakPtr(),
                 base::Passed(&entries),
                 callback));
}

void APIUtil::DidDeleteEntriesForEnsuringTitleUniqueness(
    ScopedVector<google_apis::ResourceEntry> entries,
    const GDataErrorCallback& callback,
    google_apis::GDataErrorCode error) {
  DCHECK(CalledOnValidThread());

  if (error != google_apis::HTTP_SUCCESS &&
      error != google_apis::HTTP_NOT_FOUND) {
    DVLOG(2) << "Error on deleting file: " << error;
    callback.Run(error);
    return;
  }

  DVLOG(2) << "Deletion completed";
  DeleteEntriesForEnsuringTitleUniqueness(entries.Pass(), callback);
}

APIUtil::UploadKey APIUtil::RegisterUploadCallback(
    const UploadFileCallback& callback) {
  const bool inserted = upload_callback_map_.insert(
      std::make_pair(upload_next_key_, callback)).second;
  CHECK(inserted);
  return upload_next_key_++;
}

APIUtil::UploadFileCallback APIUtil::GetAndUnregisterUploadCallback(
    UploadKey key) {
  UploadFileCallback callback;
  UploadCallbackMap::iterator found = upload_callback_map_.find(key);
  if (found == upload_callback_map_.end())
    return callback;
  callback = found->second;
  upload_callback_map_.erase(found);
  return callback;
}

void APIUtil::CancelAllUploads(google_apis::GDataErrorCode error) {
  if (upload_callback_map_.empty())
    return;
  for (UploadCallbackMap::iterator iter = upload_callback_map_.begin();
       iter != upload_callback_map_.end();
       ++iter) {
    iter->second.Run(error, std::string(), std::string());
  }
  upload_callback_map_.clear();
  drive_uploader_.reset(new google_apis::DriveUploader(drive_service_.get()));
}

std::string APIUtil::GetRootResourceId() const {
  if (IsDriveAPIEnabled()) {
    DCHECK(!root_resource_id_.empty());
    return root_resource_id_;
  }
  return drive_service_->GetRootResourceId();
}

}  // namespace drive
}  // namespace sync_file_system
