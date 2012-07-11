// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/message_loop.h"
#include "media/base/media_log.h"
#include "media/base/mock_callback.h"
#include "media/base/mock_data_source_host.h"
#include "media/base/mock_filters.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebURLResponse.h"
#include "webkit/media/buffered_data_source.h"
#include "webkit/mocks/mock_webframeclient.h"
#include "webkit/mocks/mock_weburlloader.h"
#include "webkit/media/test_response_generator.h"

using ::testing::_;
using ::testing::Assign;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::StrictMock;

using WebKit::WebFrame;
using WebKit::WebString;
using WebKit::WebURLLoader;
using WebKit::WebURLResponse;
using WebKit::WebView;

using webkit_glue::MockWebFrameClient;
using webkit_glue::MockWebURLLoader;

namespace webkit_media {

// Overrides CreateResourceLoader() to permit injecting a MockWebURLLoader.
// Also keeps track of whether said MockWebURLLoader is actively loading.
class MockBufferedDataSource : public BufferedDataSource {
 public:
  MockBufferedDataSource(MessageLoop* message_loop, WebFrame* frame)
      : BufferedDataSource(message_loop, frame, new media::MediaLog(),
                           base::Bind(&MockBufferedDataSource::set_downloading,
                                      base::Unretained(this))),
        downloading_(false),
        loading_(false) {
  }

  MOCK_METHOD2(CreateResourceLoader, BufferedResourceLoader*(int64, int64));
  BufferedResourceLoader* CreateMockResourceLoader(int64 first_byte_position,
                                                   int64 last_byte_position) {
    CHECK(!loading_) << "Previous resource load wasn't cancelled";

    BufferedResourceLoader* loader =
        BufferedDataSource::CreateResourceLoader(first_byte_position,
                                                 last_byte_position);

    // Keep track of active loading state via loadAsynchronously() and cancel().
    NiceMock<MockWebURLLoader>* url_loader = new NiceMock<MockWebURLLoader>();
    ON_CALL(*url_loader, loadAsynchronously(_, _))
        .WillByDefault(Assign(&loading_, true));
    ON_CALL(*url_loader, cancel())
        .WillByDefault(Assign(&loading_, false));

    // |test_loader_| will be used when Start() is called.
    loader->test_loader_ = scoped_ptr<WebURLLoader>(url_loader);
    return loader;
  }

  bool loading() { return loading_; }
  void set_loading(bool loading) { loading_ = loading; }
  bool downloading() { return downloading_; }
  void set_downloading(bool downloading) { downloading_ = downloading; }

 private:
  virtual ~MockBufferedDataSource() {}

  // Whether the resource is downloading or deferred.
  bool downloading_;

  // Whether the resource load has starting loading but yet to been cancelled.
  bool loading_;

  DISALLOW_COPY_AND_ASSIGN(MockBufferedDataSource);
};

static const int64 kFileSize = 5000000;
static const int64 kFarReadPosition = 4000000;
static const int kDataSize = 1024;

static const char kHttpUrl[] = "http://localhost/foo.webm";
static const char kFileUrl[] = "file:///tmp/bar.webm";

class BufferedDataSourceTest : public testing::Test {
 public:
  BufferedDataSourceTest()
      : view_(WebView::create(NULL)) {
    view_->initializeMainFrame(&client_);

    data_source_ = new MockBufferedDataSource(&message_loop_,
                                              view_->mainFrame());
    data_source_->set_host(&host_);
  }

  virtual ~BufferedDataSourceTest() {
    view_->close();
  }

  void Initialize(const char* url, media::PipelineStatus expected) {
    GURL gurl(url);
    response_generator_.reset(new TestResponseGenerator(gurl, kFileSize));

    ExpectCreateResourceLoader();
    data_source_->Initialize(gurl,
                             BufferedResourceLoader::kUnspecified,
                             media::NewExpectedStatusCB(expected));
    message_loop_.RunAllPending();

    bool is_http = gurl.SchemeIs(kHttpScheme) || gurl.SchemeIs(kHttpsScheme);
    EXPECT_EQ(data_source_->downloading(), is_http);
  }

