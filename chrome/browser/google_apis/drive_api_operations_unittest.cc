// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/google_apis/auth_service.h"
#include "chrome/browser/google_apis/drive_api_operations.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/drive_api_url_generator.h"
#include "chrome/browser/google_apis/operation_runner.h"
#include "chrome/browser/google_apis/task_util.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace google_apis {

namespace {

const char kTestDriveApiAuthToken[] = "testtoken";
const char kTestETag[] = "test_etag";
const char kTestUserAgent[] = "test-user-agent";

const char kTestChildrenResponse[] =
    "{\n"
    "\"kind\": \"drive#childReference\",\n"
    "\"id\": \"resource_id\",\n"
    "\"selfLink\": \"self_link\",\n"
    "\"childLink\": \"child_link\",\n"
    "}\n";

const char kTestUploadExistingFilePath[] = "/upload/existingfile/path";
const char kTestUploadNewFilePath[] = "/upload/newfile/path";

}  // namespace

class DriveApiOperationsTest : public testing::Test {
 public:
  DriveApiOperationsTest()
      : ui_thread_(content::BrowserThread::UI, &message_loop_),
        file_thread_(content::BrowserThread::FILE),
        io_thread_(content::BrowserThread::IO),
        test_server_(content::BrowserThread::GetMessageLoopProxyForThread(
                         content::BrowserThread::IO)) {
  }

  virtual void SetUp() OVERRIDE {
    file_thread_.Start();
    io_thread_.StartIOThread();
    profile_.reset(new TestingProfile);

    request_context_getter_ = new net::TestURLRequestContextGetter(
        content::BrowserThread::GetMessageLoopProxyForThread(
            content::BrowserThread::IO));

    operation_runner_.reset(new OperationRunner(profile_.get(),
                                                request_context_getter_.get(),
                                                std::vector<std::string>(),
                                                kTestUserAgent));
    operation_runner_->auth_service()->set_access_token_for_testing(
        kTestDriveApiAuthToken);

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    ASSERT_TRUE(test_server_.InitializeAndWaitUntilReady());
    test_server_.RegisterRequestHandler(
        base::Bind(&DriveApiOperationsTest::HandleChildrenDeleteRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&DriveApiOperationsTest::HandleDataFileRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&DriveApiOperationsTest::HandleResumeUploadRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&DriveApiOperationsTest::HandleInitiateUploadRequest,
                   base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::Bind(&DriveApiOperationsTest::HandleContentResponse,
                   base::Unretained(this)));

    url_generator_.reset(new DriveApiUrlGenerator(
        test_util::GetBaseUrlForTesting(test_server_.port())));

    // Reset the server's expected behavior just in case.
    ResetExpectedResponse();
    received_bytes_ = 0;
    content_length_ = 0;
  }

  virtual void TearDown() OVERRIDE {
    EXPECT_TRUE(test_server_.ShutdownAndWaitUntilComplete());
    request_context_getter_ = NULL;
    ResetExpectedResponse();
  }

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  content::TestBrowserThread io_thread_;
  net::test_server::EmbeddedTestServer test_server_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<OperationRunner> operation_runner_;
  scoped_ptr<DriveApiUrlGenerator> url_generator_;
  scoped_refptr<net::TestURLRequestContextGetter> request_context_getter_;
  base::ScopedTempDir temp_dir_;

  // This is a path to the file which contains expected response from
  // the server. See also HandleDataFileRequest below.
  base::FilePath expected_data_file_path_;

  // This is a path string in the expected response header from the server
  // for initiating file uploading.
  std::string expected_upload_path_;

  // These are content and its type in the expected response from the server.
  // See also HandleContentResponse below.
  std::string expected_content_type_;
  std::string expected_content_;

  // The incoming HTTP request is saved so tests can verify the request
  // parameters like HTTP method (ex. some operations should use DELETE
  // instead of GET).
  net::test_server::HttpRequest http_request_;

 private:
  void ResetExpectedResponse() {
    expected_data_file_path_.clear();
    expected_upload_path_.clear();
    expected_content_type_.clear();
    expected_content_.clear();
  }

  // For "Children: delete" request, the server will return "204 No Content"
  // response meaning "success".
  scoped_ptr<net::test_server::HttpResponse> HandleChildrenDeleteRequest(
      const net::test_server::HttpRequest& request) {
    if (request.method != net::test_server::METHOD_DELETE ||
        request.relative_url.find("/children/") == string::npos) {
      // The request is not the "Children: delete" operation. Delegate the
      // processing to the next handler.
      return scoped_ptr<net::test_server::HttpResponse>();
    }

    http_request_ = request;

    // Return the response with just "204 No Content" status code.
    scoped_ptr<net::test_server::BasicHttpResponse> http_response(
        new net::test_server::BasicHttpResponse);
    http_response->set_code(net::test_server::NO_CONTENT);
    return http_response.PassAs<net::test_server::HttpResponse>();
  }

  // Reads the data file of |expected_data_file_path_| and returns its content
  // for the request.
  // To use this method, it is necessary to set |expected_data_file_path_|
  // to the appropriate file path before sending the request to the server.
  scoped_ptr<net::test_server::HttpResponse> HandleDataFileRequest(
      const net::test_server::HttpRequest& request) {
    if (expected_data_file_path_.empty()) {
      // The file is not specified. Delegate the processing to the next
      // handler.
      return scoped_ptr<net::test_server::HttpResponse>();
    }

    http_request_ = request;

    // Return the response from the data file.
    return test_util::CreateHttpResponseFromFile(
        expected_data_file_path_).PassAs<net::test_server::HttpResponse>();
  }

  // Returns the response based on set expected upload url.
  // The response contains the url in its "Location: " header. Also, it doesn't
  // have any content.
  // To use this method, it is necessary to set |expected_upload_path_|
  // to the string representation of the url to be returned.
  scoped_ptr<net::test_server::HttpResponse> HandleInitiateUploadRequest(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url == expected_upload_path_ ||
        expected_upload_path_.empty()) {
      // The request is for resume uploading or the expected upload url is not
      // set. Delegate the processing to the next handler.
      return scoped_ptr<net::test_server::HttpResponse>();
    }

    http_request_ = request;

    scoped_ptr<net::test_server::BasicHttpResponse> response(
        new net::test_server::BasicHttpResponse);

    // Check an ETag.
    std::map<std::string, std::string>::const_iterator found =
        request.headers.find("If-Match");
    if (found != request.headers.end() &&
        found->second != "*" &&
        found->second != kTestETag) {
      response->set_code(net::test_server::PRECONDITION);
      return response.PassAs<net::test_server::HttpResponse>();
    }

    // Check if the X-Upload-Content-Length is present. If yes, store the
    // length of the file.
    found = request.headers.find("X-Upload-Content-Length");
    if (found == request.headers.end() ||
        !base::StringToInt64(found->second, &content_length_)) {
      return scoped_ptr<net::test_server::HttpResponse>();
    }
    received_bytes_ = 0;

    response->set_code(net::test_server::SUCCESS);
    response->AddCustomHeader(
        "Location",
        test_server_.base_url().Resolve(expected_upload_path_).spec());
    return response.PassAs<net::test_server::HttpResponse>();
  }

