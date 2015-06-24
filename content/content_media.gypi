# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'conditions': [
    ['use_gstreamer==1', {
      'dependencies': [
        '../base/base.gyp:base',
        '../build/linux/system.gyp:gstreamer',
      ],
      'sources': [
        'media/in_process_media_thread.cc',
        'media/in_process_media_thread.h',
        'media/media_child_thread.cc',
        'media/media_child_thread.h',
        'media/media_main.cc',
        'media/media_process.cc',
        'media/media_process.h',
        'media/gstreamer/gpuprocess/gstglcontext_gpu_process.c',
        'media/gstreamer/gpuprocess/gstglcontext_gpu_process.h',
        'media/gstreamer/gpuprocess/gstgldisplay_gpu_process.c',
        'media/gstreamer/gpuprocess/gstgldisplay_gpu_process.h',
        'media/gstreamer/gpuprocess/gstglwindow_gpu_process.c',
        'media/gstreamer/gpuprocess/gstglwindow_gpu_process.h',
        'media/gstreamer/gst_chromium_http_source.cc',
        'media/gstreamer/gst_chromium_http_source.h',
        'media/gstreamer/media_player_gstreamer.cc',
        'media/gstreamer/media_player_gstreamer.h',
      ],
      'include_dirs': [
        '..',
      ],
    }],
  ],
}