  // Helper to initialize tests with a valid 206 response.
  void InitializeWith206Response() {
    Initialize(kHttpUrl, media::PIPELINE_OK);

    EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
    Respond(response_generator_->Generate206(0));
  }

  // Helper to initialize tests with a valid file:// response.
  void InitializeWithFileResponse() {
    Initialize(kFileUrl, media::PIPELINE_OK);

    EXPECT_CALL(host_, SetTotalBytes(kFileSize));
    EXPECT_CALL(host_, AddBufferedByteRange(0, kFileSize));
    Respond(response_generator_->GenerateFileResponse(0));
  }

  // Stops any active loaders and shuts down the data source.
  //
  // This typically happens when the page is closed and for our purposes is
  // appropriate to do when tearing down a test.
  void Stop() {
    if (data_source_->loading()) {
      loader()->didFail(url_loader(), response_generator_->GenerateError());
      message_loop_.RunAllPending();
    }

    data_source_->Stop(media::NewExpectedClosure());
    message_loop_.RunAllPending();
  }

  void ExpectCreateResourceLoader() {
    EXPECT_CALL(*data_source_, CreateResourceLoader(_, _))
        .WillOnce(Invoke(data_source_.get(),
                         &MockBufferedDataSource::CreateMockResourceLoader));
    message_loop_.RunAllPending();
  }

  void Respond(const WebURLResponse& response) {
    loader()->didReceiveResponse(url_loader(), response);
    message_loop_.RunAllPending();
  }

  void ReceiveData(int size) {
    scoped_array<char> data(new char[size]);
    memset(data.get(), 0xA5, size);  // Arbitrary non-zero value.

    loader()->didReceiveData(url_loader(), data.get(), size, size);
    message_loop_.RunAllPending();
  }

  void FinishLoading() {
    data_source_->set_loading(false);
    loader()->didFinishLoading(url_loader(), 0);
    message_loop_.RunAllPending();
  }

  MOCK_METHOD1(ReadCallback, void(int size));

  void ReadAt(int64 position) {
    data_source_->Read(position, kDataSize, buffer_,
                       base::Bind(&BufferedDataSourceTest::ReadCallback,
                                  base::Unretained(this)));
    message_loop_.RunAllPending();
  }

  // Accessors for private variables on |data_source_|.
  BufferedResourceLoader* loader() {
    return data_source_->loader_.get();
  }
  WebURLLoader* url_loader() {
    return loader()->active_loader_->loader_.get();
  }

  Preload preload() { return data_source_->preload_; }
  BufferedResourceLoader::DeferStrategy defer_strategy() {
    return loader()->defer_strategy_;
  }
  int data_source_bitrate() { return data_source_->bitrate_; }
  int data_source_playback_rate() { return data_source_->playback_rate_; }
  int loader_bitrate() { return loader()->bitrate_; }
  int loader_playback_rate() { return loader()->playback_rate_; }

  scoped_refptr<MockBufferedDataSource> data_source_;

  scoped_ptr<TestResponseGenerator> response_generator_;
  MockWebFrameClient client_;
  WebView* view_;

  StrictMock<media::MockDataSourceHost> host_;
  MessageLoop message_loop_;

 private:
  // Used for calling BufferedDataSource::Read().
  uint8 buffer_[kDataSize];

  DISALLOW_COPY_AND_ASSIGN(BufferedDataSourceTest);
};

TEST_F(BufferedDataSourceTest, Range_Supported) {
  Initialize(kHttpUrl, media::PIPELINE_OK);

  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  Respond(response_generator_->Generate206(0));

  EXPECT_TRUE(data_source_->loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_InstanceSizeUnknown) {
  Initialize(kHttpUrl, media::PIPELINE_OK);

  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentRangeInstanceSize));

  EXPECT_TRUE(data_source_->loading());
  EXPECT_TRUE(data_source_->IsStreaming());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_NotFound) {
  Initialize(kHttpUrl, media::PIPELINE_ERROR_NETWORK);
  Respond(response_generator_->Generate404());

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_NotSupported) {
  Initialize(kHttpUrl, media::PIPELINE_OK);
  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  Respond(response_generator_->Generate200());

  EXPECT_TRUE(data_source_->loading());
  EXPECT_TRUE(data_source_->IsStreaming());
  Stop();
}