  scoped_ptr<net::test_server::HttpResponse> HandleResumeUploadRequest(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != expected_upload_path_) {
      // The request path is different from the expected path for uploading.
      // Delegate the processing to the next handler.
      return scoped_ptr<net::test_server::HttpResponse>();
    }

    http_request_ = request;

    if (!request.content.empty()) {
      std::map<std::string, std::string>::const_iterator iter =
          request.headers.find("Content-Range");
      if (iter == request.headers.end()) {
        // The range must be set.
        return scoped_ptr<net::test_server::HttpResponse>();
      }

      int64 length = 0;
      int64 start_position = 0;
      int64 end_position = 0;
      if (!test_util::ParseContentRangeHeader(
              iter->second, &start_position, &end_position, &length)) {
        // Invalid "Content-Range" value.
        return scoped_ptr<net::test_server::HttpResponse>();
      }

      EXPECT_EQ(start_position, received_bytes_);
      EXPECT_EQ(length, content_length_);

      // end_position is inclusive, but so +1 to change the range to byte size.
      received_bytes_ = end_position + 1;
    }

    if (received_bytes_ < content_length_) {
      scoped_ptr<net::test_server::BasicHttpResponse> response(
          new net::test_server::BasicHttpResponse);
      // Set RESUME INCOMPLETE (308) status code.
      response->set_code(net::test_server::RESUME_INCOMPLETE);

      // Add Range header to the response, based on the values of
      // Content-Range header in the request.
      // The header is annotated only when at least one byte is received.
      if (received_bytes_ > 0) {
        response->AddCustomHeader(
            "Range", "bytes=0-" + base::Int64ToString(received_bytes_ - 1));
      }

      return response.PassAs<net::test_server::HttpResponse>();
    }

