// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_MEDIA_MEDIA_PROCESS_LAUNCH_CAUSES_H_
#define CONTENT_COMMON_MEDIA_MEDIA_PROCESS_LAUNCH_CAUSES_H_

namespace content {

enum CauseForMediaLaunch {
  // Start enum from 2 to keep the same values for the histogram.
  CAUSE_FOR_MEDIA_LAUNCH_NO_LAUNCH = 2,
  CAUSE_FOR_MEDIA_LAUNCH_BROWSER_STARTUP,
  CAUSE_FOR_MEDIA_LAUNCH_RENDERER,

  // All new values should be inserted above this point so that
  // existing values continue to match up with those in histograms.xml.
  CAUSE_FOR_MEDIA_LAUNCH_MAX_ENUM
};

}  // namespace content

#endif  // CONTENT_COMMON_MEDIA_MEDIA_PROCESS_LAUNCH_CAUSES_H_
