// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/gstreamer/websourcebuffer_gstreamer.h"

#include <cmath>
#include <limits>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "content/renderer/media/gstreamer/webmediaplayer_gstreamer.h"
#include "media/base/timestamp_constants.h"
#include "third_party/WebKit/public/platform/WebSourceBufferClient.h"

namespace media {

static base::TimeDelta DoubleToTimeDelta(double time) {
  DCHECK(!std::isnan(time));
  DCHECK_NE(time, -std::numeric_limits<double>::infinity());

  if (time == std::numeric_limits<double>::infinity())
    return kInfiniteDuration;

  // Don't use base::TimeDelta::Max() here, as we want the largest finite time
  // delta.
  base::TimeDelta max_time = base::TimeDelta::FromInternalValue(
      std::numeric_limits<int64_t>::max() - 1);
  double max_time_in_seconds = max_time.InSecondsF();

  if (time >= max_time_in_seconds)
    return max_time;

  return base::TimeDelta::FromMicroseconds(time *
                                           base::Time::kMicrosecondsPerSecond);
}

WebSourceBufferGStreamer::WebSourceBufferGStreamer(
    const std::string& id,
    WebMediaPlayerMessageDispatcher* message_dispatcher)
    : id_(id),
      message_dispatcher_(message_dispatcher),
      client_(NULL),
      initialization_segment_received(false),
      append_window_end_(kInfiniteDuration) {
  DCHECK(message_dispatcher);
}

WebSourceBufferGStreamer::~WebSourceBufferGStreamer() {
  DCHECK(!message_dispatcher_)
      << "Object destroyed w/o removedFromMediaSource() call";
  DCHECK(!client_);
}

void WebSourceBufferGStreamer::setClient(blink::WebSourceBufferClient* client) {
  DCHECK(client);
  DCHECK(!client_);
  client_ = client;
}

bool WebSourceBufferGStreamer::setMode(WebSourceBuffer::AppendMode mode) {
  switch (mode) {
    case WebSourceBuffer::AppendModeSegments:
      message_dispatcher_->SendSetSequenceMode(id_, false);
      return true;
    case WebSourceBuffer::AppendModeSequence:
      message_dispatcher_->SendSetSequenceMode(id_, true);
      return true;
  }

  NOTREACHED();
  return false;
}

blink::WebTimeRanges WebSourceBufferGStreamer::buffered() {
  blink::WebTimeRanges result(ranges_.size());
  for (size_t i = 0; i < ranges_.size(); i++) {
    result[i].start = ranges_.start(i).InSecondsF();
    result[i].end = ranges_.end(i).InSecondsF();
  }

  return result;
}

double WebSourceBufferGStreamer::highestPresentationTimestamp() {
  return 0;
}

bool WebSourceBufferGStreamer::evictCodedFrames(double currentPlaybackTime,
                                                size_t newDataSize) {
  // TODO: implement message_dispatcher_->evictCodedFrames
  return true;
}

bool WebSourceBufferGStreamer::append(const unsigned char* data,
                                      unsigned length,
                                      double* timestamp_offset) {
  if (initialization_segment_received) {
    initialization_segment_received = false;
    std::vector<blink::WebSourceBufferClient::MediaTrackInfo> trackInfoVector;
    // TODO: get tracks information
/*
    for (const auto& track : tracks->tracks()) {
      trackInfoVector.push_back(
          std::make_tuple(mediaTrackTypeToBlink(track->type()),
                          blink::WebString::fromUTF8(track->id()),
                          blink::WebString::fromUTF8(track->kind()),
                          blink::WebString::fromUTF8(track->label()),
                          blink::WebString::fromUTF8(track->language())));
    }
*/
    client_->initializationSegmentReceived(trackInfoVector);
  }

  base::TimeDelta old_offset = timestamp_offset_;

  message_dispatcher_->SendAppendData(id_, data, length, append_window_start_,
                                      append_window_end_, timestamp_offset_);

  // Coded frame processing may update the timestamp offset. If the caller
  // provides a non-NULL |timestamp_offset| and frame processing changes the
  // timestamp offset, report the new offset to the caller. Do not update the
  // caller's offset otherwise, to preserve any pre-existing value that may have
  // more than microsecond precision.
  if (timestamp_offset && old_offset != timestamp_offset_)
    *timestamp_offset = timestamp_offset_.InSecondsF();

  return true;
}

void WebSourceBufferGStreamer::resetParserState() {
  message_dispatcher_->SendAbort(id_);
}

void WebSourceBufferGStreamer::remove(double start, double end) {
  DCHECK_GE(start, 0);
  DCHECK_GE(end, 0);
  message_dispatcher_->SendRemoveSegment(id_, DoubleToTimeDelta(start),
                                         DoubleToTimeDelta(end));
}

bool WebSourceBufferGStreamer::setTimestampOffset(double offset) {
  timestamp_offset_ = DoubleToTimeDelta(offset);

  // http://www.w3.org/TR/media-source/#widl-SourceBuffer-timestampOffset
  // Step 6: If the mode attribute equals "sequence", then set the group start
  // timestamp to new timestamp offset.
  message_dispatcher_->SendSetGroupStartTimestampIfInSequenceMode(
      id_, timestamp_offset_);
  return true;
}

void WebSourceBufferGStreamer::setAppendWindowStart(double start) {
  DCHECK_GE(start, 0);
  append_window_start_ = DoubleToTimeDelta(start);
}

void WebSourceBufferGStreamer::setAppendWindowEnd(double end) {
  DCHECK_GE(end, 0);
  append_window_end_ = DoubleToTimeDelta(end);
}

void WebSourceBufferGStreamer::removedFromMediaSource() {
  message_dispatcher_->SendRemoveSourceId(id_);
  message_dispatcher_ = NULL;
  client_ = NULL;
}

void WebSourceBufferGStreamer::OnInitSegmentReceived() {
  DVLOG(1) << __FUNCTION__;
  // Cannot notify the client here because SourceBuffer
  // might be updating. Indeed there is ASSERT(m_updating)
  // in SourceBuffer::initializationSegmentReceived().
  initialization_segment_received = true;
}

void WebSourceBufferGStreamer::OnBufferedRangeUpdate(
    const Ranges<base::TimeDelta>& ranges) {
  DVLOG(1) << __FUNCTION__;
  ranges_ = ranges;
}

void WebSourceBufferGStreamer::OnTimestampOffsetUpdate(
    const base::TimeDelta& timestamp_offset) {
  DVLOG(1) << __FUNCTION__;
  timestamp_offset_ = timestamp_offset;
}

}  // namespace media