    // All bytes are received. Return the "success" response with the file's
    // (dummy) metadata.
    scoped_ptr<net::test_server::BasicHttpResponse> response =
        test_util::CreateHttpResponseFromFile(
            test_util::GetTestFilePath("chromeos/drive/file_entry.json"));

    // The response code is CREATED if it is new file uploading.
    if (http_request_.relative_url == kTestUploadNewFilePath) {
      response->set_code(net::test_server::CREATED);
    }

    return response.PassAs<net::test_server::HttpResponse>();
  }

  // Returns the response based on set expected content and its type.
  // To use this method, both |expected_content_type_| and |expected_content_|
  // must be set in advance.
  scoped_ptr<net::test_server::HttpResponse> HandleContentResponse(
      const net::test_server::HttpRequest& request) {
    if (expected_content_type_.empty() || expected_content_.empty()) {
      // Expected content is not set. Delegate the processing to the next
      // handler.
      return scoped_ptr<net::test_server::HttpResponse>();
    }

    http_request_ = request;

    scoped_ptr<net::test_server::BasicHttpResponse> response(
        new net::test_server::BasicHttpResponse);
    response->set_code(net::test_server::SUCCESS);
    response->set_content_type(expected_content_type_);
    response->set_content(expected_content_);
    return response.PassAs<net::test_server::HttpResponse>();
  }

  // These are for the current upload file status.
  int64 received_bytes_;
  int64 content_length_;
};

TEST_F(DriveApiOperationsTest, GetAboutOperation_ValidJson) {
  // Set an expected data file containing valid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/drive/about.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<AboutResource> about_resource;

  GetAboutOperation* operation = new GetAboutOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &about_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/drive/v2/about", http_request_.relative_url);

  scoped_ptr<AboutResource> expected(
      AboutResource::CreateFrom(
          *test_util::LoadJSONFile("chromeos/drive/about.json")));
  ASSERT_TRUE(about_resource.get());
  EXPECT_EQ(expected->largest_change_id(), about_resource->largest_change_id());
  EXPECT_EQ(expected->quota_bytes_total(), about_resource->quota_bytes_total());
  EXPECT_EQ(expected->quota_bytes_used(), about_resource->quota_bytes_used());
  EXPECT_EQ(expected->root_folder_id(), about_resource->root_folder_id());
}

TEST_F(DriveApiOperationsTest, GetAboutOperation_InvalidJson) {
  // Set an expected data file containing invalid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/gdata/testfile.txt");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<AboutResource> about_resource;

  GetAboutOperation* operation = new GetAboutOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &about_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  // "parse error" should be returned, and the about resource should be NULL.
  EXPECT_EQ(GDATA_PARSE_ERROR, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/drive/v2/about", http_request_.relative_url);
  EXPECT_FALSE(about_resource.get());
}

TEST_F(DriveApiOperationsTest, GetApplistOperation) {
  // Set an expected data file containing valid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/drive/applist.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result;

  GetApplistOperation* operation = new GetApplistOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &result)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/drive/v2/apps", http_request_.relative_url);
  EXPECT_TRUE(result);
}

TEST_F(DriveApiOperationsTest, GetChangelistOperation) {
  // Set an expected data file containing valid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/drive/changelist.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result;

  GetChangelistOperation* operation = new GetChangelistOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      true,  // include deleted
      100,  // start changestamp
      500,  // max results
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &result)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/drive/v2/changes?startChangeId=100&maxResults=500",
            http_request_.relative_url);
  EXPECT_TRUE(result);
}

TEST_F(DriveApiOperationsTest, GetFilelistOperation) {
  // Set an expected data file containing valid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/drive/filelist.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result;

  GetFilelistOperation* operation = new GetFilelistOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      "\"abcde\" in parents",
      50,  // max results
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &result)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/drive/v2/files?maxResults=50&q=%22abcde%22+in+parents",
            http_request_.relative_url);
  EXPECT_TRUE(result);
}