// Special carve-out for Apache versions that choose to return a 200 for
// Range:0- ("because it's more efficient" than a 206)
TEST_F(BufferedDataSourceTest, Range_SupportedButReturned200) {
  Initialize(kHttpUrl, media::PIPELINE_OK);
  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  WebURLResponse response = response_generator_->Generate200();
  response.setHTTPHeaderField(WebString::fromUTF8("Accept-Ranges"),
                              WebString::fromUTF8("bytes"));
  Respond(response);

  EXPECT_TRUE(data_source_->loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_MissingContentRange) {
  Initialize(kHttpUrl, media::PIPELINE_ERROR_NETWORK);
  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentRange));

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_MissingContentLength) {
  Initialize(kHttpUrl, media::PIPELINE_OK);

  // It'll manage without a Content-Length response.
  EXPECT_CALL(host_, SetTotalBytes(response_generator_->content_length()));
  Respond(response_generator_->Generate206(
      0, TestResponseGenerator::kNoContentLength));

  EXPECT_TRUE(data_source_->loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

TEST_F(BufferedDataSourceTest, Range_WrongContentRange) {
  Initialize(kHttpUrl, media::PIPELINE_ERROR_NETWORK);

  // Now it's done and will fail.
  Respond(response_generator_->Generate206(1337));

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

// Test the case where the initial response from the server indicates that
// Range requests are supported, but a later request prove otherwise.
TEST_F(BufferedDataSourceTest, Range_ServerLied) {
  InitializeWith206Response();

  // Read causing a new request to be made -- we'll expect it to error.
  ExpectCreateResourceLoader();
  ReadAt(kFarReadPosition);

  // Return a 200 in response to a range request.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Respond(response_generator_->Generate200());

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, Http_AbortWhileReading) {
  InitializeWith206Response();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(0);

  // Abort!!!
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  data_source_->Abort();
  message_loop_.RunAllPending();

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_AbortWhileReading) {
  InitializeWithFileResponse();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(0);

  // Abort!!!
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  data_source_->Abort();
  message_loop_.RunAllPending();

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, Http_Retry) {
  InitializeWith206Response();

  // Read to advance our position.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize - 1));
  ReadAt(0);
  ReceiveData(kDataSize);

  // Issue a pending read but terminate the connection to force a retry.
  ReadAt(kDataSize);
  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->Generate206(kDataSize));

  // Complete the read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(kDataSize, (kDataSize * 2) - 1));
  ReceiveData(kDataSize);

  EXPECT_TRUE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_Retry) {
  InitializeWithFileResponse();

  // Read to advance our position.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReadAt(0);
  ReceiveData(kDataSize);

  // Issue a pending read but terminate the connection to force a retry.
  ReadAt(kDataSize);
  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->GenerateFileResponse(kDataSize));

  // Complete the read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReceiveData(kDataSize);

  EXPECT_TRUE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, Http_TooManyRetries) {
  InitializeWith206Response();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(0);

  // It'll try three times.
  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->Generate206(0));

  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->Generate206(0));

  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->Generate206(0));

  // It'll error after this.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  FinishLoading();

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_TooManyRetries) {
  InitializeWithFileResponse();

  // Make sure there's a pending read -- we'll expect it to error.
  ReadAt(0);

  // It'll try three times.
  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->GenerateFileResponse(0));

  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->GenerateFileResponse(0));

  ExpectCreateResourceLoader();
  FinishLoading();
  Respond(response_generator_->GenerateFileResponse(0));

  // It'll error after this.
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  FinishLoading();

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_InstanceSizeUnknown) {
  Initialize(kFileUrl, media::PIPELINE_ERROR_NETWORK);
  EXPECT_FALSE(data_source_->downloading());

  Respond(response_generator_->GenerateFileResponse(-1));

  EXPECT_FALSE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_Successful) {
  InitializeWithFileResponse();

  EXPECT_TRUE(data_source_->loading());
  EXPECT_FALSE(data_source_->IsStreaming());
  Stop();
}

static void SetTrue(bool* value) {
  *value = true;
}

