// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "base/debug/stack_trace.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/third_party/dynamic_annotations/dynamic_annotations.h"
#include "base/threading/platform_thread.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "content/child/child_process.h"
#include "content/common/content_constants_internal.h"
#include "content/common/media/media_config.h"
#include "content/common/media/media_messages.h"
#include "content/common/sandbox_linux/sandbox_linux.h"
#include "content/media/media_child_thread.h"
#include "content/media/media_process.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"

#if defined(USE_X11)
#include "ui/base/x/x11_util.h"
#endif

#if defined(OS_LINUX)
#include "content/public/common/sandbox_init.h"
#endif

#if defined(SANITIZER_COVERAGE)
#include <sanitizer/common_interface_defs.h>
#include <sanitizer/coverage_interface.h>
#endif

namespace content {

namespace {

bool WarmUpSandbox(const base::CommandLine& command_line);

#if defined(OS_LINUX)
bool StartSandboxLinux();
#endif

base::LazyInstance<MediaChildThread::DeferredMessages> deferred_messages =
    LAZY_INSTANCE_INITIALIZER;

bool MediaProcessLogMessageHandler(int severity,
                                   const char* file,
                                   int line,
                                   size_t message_start,
                                   const std::string& str) {
  std::string header = str.substr(0, message_start);
  std::string message = str.substr(message_start);
  deferred_messages.Get().push(
      new MediaHostMsg_OnLogMessage(severity, header, message));
  return false;
}

}  // namespace anonymous

// Main function for starting the media process.
int MediaMain(const MainFunctionParams& parameters) {

#if !defined(OFFICIAL_BUILD)
  base::debug::EnableInProcessStackDumping();
#endif

  TRACE_EVENT0("media", "MediaMain");
  base::trace_event::TraceLog::GetInstance()->SetProcessName("Media Process");
  base::trace_event::TraceLog::GetInstance()->SetProcessSortIndex(
      kTraceEventMediaProcessSortIndex);

  const base::CommandLine& command_line = parameters.command_line;
  if (command_line.HasSwitch(switches::kMediaStartupDialog)) {
    ChildProcess::WaitForDebugger("Media");
  }

  base::Time start_time = base::Time::Now();

  logging::SetLogMessageHandler(MediaProcessLogMessageHandler);
#if defined(OS_LINUX)
  base::MessageLoop main_message_loop(base::MessageLoop::TYPE_DEFAULT);
#else
  base::MessageLoop main_message_loop(base::MessageLoop::TYPE_IO);
#endif

  base::PlatformThread::SetName("CrMediaMain");

  bool initialized_sandbox = true;

  // Warm up resources
  if (WarmUpSandbox(command_line)) {
    initialized_sandbox = false;
  }

  if (!initialized_sandbox) {
    StartSandboxLinux();
  }

  logging::SetLogMessageHandler(NULL);

  MediaProcess media_process;

  MediaChildThread* child_thread =
      new MediaChildThread(false, deferred_messages.Get());
  while (!deferred_messages.Get().empty())
    deferred_messages.Get().pop();

  child_thread->Init(start_time);

  media_process.set_main_thread(child_thread);

  {
    TRACE_EVENT0("media", "Run Message Loop");
    main_message_loop.Run();
  }

  return 0;
}

namespace {

bool WarmUpSandbox(const base::CommandLine& command_line) {
  {
    TRACE_EVENT0("media", "Warm up rand");
    // Warm up the random subsystem, which needs to be done pre-sandbox on all
    // platforms.
    (void)base::RandUint64();
  }
  return true;
}

#if defined(OS_LINUX)
bool StartSandboxLinux() {
  TRACE_EVENT0("media", "Initialize sandbox");

  bool res = false;

// TODO: WarmUpSandbox -- access hardware resources?

#if defined(SANITIZER_COVERAGE)
  const std::string sancov_file_name =
      "media." + base::Uint64ToString(base::RandUint64());
  LinuxSandbox* linux_sandbox = LinuxSandbox::GetInstance();
  linux_sandbox->sanitizer_args()->coverage_sandboxed = 1;
  linux_sandbox->sanitizer_args()->coverage_fd =
      __sanitizer_maybe_open_cov_file(sancov_file_name.c_str());
  linux_sandbox->sanitizer_args()->coverage_max_block_size = 0;
#endif

  // LinuxSandbox::InitializeSandbox() must always be called
  // with only one thread.
  res = LinuxSandbox::InitializeSandbox();
  return res;
}
#endif  // defined(OS_LINUX)

}  // namespace.

}  // namespace content