TEST_F(DriveApiOperationsTest, ContinueGetFileListOperation) {
  // Set an expected data file containing valid result.
  expected_data_file_path_ = test_util::GetTestFilePath(
      "chromeos/drive/filelist.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<base::Value> result;

  drive::ContinueGetFileListOperation* operation =
      new drive::ContinueGetFileListOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          test_server_.GetURL("/continue/get/file/list"),
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &result)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_GET, http_request_.method);
  EXPECT_EQ("/continue/get/file/list", http_request_.relative_url);
  EXPECT_TRUE(result);
}

TEST_F(DriveApiOperationsTest, CreateDirectoryOperation) {
  // Set an expected data file containing the directory's entry data.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/directory_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<FileResource> file_resource;

  // Create "new directory" in the root directory.
  drive::CreateDirectoryOperation* operation =
      new drive::CreateDirectoryOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "root",
          "new directory",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &file_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/drive/v2/files", http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);

  scoped_ptr<FileResource> expected(
      FileResource::CreateFrom(
          *test_util::LoadJSONFile("chromeos/drive/directory_entry.json")));

  // Sanity check.
  ASSERT_TRUE(file_resource.get());

  EXPECT_EQ(expected->file_id(), file_resource->file_id());
  EXPECT_EQ(expected->title(), file_resource->title());
  EXPECT_EQ(expected->mime_type(), file_resource->mime_type());
  EXPECT_EQ(expected->parents().size(), file_resource->parents().size());
}

TEST_F(DriveApiOperationsTest, RenameResourceOperation) {
  // Set an expected data file containing the directory's entry data.
  // It'd be returned if we rename a directory.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/directory_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;

  // Create "new directory" in the root directory.
  drive::RenameResourceOperation* operation =
      new drive::RenameResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "resource_id",
          "new name",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_PATCH, http_request_.method);
  EXPECT_EQ("/drive/v2/files/resource_id", http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"title\":\"new name\"}", http_request_.content);
}

TEST_F(DriveApiOperationsTest, TouchResourceOperation) {
  // Set an expected data file containing the directory's entry data.
  // It'd be returned if we rename a directory.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/directory_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<FileResource> file_resource;
  const base::Time::Exploded kModifiedDate = {2012, 7, 0, 19, 15, 59, 13, 123};
  const base::Time::Exploded kLastViewedByMeDate =
      {2013, 7, 0, 19, 15, 59, 13, 123};

  // Touch a file with |resource_id|.
  drive::TouchResourceOperation* operation = new drive::TouchResourceOperation(
      operation_runner_.get(),
      request_context_getter_.get(),
      *url_generator_,
      "resource_id",
      base::Time::FromUTCExploded(kModifiedDate),
      base::Time::FromUTCExploded(kLastViewedByMeDate),
      CreateComposedCallback(
          base::Bind(&test_util::RunAndQuit),
          test_util::CreateCopyResultCallback(&error, &file_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_PATCH, http_request_.method);
  EXPECT_EQ("/drive/v2/files/resource_id"
            "?setModifiedDate=true&updateViewedDate=false",
            http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"lastViewedByMeDate\":\"2013-07-19T15:59:13.123Z\","
            "\"modifiedDate\":\"2012-07-19T15:59:13.123Z\"}",
            http_request_.content);
}

TEST_F(DriveApiOperationsTest, CopyResourceOperation) {
  // Set an expected data file containing the dummy file entry data.
  // It'd be returned if we copy a file.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/file_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<FileResource> file_resource;

  // Copy the file to a new file named "new name".
  drive::CopyResourceOperation* operation =
      new drive::CopyResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "resource_id",
          "parent_resource_id",
          "new name",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &file_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/drive/v2/files/resource_id/copy", http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(
      "{\"parents\":[{\"id\":\"parent_resource_id\"}],\"title\":\"new name\"}",
      http_request_.content);
  EXPECT_TRUE(file_resource);
}

TEST_F(DriveApiOperationsTest, CopyResourceOperation_EmptyParentResourceId) {
  // Set an expected data file containing the dummy file entry data.
  // It'd be returned if we copy a file.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/file_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;
  scoped_ptr<FileResource> file_resource;

  // Copy the file to a new file named "new name".
  drive::CopyResourceOperation* operation =
      new drive::CopyResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "resource_id",
          std::string(),  // parent resource id.
          "new name",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &file_resource)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/drive/v2/files/resource_id/copy", http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"title\":\"new name\"}", http_request_.content);
  EXPECT_TRUE(file_resource);
}

