// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/fake_demuxer_stream.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/pickle.h"
#include "media/base/bind_to_loop.h"
#include "media/base/decoder_buffer.h"
#include "media/base/video_frame.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"

namespace media {

static const int kStartTimestampMs = 0;
static const int kDurationMs = 30;
static const int kStartWidth = 320;
static const int kStartHeight = 240;
static const int kWidthDelta = 4;
static const int kHeightDelta = 3;
static const char kFakeBufferHeader[] = "Fake Buffer";

FakeDemuxerStream::FakeDemuxerStream(int num_configs,
                                     int num_buffers_in_one_config,
                                     bool is_encrypted)
    : message_loop_(base::MessageLoopProxy::current()),
      num_configs_left_(num_configs),
      num_buffers_in_one_config_(num_buffers_in_one_config),
      is_encrypted_(is_encrypted),
      num_buffers_left_in_current_config_(num_buffers_in_one_config),
      num_buffers_returned_(0),
      current_timestamp_(base::TimeDelta::FromMilliseconds(kStartTimestampMs)),
      duration_(base::TimeDelta::FromMilliseconds(kDurationMs)),
      next_coded_size_(kStartWidth, kStartHeight),
      next_read_num_(0),
      read_to_hold_(-1) {
  DCHECK_GT(num_configs_left_, 0);
  DCHECK_GT(num_buffers_in_one_config_, 0);
  UpdateVideoDecoderConfig();
}

FakeDemuxerStream::~FakeDemuxerStream() {}

void FakeDemuxerStream::Read(const ReadCB& read_cb) {
  DCHECK(message_loop_->BelongsToCurrentThread());
  DCHECK(read_cb_.is_null());

  read_cb_ = BindToCurrentLoop(read_cb);

  if (read_to_hold_ == next_read_num_)
    return;

  DCHECK(read_to_hold_ == -1 || read_to_hold_ > next_read_num_);
  DoRead();
}

const AudioDecoderConfig& FakeDemuxerStream::audio_decoder_config() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  NOTREACHED();
  return audio_decoder_config_;
}

const VideoDecoderConfig& FakeDemuxerStream::video_decoder_config() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  return video_decoder_config_;
}

// TODO(xhwang): Support audio if needed.
DemuxerStream::Type FakeDemuxerStream::type() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  return VIDEO;
}

void FakeDemuxerStream::EnableBitstreamConverter() {
  DCHECK(message_loop_->BelongsToCurrentThread());
}

void FakeDemuxerStream::HoldNextRead() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  read_to_hold_ = next_read_num_;
}

void FakeDemuxerStream::HoldNextConfigChangeRead() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  // Set |read_to_hold_| to be the next config change read.
  read_to_hold_ = next_read_num_ + num_buffers_in_one_config_ -
                  next_read_num_ % (num_buffers_in_one_config_ + 1);
}

void FakeDemuxerStream::SatisfyRead() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  DCHECK_EQ(read_to_hold_, next_read_num_);
  DCHECK(!read_cb_.is_null());

  read_to_hold_ = -1;
  DoRead();
}

void FakeDemuxerStream::Reset() {
  read_to_hold_ = -1;

  if (!read_cb_.is_null())
    base::ResetAndReturn(&read_cb_).Run(kAborted, NULL);
}

void FakeDemuxerStream::UpdateVideoDecoderConfig() {
  const gfx::Rect kVisibleRect(kStartWidth, kStartHeight);
  video_decoder_config_.Initialize(
      kCodecVP8, VIDEO_CODEC_PROFILE_UNKNOWN, VideoFrame::YV12,
      next_coded_size_, kVisibleRect, next_coded_size_,
      NULL, 0, is_encrypted_, false);
  next_coded_size_.Enlarge(kWidthDelta, kHeightDelta);
}

void FakeDemuxerStream::DoRead() {
  DCHECK(message_loop_->BelongsToCurrentThread());
  DCHECK(!read_cb_.is_null());

  next_read_num_++;

  if (num_buffers_left_in_current_config_ == 0) {
    // End of stream.
    if (num_configs_left_ == 0) {
      base::ResetAndReturn(&read_cb_).Run(kOk,
                                          DecoderBuffer::CreateEOSBuffer());
      return;
    }

    // Config change.
    num_buffers_left_in_current_config_ = num_buffers_in_one_config_;
    UpdateVideoDecoderConfig();
    base::ResetAndReturn(&read_cb_).Run(kConfigChanged, NULL);
    return;
  }

  // Prepare the next buffer.
  Pickle pickle;
  pickle.WriteString(kFakeBufferHeader);
  pickle.WriteInt(video_decoder_config_.coded_size().width());
  pickle.WriteInt(video_decoder_config_.coded_size().height());
  pickle.WriteInt64(current_timestamp_.InMilliseconds());

  scoped_refptr<DecoderBuffer> buffer = DecoderBuffer::CopyFrom(
      static_cast<const uint8*>(pickle.data()), pickle.size());

  // TODO(xhwang): Output out-of-order buffers if needed.
  buffer->SetTimestamp(current_timestamp_);
  buffer->SetDuration(duration_);
  current_timestamp_ += duration_;

  num_buffers_left_in_current_config_--;
  if (num_buffers_left_in_current_config_ == 0)
    num_configs_left_--;

  num_buffers_returned_++;
  base::ResetAndReturn(&read_cb_).Run(kOk, buffer);
}

}  // namespace media
