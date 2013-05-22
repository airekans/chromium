// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_OPERATIONS_H_
#define CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_OPERATIONS_H_

#include <string>

#include "base/callback_forward.h"
#include "chrome/browser/google_apis/base_operations.h"
#include "chrome/browser/google_apis/drive_api_url_generator.h"
#include "chrome/browser/google_apis/drive_service_interface.h"

namespace net {
class URLRequestContextGetter;
}  // namespace net

namespace google_apis {

class FileResource;

// Callback used for operations that the server returns FileResource data
// formatted into JSON value.
typedef base::Callback<void(GDataErrorCode error,
                            scoped_ptr<FileResource> entry)>
    FileResourceCallback;


//============================== GetAboutOperation =============================

// This class performs the operation for fetching About data.
class GetAboutOperation : public GetDataOperation {
 public:
  GetAboutOperation(OperationRunner* runner,
                    net::URLRequestContextGetter* url_request_context_getter,
                    const DriveApiUrlGenerator& url_generator,
                    const GetAboutResourceCallback& callback);
  virtual ~GetAboutOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;

  DISALLOW_COPY_AND_ASSIGN(GetAboutOperation);
};

//============================= GetApplistOperation ============================

// This class performs the operation for fetching Applist.
class GetApplistOperation : public GetDataOperation {
 public:
  GetApplistOperation(OperationRunner* runner,
                      net::URLRequestContextGetter* url_request_context_getter,
                      const DriveApiUrlGenerator& url_generator,
                      const GetDataCallback& callback);
  virtual ~GetApplistOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;

  DISALLOW_COPY_AND_ASSIGN(GetApplistOperation);
};

//============================ GetChangelistOperation ==========================

// This class performs the operation for fetching changelist.
// The result may contain only first part of the result. The remaining result
// should be able to be fetched by ContinueGetFileListOperation defined below.
class GetChangelistOperation : public GetDataOperation {
 public:
  // |include_deleted| specifies if the response should contain the changes
  // for deleted entries or not.
  // |start_changestamp| specifies the starting point of change list or 0 if
  // all changes are necessary.
  // |max_results| specifies the max of the number of files resource in the
  // response.
  GetChangelistOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      bool include_deleted,
      int64 start_changestamp,
      int max_results,
      const GetDataCallback& callback);
  virtual ~GetChangelistOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const bool include_deleted_;
  const int64 start_changestamp_;
  const int max_results_;

  DISALLOW_COPY_AND_ASSIGN(GetChangelistOperation);
};

//============================= GetFilelistOperation ===========================

// This class performs the operation for fetching Filelist.
// The result may contain only first part of the result. The remaining result
// should be able to be fetched by ContinueGetFileListOperation defined below.
class GetFilelistOperation : public GetDataOperation {
 public:
  GetFilelistOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& search_string,
      int max_results,
      const GetDataCallback& callback);
  virtual ~GetFilelistOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string search_string_;
  const int max_results_;

  DISALLOW_COPY_AND_ASSIGN(GetFilelistOperation);
};

//=============================== GetFileOperation =============================

// This class performs the operation for fetching a file.
class GetFileOperation : public GetDataOperation {
 public:
  GetFileOperation(OperationRunner* runner,
                   net::URLRequestContextGetter* url_request_context_getter,
                   const DriveApiUrlGenerator& url_generator,
                   const std::string& file_id,
                   const FileResourceCallback& callback);
  virtual ~GetFileOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  std::string file_id_;

  DISALLOW_COPY_AND_ASSIGN(GetFileOperation);
};

// This namespace is introduced to avoid class name confliction between
// the operations for Drive API v2 and GData WAPI for transition.
// And, when the migration is done and GData WAPI's code is cleaned up,
// classes inside this namespace should be moved to the google_apis namespace.
// TODO(hidehiko): Move all the operations defined in this file into drive
// namespace.  crbug.com/180808
namespace drive {

//======================= ContinueGetFileListOperation =========================

// This class performs the operation to fetch remaining Filelist result.
class ContinueGetFileListOperation : public GetDataOperation {
 public:
  ContinueGetFileListOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const GURL& url,
      const GetDataCallback& callback);
  virtual ~ContinueGetFileListOperation();

 protected:
  virtual GURL GetURL() const OVERRIDE;

 private:
  const GURL url_;

  DISALLOW_COPY_AND_ASSIGN(ContinueGetFileListOperation);
};

//========================== CreateDirectoryOperation ==========================