TEST_F(DriveApiOperationsTest, TrashResourceOperation) {
  // Set data for the expected result. Directory entry should be returned
  // if the trashing entry is a directory, so using it here should be fine.
  expected_data_file_path_ =
      test_util::GetTestFilePath("chromeos/drive/directory_entry.json");

  GDataErrorCode error = GDATA_OTHER_ERROR;

  // Trash a resource with the given resource id.
  drive::TrashResourceOperation* operation =
      new drive::TrashResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "resource_id",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/drive/v2/files/resource_id/trash", http_request_.relative_url);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_TRUE(http_request_.content.empty());
}

TEST_F(DriveApiOperationsTest, InsertResourceOperation) {
  // Set an expected data file containing the children entry.
  expected_content_type_ = "application/json";
  expected_content_ = kTestChildrenResponse;

  GDataErrorCode error = GDATA_OTHER_ERROR;

  // Add a resource with "resource_id" to a directory with
  // "parent_resource_id".
  drive::InsertResourceOperation* operation =
      new drive::InsertResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "parent_resource_id",
          "resource_id",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/drive/v2/files/parent_resource_id/children",
            http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);

  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"id\":\"resource_id\"}", http_request_.content);
}

TEST_F(DriveApiOperationsTest, DeleteResourceOperation) {
  GDataErrorCode error = GDATA_OTHER_ERROR;

  // Remove a resource with "resource_id" from a directory with
  // "parent_resource_id".
  drive::DeleteResourceOperation* operation =
      new drive::DeleteResourceOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          "parent_resource_id",
          "resource_id",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_NO_CONTENT, error);
  EXPECT_EQ(net::test_server::METHOD_DELETE, http_request_.method);
  EXPECT_EQ("/drive/v2/files/parent_resource_id/children/resource_id",
            http_request_.relative_url);
  EXPECT_FALSE(http_request_.has_content);
}

TEST_F(DriveApiOperationsTest, UploadNewFileOperation) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadNewFilePath;

  const char kTestContentType[] = "text/plain";
  const std::string kTestContent(100, 'a');
  const base::FilePath kTestFilePath =
      temp_dir_.path().AppendASCII("upload_file.txt");
  ASSERT_TRUE(test_util::WriteStringToFile(kTestFilePath, kTestContent));

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with
  // "parent_resource_id".
  drive::InitiateUploadNewFileOperation* operation =
      new drive::InitiateUploadNewFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          kTestContent.size(),
          "parent_resource_id",  // The resource id of the parent directory.
          "new file title",  // The title of the file being uploaded.
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(kTestUploadNewFilePath, upload_url.path());
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kTestContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"parents\":[{"
                "\"id\":\"parent_resource_id\","
                "\"kind\":\"drive#fileLink\""
            "}],"
            "\"title\":\"new file title\"}",
            http_request_.content);

  // Upload the content to the upload URL.
  UploadRangeResponse response;
  scoped_ptr<FileResource> new_entry;

  drive::ResumeUploadOperation* resume_operation =
      new drive::ResumeUploadOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          upload_url,
          0,  // start_position
          kTestContent.size(),  // end_position (exclusive)
          kTestContent.size(),  // content_length,
          kTestContentType,
          kTestFilePath,
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&response, &new_entry)),
          ProgressCallback());
  operation_runner_->StartOperationWithRetry(resume_operation);
  base::MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should be added.
  EXPECT_EQ("bytes 0-" +
            base::Int64ToString(kTestContent.size() - 1) + "/" +
            base::Int64ToString(kTestContent.size()),
            http_request_.headers["Content-Range"]);
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kTestContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

TEST_F(DriveApiOperationsTest, UploadNewEmptyFileOperation) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadNewFilePath;

  const char kTestContentType[] = "text/plain";
  const char kTestContent[] = "";
  const base::FilePath kTestFilePath =
      temp_dir_.path().AppendASCII("empty_file.txt");
  ASSERT_TRUE(test_util::WriteStringToFile(kTestFilePath, kTestContent));

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with "parent_resource_id".
  drive::InitiateUploadNewFileOperation* operation =
      new drive::InitiateUploadNewFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          0,
          "parent_resource_id",  // The resource id of the parent directory.
          "new file title",  // The title of the file being uploaded.
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(kTestUploadNewFilePath, upload_url.path());
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ("0", http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"parents\":[{"
                "\"id\":\"parent_resource_id\","
                "\"kind\":\"drive#fileLink\""
            "}],"
            "\"title\":\"new file title\"}",
            http_request_.content);

  // Upload the content to the upload URL.
  UploadRangeResponse response;
  scoped_ptr<FileResource> new_entry;

  drive::ResumeUploadOperation* resume_operation =
      new drive::ResumeUploadOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          upload_url,
          0,  // start_position
          0,  // end_position (exclusive)
          0,  // content_length,
          kTestContentType,
          kTestFilePath,
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&response, &new_entry)),
          ProgressCallback());
  operation_runner_->StartOperationWithRetry(resume_operation);
  base::MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should NOT be added.
  EXPECT_EQ(0U, http_request_.headers.count("Content-Range"));
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kTestContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

