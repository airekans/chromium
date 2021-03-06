// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/mocks/test_media_stream_client.h"

#include "googleurl/src/gurl.h"
#include "media/base/media_log.h"
#include "media/base/pipeline.h"
#include "third_party/WebKit/public/platform/WebMediaStream.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebMediaStreamRegistry.h"
#include "webkit/renderer/media/media_stream_audio_renderer.h"
#include "webkit/renderer/media/simple_video_frame_provider.h"
#include "webkit/renderer/media/webmediaplayer_impl.h"
#include "webkit/renderer/media/webmediaplayer_ms.h"
#include "webkit/renderer/media/webmediaplayer_params.h"

using namespace WebKit;

namespace {

static const int kVideoCaptureWidth = 352;
static const int kVideoCaptureHeight = 288;
static const int kVideoCaptureFrameDurationMs = 33;

bool IsMockMediaStreamWithVideo(const WebURL& url) {
#if ENABLE_WEBRTC
  WebMediaStream descriptor(
      WebMediaStreamRegistry::lookupMediaStreamDescriptor(url));
  if (descriptor.isNull())
    return false;
  WebVector<WebMediaStreamTrack> videoSources;
  descriptor.videoSources(videoSources);
  return videoSources.size() > 0;
#else
  return false;
#endif
}

}  // namespace

namespace webkit_glue {

WebKit::WebMediaPlayer* CreateMediaPlayer(
    WebFrame* frame,
    const WebURL& url,
    WebMediaPlayerClient* client,
    webkit_media::MediaStreamClient* media_stream_client) {
  if (media_stream_client && media_stream_client->IsMediaStream(url)) {
    return new webkit_media::WebMediaPlayerMS(
        frame,
        client,
        base::WeakPtr<webkit_media::WebMediaPlayerDelegate>(),
        media_stream_client,
        new media::MediaLog());
  }

#if defined(OS_ANDROID)
  return NULL;
#else
  webkit_media::WebMediaPlayerParams params(
      NULL, NULL, new media::MediaLog());
  return new webkit_media::WebMediaPlayerImpl(
      frame,
      client,
      base::WeakPtr<webkit_media::WebMediaPlayerDelegate>(),
      params);
#endif
}

TestMediaStreamClient::TestMediaStreamClient() {}

TestMediaStreamClient::~TestMediaStreamClient() {}

bool TestMediaStreamClient::IsMediaStream(const GURL& url) {
  return IsMockMediaStreamWithVideo(url);
}

scoped_refptr<webkit_media::VideoFrameProvider>
TestMediaStreamClient::GetVideoFrameProvider(
    const GURL& url,
    const base::Closure& error_cb,
    const webkit_media::VideoFrameProvider::RepaintCB& repaint_cb) {
  if (!IsMockMediaStreamWithVideo(url))
    return NULL;

  return new webkit_media::SimpleVideoFrameProvider(
      gfx::Size(kVideoCaptureWidth, kVideoCaptureHeight),
      base::TimeDelta::FromMilliseconds(kVideoCaptureFrameDurationMs),
      error_cb,
      repaint_cb);
}

scoped_refptr<webkit_media::MediaStreamAudioRenderer>
TestMediaStreamClient::GetAudioRenderer(const GURL& url) {
  return NULL;
}

}  // namespace webkit_glue