// This class performs the operation for creating a directory.
class CreateDirectoryOperation : public GetDataOperation {
 public:
  CreateDirectoryOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& parent_resource_id,
      const std::string& directory_name,
      const FileResourceCallback& callback);
  virtual ~CreateDirectoryOperation();

 protected:
  // Overridden from GetDataOperation.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual bool GetContentData(std::string* upload_content_type,
                              std::string* upload_content) OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string parent_resource_id_;
  const std::string directory_name_;

  DISALLOW_COPY_AND_ASSIGN(CreateDirectoryOperation);
};

//=========================== RenameResourceOperation ==========================

// This class performs the operation for renaming a document/file/directory.
class RenameResourceOperation : public EntryActionOperation {
 public:
  // |callback| must not be null.
  RenameResourceOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& resource_id,
      const std::string& new_name,
      const EntryActionCallback& callback);
  virtual ~RenameResourceOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual std::vector<std::string> GetExtraRequestHeaders() const OVERRIDE;
  virtual GURL GetURL() const OVERRIDE;
  virtual bool GetContentData(std::string* upload_content_type,
                              std::string* upload_content) OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;

  const std::string resource_id_;
  const std::string new_name_;

  DISALLOW_COPY_AND_ASSIGN(RenameResourceOperation);
};

//=========================== CopyResourceOperation ============================

// This class performs the operation for copying a resource.
//
// Copies the resource with |resource_id| into a directory with
// |parent_resource_id|. The new resource will be named as |new_name|.
// |parent_resource_id| can be empty. In the case, the copy will be created
// directly under the default root directory (this is the default behavior
// of Drive API v2's copy operation).
//
// This operation corresponds to "Files: copy" operation on Drive API v2. See
// also: https://developers.google.com/drive/v2/reference/files/copy
class CopyResourceOperation : public GetDataOperation {
 public:
  // Upon completion, |callback| will be called. |callback| must not be null.
  CopyResourceOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& resource_id,
      const std::string& parent_resource_id,
      const std::string& new_name,
      const FileResourceCallback& callback);
  virtual ~CopyResourceOperation();

 protected:
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual GURL GetURL() const OVERRIDE;
  virtual bool GetContentData(std::string* upload_content_type,
                              std::string* upload_content) OVERRIDE;
 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string resource_id_;
  const std::string parent_resource_id_;
  const std::string new_name_;

  DISALLOW_COPY_AND_ASSIGN(CopyResourceOperation);
};

//=========================== TrashResourceOperation ===========================

// This class performs the operation for trashing a resource.
//
// According to the document:
// https://developers.google.com/drive/v2/reference/files/trash
// the file resource will be returned from the server, which is not in the
// response from WAPI server. For the transition, we simply ignore the result,
// because now we do not handle resources in trash.
// Note for the naming: the name "trash" comes from the server's operation
// name. In order to be consistent with the server, we chose "trash" here,
// although we are preferring the term "remove" in drive/google_api code.
// TODO(hidehiko): Replace the base class to GetDataOperation.
class TrashResourceOperation : public EntryActionOperation {
 public:
  // |callback| must not be null.
  TrashResourceOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& resource_id,
      const EntryActionCallback& callback);
  virtual ~TrashResourceOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string resource_id_;

  DISALLOW_COPY_AND_ASSIGN(TrashResourceOperation);
};

//========================== InsertResourceOperation ===========================

// This class performs the operation for inserting a resource to a directory.
// Note that this is the operation of "Children: insert" of the Drive API v2.
// https://developers.google.com/drive/v2/reference/children/insert.
class InsertResourceOperation : public EntryActionOperation {
 public:
  // |callback| must not be null.
  InsertResourceOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& parent_resource_id,
      const std::string& resource_id,
      const EntryActionCallback& callback);
  virtual ~InsertResourceOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual bool GetContentData(std::string* upload_content_type,
                              std::string* upload_content) OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string parent_resource_id_;
  const std::string resource_id_;

  DISALLOW_COPY_AND_ASSIGN(InsertResourceOperation);
};

//========================== DeleteResourceOperation ===========================

// This class performs the operation for removing a resource from a directory.
// Note that we use "delete" for the name of this class, which comes from the
// operation name of the Drive API v2, although we prefer "remove" for that
// sense in "drive/google_api"
// Also note that this is the operation of "Children: delete" of the Drive API
// v2. https://developers.google.com/drive/v2/reference/children/delete
class DeleteResourceOperation : public EntryActionOperation {
 public:
  // |callback| must not be null.
  DeleteResourceOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const std::string& parent_resource_id,
      const std::string& resource_id,
      const EntryActionCallback& callback);
  virtual ~DeleteResourceOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string parent_resource_id_;
  const std::string resource_id_;

  DISALLOW_COPY_AND_ASSIGN(DeleteResourceOperation);
};