// TODO(kinaba): crbug.com/{241241,164098} Re-enable the test.
#define NO_GET_UPLOAD_STATUS_TEST

TEST_F(DriveApiOperationsTest, UploadNewLargeFileOperation) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadNewFilePath;

  const char kTestContentType[] = "text/plain";
  const size_t kNumChunkBytes = 10;  // Num bytes in a chunk.
  const std::string kTestContent(100, 'a');
  const base::FilePath kTestFilePath =
      temp_dir_.path().AppendASCII("upload_file.txt");
  ASSERT_TRUE(test_util::WriteStringToFile(kTestFilePath, kTestContent));

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with "parent_resource_id".
  drive::InitiateUploadNewFileOperation* operation =
      new drive::InitiateUploadNewFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          kTestContent.size(),
          "parent_resource_id",  // The resource id of the parent directory.
          "new file title",  // The title of the file being uploaded.
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(kTestUploadNewFilePath, upload_url.path());
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kTestContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);

  EXPECT_EQ(net::test_server::METHOD_POST, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_EQ("application/json", http_request_.headers["Content-Type"]);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ("{\"parents\":[{"
                "\"id\":\"parent_resource_id\","
                "\"kind\":\"drive#fileLink\""
            "}],"
            "\"title\":\"new file title\"}",
            http_request_.content);

#if !defined(NO_GET_UPLOAD_STATUS_TEST)
  // Before sending any data, check the current status.
  // This is an edge case test for GetUploadStatusOperation.
  {
    UploadRangeResponse response;
    scoped_ptr<FileResource> new_entry;

    // Check the response by GetUploadStatusOperation.
    drive::GetUploadStatusOperation* get_upload_status_operation =
        new drive::GetUploadStatusOperation(
            operation_runner_.get(),
            request_context_getter_.get(),
            base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
            upload_url,
            kTestContent.size(),
            CreateComposedCallback(
                base::Bind(&test_util::RunAndQuit),
                test_util::CreateCopyResultCallback(&response, &new_entry)));
    operation_runner_->StartOperationWithRetry(get_upload_status_operation);
    base::MessageLoop::current()->Run();

    // METHOD_PUT should be used to upload data.
    EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
    // Request should go to the upload URL.
    EXPECT_EQ(upload_url.path(), http_request_.relative_url);
    // Content-Range header should be added.
    EXPECT_EQ("bytes */" + base::Int64ToString(kTestContent.size()),
              http_request_.headers["Content-Range"]);
    EXPECT_TRUE(http_request_.has_content);
    EXPECT_TRUE(http_request_.content.empty());

    // Check the response.
    EXPECT_EQ(HTTP_RESUME_INCOMPLETE, response.code);
    EXPECT_EQ(0, response.start_position_received);
    EXPECT_EQ(0, response.end_position_received);
  }
#endif  // NO_GET_UPLOAD_STATUS_TEST

  // Upload the content to the upload URL.
  for (size_t start_position = 0; start_position < kTestContent.size();
       start_position += kNumChunkBytes) {
    const std::string payload = kTestContent.substr(
        start_position,
        std::min(kNumChunkBytes, kTestContent.size() - start_position));
    const size_t end_position = start_position + payload.size();

    UploadRangeResponse response;
    scoped_ptr<FileResource> new_entry;

    drive::ResumeUploadOperation* resume_operation =
        new drive::ResumeUploadOperation(
            operation_runner_.get(),
            request_context_getter_.get(),
            base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
            upload_url,
            start_position,
            end_position,
            kTestContent.size(),  // content_length,
            kTestContentType,
            kTestFilePath,
            CreateComposedCallback(
                base::Bind(&test_util::RunAndQuit),
                test_util::CreateCopyResultCallback(&response, &new_entry)),
            ProgressCallback());
    operation_runner_->StartOperationWithRetry(resume_operation);
    base::MessageLoop::current()->Run();

    // METHOD_PUT should be used to upload data.
    EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
    // Request should go to the upload URL.
    EXPECT_EQ(upload_url.path(), http_request_.relative_url);
    // Content-Range header should be added.
    EXPECT_EQ("bytes " +
              base::Int64ToString(start_position) + "-" +
              base::Int64ToString(end_position - 1) + "/" +
              base::Int64ToString(kTestContent.size()),
              http_request_.headers["Content-Range"]);
    // The upload content should be set in the HTTP request.
    EXPECT_TRUE(http_request_.has_content);
    EXPECT_EQ(payload, http_request_.content);

    if (end_position == kTestContent.size()) {
      // Check the response.
      EXPECT_EQ(HTTP_CREATED, response.code);  // Because it's a new file
      // The start and end positions should be set to -1, if an upload is
      // complete.
      EXPECT_EQ(-1, response.start_position_received);
      EXPECT_EQ(-1, response.end_position_received);
      break;
    }

    // Check the response.
    EXPECT_EQ(HTTP_RESUME_INCOMPLETE, response.code);
    EXPECT_EQ(0, response.start_position_received);
    EXPECT_EQ(static_cast<int64>(end_position), response.end_position_received);

#if !defined(NO_GET_UPLOAD_STATUS_TEST)
    // Check the response by GetUploadStatusOperation.
    drive::GetUploadStatusOperation* get_upload_status_operation =
        new drive::GetUploadStatusOperation(
            operation_runner_.get(),
            request_context_getter_.get(),
            base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
            upload_url,
            kTestContent.size(),
            CreateComposedCallback(
                base::Bind(&test_util::RunAndQuit),
                test_util::CreateCopyResultCallback(&response, &new_entry)));
    operation_runner_->StartOperationWithRetry(get_upload_status_operation);
    base::MessageLoop::current()->Run();

    // METHOD_PUT should be used to upload data.
    EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
    // Request should go to the upload URL.
    EXPECT_EQ(upload_url.path(), http_request_.relative_url);
    // Content-Range header should be added.
    EXPECT_EQ("bytes */" + base::Int64ToString(kTestContent.size()),
              http_request_.headers["Content-Range"]);
    EXPECT_TRUE(http_request_.has_content);
    EXPECT_TRUE(http_request_.content.empty());

    // Check the response.
    EXPECT_EQ(HTTP_RESUME_INCOMPLETE, response.code);
    EXPECT_EQ(0, response.start_position_received);
    EXPECT_EQ(static_cast<int64>(end_position),
              response.end_position_received);
#endif  // NO_GET_UPLOAD_STATUS_TEST
  }
}

