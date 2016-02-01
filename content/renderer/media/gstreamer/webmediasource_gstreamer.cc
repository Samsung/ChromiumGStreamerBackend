// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/gstreamer/webmediasource_gstreamer.h"

#include "base/guid.h"
#include "content/renderer/media/gstreamer/webmediaplayer_gstreamer.h"
#include "content/renderer/media/gstreamer/websourcebuffer_gstreamer.h"
#include "media/base/mime_util.h"
#include "media/blink/websourcebuffer_impl.h"
#include "media/filters/chunk_demuxer.h"
#include "third_party/WebKit/public/platform/WebCString.h"
#include "third_party/WebKit/public/platform/WebString.h"

using ::blink::WebString;
using ::blink::WebMediaSource;

namespace media {

#define STATIC_ASSERT_MATCHING_STATUS_ENUM(webkit_name, chromium_name) \
  static_assert(static_cast<int>(WebMediaSource::webkit_name) ==       \
                    static_cast<int>(ChunkDemuxer::chromium_name),     \
                "mismatching status enum values: " #webkit_name)
STATIC_ASSERT_MATCHING_STATUS_ENUM(AddStatusOk, kOk);
STATIC_ASSERT_MATCHING_STATUS_ENUM(AddStatusNotSupported, kNotSupported);
STATIC_ASSERT_MATCHING_STATUS_ENUM(AddStatusReachedIdLimit, kReachedIdLimit);
#undef STATIC_ASSERT_MATCHING_STATUS_ENUM

WebMediaSourceGStreamer::WebMediaSourceGStreamer(
    WebMediaPlayerGStreamer* player,
    WebMediaPlayerMessageDispatcher* message_dispatcher,
    const SetNetworkStateCB& set_network_state_cb)
    : player_(player),
      message_dispatcher_(message_dispatcher),
      set_network_state_cb_(set_network_state_cb) {
  DCHECK(player_);
  DCHECK(message_dispatcher_);
}

WebMediaSourceGStreamer::~WebMediaSourceGStreamer() {
  player_->OnSourceDeleted(this);
}

WebMediaSource::AddStatus WebMediaSourceGStreamer::addSourceBuffer(
    const blink::WebString& type,
    const blink::WebString& codecs,
    blink::WebSourceBuffer** source_buffer) {
  std::string id = base::GenerateGUID();

  std::vector<std::string> parsed_codec_ids;
  media::ParseCodecString(codecs.utf8().data(), &parsed_codec_ids, false);

  if (!message_dispatcher_->SendAddSourceId(id, type.utf8().data(),
                                            parsed_codec_ids))
    return WebMediaSource::AddStatusNotSupported;

  WebSourceBufferGStreamer* source_buffer_gstreamer =
      new WebSourceBufferGStreamer(id, message_dispatcher_);

  *source_buffer = source_buffer_gstreamer;
  source_buffers_[id] = source_buffer_gstreamer;

  return WebMediaSource::AddStatusOk;
}

double WebMediaSourceGStreamer::duration() {
  return player_->duration();
}

void WebMediaSourceGStreamer::setDuration(double new_duration) {
  DCHECK_GE(new_duration, 0);
  message_dispatcher_->SendSetDuration(
      base::TimeDelta::FromSecondsD(new_duration));
}

void WebMediaSourceGStreamer::markEndOfStream(
    WebMediaSource::EndOfStreamStatus) {
  message_dispatcher_->SendMarkEndOfStream();
}

void WebMediaSourceGStreamer::unmarkEndOfStream() {
  message_dispatcher_->SendUnmarkEndOfStream();
}

void WebMediaSourceGStreamer::OnAddSourceId(const std::string& id) {
  if (id.empty() || source_buffers_.find(id) == source_buffers_.end())
    set_network_state_cb_.Run(blink::WebMediaPlayer::NetworkStateFormatError);
}

void WebMediaSourceGStreamer::OnRemoveSourceId(const std::string& id) {
  WebSourceBufferGStreamerMap::iterator iter = source_buffers_.find(id);
  if (iter == source_buffers_.end())
    set_network_state_cb_.Run(blink::WebMediaPlayer::NetworkStateFormatError);
  else
    source_buffers_.erase(iter);
}

void WebMediaSourceGStreamer::OnInitSegmentReceived(const std::string& id) {
  WebSourceBufferGStreamerMap::iterator iter = source_buffers_.find(id);

  if (iter == source_buffers_.end())
    set_network_state_cb_.Run(blink::WebMediaPlayer::NetworkStateFormatError);
  else
    iter->second->OnInitSegmentReceived();
}

void WebMediaSourceGStreamer::OnBufferedRangeUpdate(
    const std::string& id,
    const Ranges<base::TimeDelta>& ranges) {
  WebSourceBufferGStreamerMap::iterator iter = source_buffers_.find(id);

  if (iter == source_buffers_.end())
    set_network_state_cb_.Run(blink::WebMediaPlayer::NetworkStateFormatError);
  else
    iter->second->OnBufferedRangeUpdate(ranges);
}

void WebMediaSourceGStreamer::OnTimestampOffsetUpdate(
    const std::string& id,
    const base::TimeDelta& timestamp_offset) {
  WebSourceBufferGStreamerMap::iterator iter = source_buffers_.find(id);

  if (iter == source_buffers_.end())
    set_network_state_cb_.Run(blink::WebMediaPlayer::NetworkStateFormatError);
  else
    iter->second->OnTimestampOffsetUpdate(timestamp_offset);
}

}  // namespace media
