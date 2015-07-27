// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IPC messages for android media player.
// Multiply-included message file, hence no include guard.

#include "base/basictypes.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/common/media/media_player_messages_enums_gstreamer.h"
#include "ipc/ipc_message_macros.h"
#include "url/gurl.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT
#define IPC_MESSAGE_START MediaPlayerMsgStart

// From render process to media process
// Start the player for playback.
IPC_MESSAGE_CONTROL3(MediaPlayerMsg_Load, int /* player_id */, GURL /* url */,
                     unsigned /* position_update_interval */)

IPC_MESSAGE_CONTROL1(MediaPlayerMsg_Start, int /* player_id */)

IPC_MESSAGE_CONTROL1(MediaPlayerMsg_Pause, int /* player_id */)

IPC_MESSAGE_CONTROL2(MediaPlayerMsg_Seek,
                     int /* player_id */,
                     base::TimeDelta /* time */)

IPC_MESSAGE_CONTROL1(MediaPlayerMsg_Release, int /* player_id */)

// The player is done with the current texture
IPC_MESSAGE_CONTROL2(MediaPlayerMsg_ReleaseTexture,
                     int /* player_id */,
                     unsigned /* texture_id */)

// Request the player to add a media source buffer.
IPC_MESSAGE_CONTROL4(MediaPlayerMsg_AddSourceId,
                     int /* player_id */,
                     std::string /* source id */,
                     std::string /* type */,
                     std::vector<std::string> /* codecs */)

// Request the player to remove a media source buffer.
IPC_MESSAGE_CONTROL2(MediaPlayerMsg_RemoveSourceId,
                     int /* player_id */,
                     std::string /* source id */)

// Request the player to set the new duration.
IPC_MESSAGE_CONTROL2(MediaPlayerMsg_SetDuration,
                     int /* player_id */,
                     base::TimeDelta /* duration */)

// Request the player to mark EOS the media source.
IPC_MESSAGE_CONTROL1(MediaPlayerMsg_MarkEndOfStream, int /* player_id */)

// Request the player to unmark EOS the media source.
IPC_MESSAGE_CONTROL1(MediaPlayerMsg_UnmarkEndOfStream, int /* player_id */)

// Request the player to set the MSE sequence mode.
IPC_MESSAGE_CONTROL3(MediaPlayerMsg_SetSequenceMode,
                     int /* player_id */,
                     std::string /* source id */,
                     bool /* sequence mode */)

// Request the player to process source buffer data.
IPC_MESSAGE_CONTROL4(MediaPlayerMsg_AppendData,
                     int /* player_id */,
                     std::string /* source id */,
                     std::vector<unsigned char> /* data */,
                     std::vector<base::TimeDelta> /* start, end, offset */)

// Request the player to abort the current segment and reset the parser.
IPC_MESSAGE_CONTROL2(MediaPlayerMsg_Abort,
                     int /* player_id */,
                     std::string /* source id */)

// Request the play to configure group timestamps for the sequence.
IPC_MESSAGE_CONTROL3(MediaPlayerMsg_SetGroupStartTimestampIfInSequenceMode,
                     int /* player_id */,
                     std::string /* source id */,
                     base::TimeDelta /* timestamp offset */)

// Request the play to remove a segment for a source buffer.
IPC_MESSAGE_CONTROL4(MediaPlayerMsg_RemoveSegment,
                     int /* player_id */,
                     std::string /* source id */,
                     base::TimeDelta /* start */,
                     base::TimeDelta /* end */)

// From media process to render process
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_MediaBufferingUpdate, int /* percent */)

// A media playback error has occurred.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_MediaError, int /* error */)

// Playback is completed.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_MediaPlaybackCompleted)

// Media metadata has changed.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_MediaDurationChanged,
                    base::TimeDelta /* duration */)

// Media seek is completed.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_SeekCompleted,
                    base::TimeDelta /* current_time */)

// Video size has changed.
IPC_MESSAGE_ROUTED2(MediaPlayerMsg_MediaVideoSizeChanged,
                    int /* width */,
                    int /* height */)

// The current play time has updated.
IPC_MESSAGE_ROUTED2(MediaPlayerMsg_MediaTimeUpdate,
                    base::TimeDelta /* current_timestamp */,
                    base::TimeTicks /* current_time_ticks */)

// The player has been released.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_MediaPlayerReleased)

// The player exited fullscreen.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_DidExitFullscreen)

// The player started playing.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_DidMediaPlayerPlay)

// The player was paused.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_DidMediaPlayerPause)

// The player is ready to use the current frame
IPC_MESSAGE_ROUTED4(MediaPlayerMsg_SetCurrentFrame,
                    int /* width */,
                    int /* height */,
                    unsigned /* texture_id */,
                    std::vector<int32_t> /* name */)

// The player has setup its source element.
IPC_MESSAGE_ROUTED0(MediaPlayerMsg_SourceSelected)

// The player has added a media source buffer.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_DidAddSourceId, std::string /* source id */)

// The player has removed a media source buffer.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_DidRemoveSourceId,
                    std::string /* source id */)

// The player validated a media source buffer.
IPC_MESSAGE_ROUTED1(MediaPlayerMsg_InitSegmentReceived,
                    std::string /* source id */)

// The player updated the time range for last append buffer.
IPC_MESSAGE_ROUTED2(MediaPlayerMsg_BufferedRangeUpdate,
                    std::string /* source id */,
                    std::vector<base::TimeDelta> /* ranges */)

// The player updated the timestamp offset for the next sequence.
IPC_MESSAGE_ROUTED2(MediaPlayerMsg_TimestampOffsetUpdate,
                    std::string /* source id */,
                    base::TimeDelta /* timestamp_offset */)