TEST_F(DriveApiOperationsTest, UploadExistingFileOperation) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadExistingFilePath;

  const char kTestContentType[] = "text/plain";
  const std::string kTestContent(100, 'a');
  const base::FilePath kTestFilePath =
      temp_dir_.path().AppendASCII("upload_file.txt");
  ASSERT_TRUE(test_util::WriteStringToFile(kTestFilePath, kTestContent));

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with "parent_resource_id".
  drive::InitiateUploadExistingFileOperation* operation =
      new drive::InitiateUploadExistingFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          kTestContent.size(),
          "resource_id",  // The resource id of the file to be overwritten.
          std::string(),  // No etag.
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(kTestUploadExistingFilePath, upload_url.path());
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kTestContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);
  EXPECT_EQ("*", http_request_.headers["If-Match"]);

  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files/resource_id?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_TRUE(http_request_.content.empty());

  // Upload the content to the upload URL.
  UploadRangeResponse response;
  scoped_ptr<FileResource> new_entry;

  drive::ResumeUploadOperation* resume_operation =
      new drive::ResumeUploadOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          upload_url,
          0,  // start_position
          kTestContent.size(),  // end_position (exclusive)
          kTestContent.size(),  // content_length,
          kTestContentType,
          kTestFilePath,
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&response, &new_entry)),
          ProgressCallback());
  operation_runner_->StartOperationWithRetry(resume_operation);
  base::MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should be added.
  EXPECT_EQ("bytes 0-" +
            base::Int64ToString(kTestContent.size() - 1) + "/" +
            base::Int64ToString(kTestContent.size()),
            http_request_.headers["Content-Range"]);
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kTestContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_SUCCESS, response.code);  // Because it's an existing file
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