// This test makes sure that Stop() does not require a task to run on
// |message_loop_| before it calls its callback. This prevents accidental
// introduction of a pipeline teardown deadlock. The pipeline owner blocks
// the render message loop while waiting for Stop() to complete. Since this
// object runs on the render message loop, Stop() will not complete if it
// requires a task to run on the the message loop that is being blocked.
TEST_F(BufferedDataSourceTest, StopDoesNotUseMessageLoopForCallback) {
  InitializeWith206Response();

  // Stop() the data source, using a callback that lets us verify that it was
  // called before Stop() returns. This is to make sure that the callback does
  // not require |message_loop_| to execute tasks before being called.
  bool stop_done_called = false;
  EXPECT_TRUE(data_source_->loading());
  data_source_->Stop(base::Bind(&SetTrue, &stop_done_called));

  // Verify that the callback was called inside the Stop() call.
  EXPECT_TRUE(stop_done_called);
  message_loop_.RunAllPending();
}

TEST_F(BufferedDataSourceTest, DefaultValues) {
  InitializeWith206Response();

  // Ensure we have sane values for default loading scenario.
  EXPECT_EQ(AUTO, preload());
  EXPECT_EQ(BufferedResourceLoader::kCapacityDefer, defer_strategy());

  EXPECT_EQ(0, data_source_bitrate());
  EXPECT_EQ(0.0f, data_source_playback_rate());
  EXPECT_EQ(0, loader_bitrate());
  EXPECT_EQ(0.0f, loader_playback_rate());

  EXPECT_TRUE(data_source_->loading());
  Stop();
}

TEST_F(BufferedDataSourceTest, SetBitrate) {
  InitializeWith206Response();

  data_source_->SetBitrate(1234);
  message_loop_.RunAllPending();
  EXPECT_EQ(1234, data_source_bitrate());
  EXPECT_EQ(1234, loader_bitrate());

  // Read so far ahead to cause the loader to get recreated.
  BufferedResourceLoader* old_loader = loader();
  ExpectCreateResourceLoader();
  ReadAt(kFarReadPosition);
  Respond(response_generator_->Generate206(kFarReadPosition));

  // Verify loader changed but still has same bitrate.
  EXPECT_NE(old_loader, loader());
  EXPECT_EQ(1234, loader_bitrate());

  EXPECT_TRUE(data_source_->loading());
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Stop();
}

TEST_F(BufferedDataSourceTest, SetPlaybackRate) {
  InitializeWith206Response();

  data_source_->SetPlaybackRate(2.0f);
  message_loop_.RunAllPending();
  EXPECT_EQ(2.0f, data_source_playback_rate());
  EXPECT_EQ(2.0f, loader_playback_rate());

  // Read so far ahead to cause the loader to get recreated.
  BufferedResourceLoader* old_loader = loader();
  ExpectCreateResourceLoader();
  ReadAt(kFarReadPosition);
  Respond(response_generator_->Generate206(kFarReadPosition));

  // Verify loader changed but still has same playback rate.
  EXPECT_NE(old_loader, loader());

  EXPECT_TRUE(data_source_->loading());
  EXPECT_CALL(*this, ReadCallback(media::DataSource::kReadError));
  Stop();
}

TEST_F(BufferedDataSourceTest, Http_Read) {
  InitializeWith206Response();

  ReadAt(0);

  // Receive first half of the read.
  EXPECT_CALL(host_, AddBufferedByteRange(0, (kDataSize / 2) - 1));
  ReceiveData(kDataSize / 2);

  // Receive last half of the read.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  EXPECT_CALL(host_, AddBufferedByteRange(0, kDataSize - 1));
  ReceiveData(kDataSize / 2);

  EXPECT_TRUE(data_source_->downloading());
  Stop();
}

TEST_F(BufferedDataSourceTest, File_Read) {
  InitializeWithFileResponse();

  ReadAt(0);

  // Receive first half of the read but no buffering update.
  ReceiveData(kDataSize / 2);

  // Receive last half of the read but no buffering update.
  EXPECT_CALL(*this, ReadCallback(kDataSize));
  ReceiveData(kDataSize / 2);

  Stop();
}

}  // namespace webkit_media