//======================= InitiateUploadNewFileOperation =======================

// This class performs the operation for initiating the upload of a new file.
class InitiateUploadNewFileOperation : public InitiateUploadOperationBase {
 public:
  // |parent_resource_id| should be the resource id of the parent directory.
  // |title| should be set.
  // See also the comments of InitiateUploadOperationBase for more details
  // about the other parameters.
  InitiateUploadNewFileOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const base::FilePath& drive_file_path,
      const std::string& content_type,
      int64 content_length,
      const std::string& parent_resource_id,
      const std::string& title,
      const InitiateUploadCallback& callback);
  virtual ~InitiateUploadNewFileOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual bool GetContentData(std::string* upload_content_type,
                              std::string* upload_content) OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string parent_resource_id_;
  const std::string title_;

  DISALLOW_COPY_AND_ASSIGN(InitiateUploadNewFileOperation);
};

//==================== InitiateUploadExistingFileOperation =====================

// This class performs the operation for initiating the upload of an existing
// file.
class InitiateUploadExistingFileOperation
    : public InitiateUploadOperationBase {
 public:
  // |upload_url| should be the upload_url() of the file
  //    (resumable-create-media URL)
  // |etag| should be set if it is available to detect the upload confliction.
  // See also the comments of InitiateUploadOperationBase for more details
  // about the other parameters.
  InitiateUploadExistingFileOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const DriveApiUrlGenerator& url_generator,
      const base::FilePath& drive_file_path,
      const std::string& content_type,
      int64 content_length,
      const std::string& resource_id,
      const std::string& etag,
      const InitiateUploadCallback& callback);
  virtual ~InitiateUploadExistingFileOperation();

 protected:
  // UrlFetchOperationBase overrides.
  virtual GURL GetURL() const OVERRIDE;
  virtual net::URLFetcher::RequestType GetRequestType() const OVERRIDE;
  virtual std::vector<std::string> GetExtraRequestHeaders() const OVERRIDE;

 private:
  const DriveApiUrlGenerator url_generator_;
  const std::string resource_id_;
  const std::string etag_;

  DISALLOW_COPY_AND_ASSIGN(InitiateUploadExistingFileOperation);
};

// Callback used for ResumeUpload() and GetUploadStatus().
typedef base::Callback<void(
    const UploadRangeResponse& response,
    scoped_ptr<FileResource> new_resource)> UploadRangeCallback;

//============================ ResumeUploadOperation ===========================

// Performs the operation for resuming the upload of a file.
class ResumeUploadOperation : public ResumeUploadOperationBase {
 public:
  // See also ResumeUploadOperationBase's comment for parameters meaning.
  // |callback| must not be null. |progress_callback| may be null.
  ResumeUploadOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const base::FilePath& drive_file_path,
      const GURL& upload_location,
      int64 start_position,
      int64 end_position,
      int64 content_length,
      const std::string& content_type,
      const base::FilePath& local_file_path,
      const UploadRangeCallback& callback,
      const ProgressCallback& progress_callback);
  virtual ~ResumeUploadOperation();

 protected:
  // UploadRangeOperationBase overrides.
  virtual void OnRangeOperationComplete(
      const UploadRangeResponse& response,
      scoped_ptr<base::Value> value) OVERRIDE;
  // content::UrlFetcherDelegate overrides.
  virtual void OnURLFetchUploadProgress(const net::URLFetcher* source,
                                        int64 current, int64 total) OVERRIDE;

 private:
  const UploadRangeCallback callback_;
  const ProgressCallback progress_callback_;

  DISALLOW_COPY_AND_ASSIGN(ResumeUploadOperation);
};

//========================== GetUploadStatusOperation ==========================

// Performs the operation to request the current upload status of a file.
class GetUploadStatusOperation : public GetUploadStatusOperationBase {
 public:
  // See also GetUploadStatusOperationBase's comment for parameters meaning.
  // |callback| must not be null.
  GetUploadStatusOperation(
      OperationRunner* runner,
      net::URLRequestContextGetter* url_request_context_getter,
      const base::FilePath& drive_file_path,
      const GURL& upload_url,
      int64 content_length,
      const UploadRangeCallback& callback);
  virtual ~GetUploadStatusOperation();

 protected:
  // UploadRangeOperationBase overrides.
  virtual void OnRangeOperationComplete(
      const UploadRangeResponse& response,
      scoped_ptr<base::Value> value) OVERRIDE;

 private:
  const UploadRangeCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(GetUploadStatusOperation);
};


}  // namespace drive
}  // namespace google_apis

#endif  // CHROME_BROWSER_GOOGLE_APIS_DRIVE_API_OPERATIONS_H_