TEST_F(DriveApiOperationsTest, UploadExistingFileOperationWithETag) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadExistingFilePath;

  const char kTestContentType[] = "text/plain";
  const std::string kTestContent(100, 'a');
  const base::FilePath kTestFilePath =
      temp_dir_.path().AppendASCII("upload_file.txt");
  ASSERT_TRUE(test_util::WriteStringToFile(kTestFilePath, kTestContent));

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with "parent_resource_id".
  drive::InitiateUploadExistingFileOperation* operation =
      new drive::InitiateUploadExistingFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          kTestContent.size(),
          "resource_id",  // The resource id of the file to be overwritten.
          kTestETag,
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_SUCCESS, error);
  EXPECT_EQ(kTestUploadExistingFilePath, upload_url.path());
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kTestContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);
  EXPECT_EQ(kTestETag, http_request_.headers["If-Match"]);

  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files/resource_id?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_TRUE(http_request_.content.empty());

  // Upload the content to the upload URL.
  UploadRangeResponse response;
  scoped_ptr<FileResource> new_entry;

  drive::ResumeUploadOperation* resume_operation =
      new drive::ResumeUploadOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          upload_url,
          0,  // start_position
          kTestContent.size(),  // end_position (exclusive)
          kTestContent.size(),  // content_length,
          kTestContentType,
          kTestFilePath,
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&response, &new_entry)),
          ProgressCallback());
  operation_runner_->StartOperationWithRetry(resume_operation);
  base::MessageLoop::current()->Run();

  // METHOD_PUT should be used to upload data.
  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  // Request should go to the upload URL.
  EXPECT_EQ(upload_url.path(), http_request_.relative_url);
  // Content-Range header should be added.
  EXPECT_EQ("bytes 0-" +
            base::Int64ToString(kTestContent.size() - 1) + "/" +
            base::Int64ToString(kTestContent.size()),
            http_request_.headers["Content-Range"]);
  // The upload content should be set in the HTTP request.
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_EQ(kTestContent, http_request_.content);

  // Check the response.
  EXPECT_EQ(HTTP_SUCCESS, response.code);  // Because it's an existing file
  // The start and end positions should be set to -1, if an upload is complete.
  EXPECT_EQ(-1, response.start_position_received);
  EXPECT_EQ(-1, response.end_position_received);
}

TEST_F(DriveApiOperationsTest, UploadExistingFileOperationWithETagConflicting) {
  // Set an expected url for uploading.
  expected_upload_path_ = kTestUploadExistingFilePath;

  const char kTestContentType[] = "text/plain";
  const std::string kTestContent(100, 'a');

  GDataErrorCode error = GDATA_OTHER_ERROR;
  GURL upload_url;

  // Initiate uploading a new file to the directory with "parent_resource_id".
  drive::InitiateUploadExistingFileOperation* operation =
      new drive::InitiateUploadExistingFileOperation(
          operation_runner_.get(),
          request_context_getter_.get(),
          *url_generator_,
          base::FilePath(FILE_PATH_LITERAL("drive/file/path")),
          kTestContentType,
          kTestContent.size(),
          "resource_id",  // The resource id of the file to be overwritten.
          "Conflicting-etag",
          CreateComposedCallback(
              base::Bind(&test_util::RunAndQuit),
              test_util::CreateCopyResultCallback(&error, &upload_url)));
  operation_runner_->StartOperationWithRetry(operation);
  base::MessageLoop::current()->Run();

  EXPECT_EQ(HTTP_PRECONDITION, error);
  EXPECT_EQ(kTestContentType, http_request_.headers["X-Upload-Content-Type"]);
  EXPECT_EQ(base::Int64ToString(kTestContent.size()),
            http_request_.headers["X-Upload-Content-Length"]);
  EXPECT_EQ("Conflicting-etag", http_request_.headers["If-Match"]);

  EXPECT_EQ(net::test_server::METHOD_PUT, http_request_.method);
  EXPECT_EQ("/upload/drive/v2/files/resource_id?uploadType=resumable",
            http_request_.relative_url);
  EXPECT_TRUE(http_request_.has_content);
  EXPECT_TRUE(http_request_.content.empty());
}

}  // namespace google_apis
