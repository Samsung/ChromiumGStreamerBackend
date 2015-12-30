// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/sandbox_linux/bpf_media_policy_linux.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "content/common/sandbox_linux/sandbox_bpf_base_policy_linux.h"
#include "content/common/sandbox_linux/sandbox_seccomp_bpf_linux.h"
#include "content/common/set_process_title.h"
#include "content/public/common/content_switches.h"
#include "sandbox/linux/bpf_dsl/bpf_dsl.h"
#include "sandbox/linux/bpf_dsl/bpf_dsl_impl.h"
#include "sandbox/linux/bpf_dsl/policy_compiler.h"
#include "sandbox/linux/seccomp-bpf/die.h"
#include "sandbox/linux/seccomp-bpf/sandbox_bpf.h"
#include "sandbox/linux/seccomp-bpf/syscall.h"
#include "sandbox/linux/seccomp-bpf-helpers/sigsys_handlers.h"
#include "sandbox/linux/seccomp-bpf-helpers/syscall_parameters_restrictions.h"
#include "sandbox/linux/seccomp-bpf-helpers/syscall_sets.h"
#include "sandbox/linux/syscall_broker/broker_file_permission.h"
#include "sandbox/linux/syscall_broker/broker_process.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"

using sandbox::arch_seccomp_data;
using sandbox::bpf_dsl::Allow;
using sandbox::bpf_dsl::Error;
using sandbox::bpf_dsl::Arg;
using sandbox::bpf_dsl::If;
using sandbox::bpf_dsl::ResultExpr;
using sandbox::bpf_dsl::Trap;
using sandbox::bpf_dsl::UnsafeTrap;
using sandbox::syscall_broker::BrokerFilePermission;
using sandbox::syscall_broker::BrokerProcess;
using sandbox::SyscallSets;

namespace content {

namespace {

intptr_t MediaSIGSYS_Handler(const struct arch_seccomp_data& args,
                             void* aux_broker_process) {
  RAW_CHECK(aux_broker_process);
  BrokerProcess* broker_process =
      static_cast<BrokerProcess*>(aux_broker_process);

  // For debuging purposes:
  // It is possible to do the system call in this handler (instead of doing
  // it in the broker process) by just uncommenting the following line:
  // return sandbox::SandboxBPF::ForwardSyscall(args);

  switch (args.nr) {
#if !defined(__aarch64__)
    case __NR_access: {
      intptr_t retv =
          broker_process->Access(reinterpret_cast<const char*>(args.args[0]),
                                 static_cast<int>(args.args[1]));
      if (retv < 1) {
        struct stat sb;
        if (!stat(reinterpret_cast<const char*>(args.args[0]), &sb)) {
          VLOG(1) << "access failed: "
                  << reinterpret_cast<const char*>(args.args[0])
                  << static_cast<int>(args.args[1]);
        }
      }
      return retv;
    }
    case __NR_open:
#if defined(MEMORY_SANITIZER)
      // http://crbug.com/372840
      __msan_unpoison_string(reinterpret_cast<const char*>(args.args[0]));
#endif
      {
        intptr_t retv =
            broker_process->Open(reinterpret_cast<const char*>(args.args[0]),
                                 static_cast<int>(args.args[1]));
        if (retv < 1) {
          struct stat sb;
          if (!stat(reinterpret_cast<const char*>(args.args[0]), &sb)) {
            VLOG(1) << "open failed: "
                    << reinterpret_cast<const char*>(args.args[0]);
          }
        }
        return retv;
      }
#endif  // !defined(__aarch64__)
    case __NR_faccessat:
      if (static_cast<int>(args.args[0]) == AT_FDCWD) {
        intptr_t retv =
            broker_process->Access(reinterpret_cast<const char*>(args.args[1]),
                                   static_cast<int>(args.args[2]));
        if (retv < 1) {
          struct stat sb;
          if (!stat(reinterpret_cast<const char*>(args.args[1]), &sb)) {
            VLOG(1) << "faccessat failed: "
                    << reinterpret_cast<const char*>(args.args[1]);
          }
        }
        return retv;
      } else {
        return -EPERM;
      }
    case __NR_openat:
      // Allow using openat() as open().
      if (static_cast<int>(args.args[0]) == AT_FDCWD) {
        intptr_t retv =
            broker_process->Open(reinterpret_cast<const char*>(args.args[1]),
                                 static_cast<int>(args.args[2]));
        if (retv < 1) {
          struct stat sb;
          if (!stat(reinterpret_cast<const char*>(args.args[1]), &sb)) {
            VLOG(1) << "openat failed: "
                    << reinterpret_cast<const char*>(args.args[1]);
          }
        }
        return retv;
      } else {
        return -EPERM;
      }
    default:
      RAW_CHECK(false);
      return -ENOSYS;
  }
}

class MediaBrokerProcessPolicy : public MediaProcessPolicy {
 public:
  static sandbox::bpf_dsl::Policy* Create() {
    return new MediaBrokerProcessPolicy();
  }
  ~MediaBrokerProcessPolicy() override {}

  ResultExpr EvaluateSyscall(int system_call_number) const override;

 private:
  MediaBrokerProcessPolicy() {}
  DISALLOW_COPY_AND_ASSIGN(MediaBrokerProcessPolicy);
};

// x86_64/i386 or desktop ARM.
ResultExpr MediaBrokerProcessPolicy::EvaluateSyscall(int sysno) const {
  switch (sysno) {
#if !defined(__aarch64__)
    case __NR_access:
    case __NR_open:
#endif  // !defined(__aarch64__)
    case __NR_faccessat:
    case __NR_openat:
    // The broker process needs to able to unlink the temporary
    // files that it may create.
    case __NR_unlink:
      return Allow();
    default:
      return MediaProcessPolicy::EvaluateSyscall(sysno);
  }
}

void UpdateProcessTypeToMediaBroker() {
  base::CommandLine::StringVector exec =
      base::CommandLine::ForCurrentProcess()->GetArgs();
  base::CommandLine::Reset();
  base::CommandLine::Init(0, NULL);
  base::CommandLine::ForCurrentProcess()->InitFromArgv(exec);
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kProcessType, "media-broker");

  // Update the process title. The argv was already cached by the call to
  // SetProcessTitleFromCommandLine in content_main_runner.cc, so we can pass
  // NULL here (we don't have the original argv at this point).
  SetProcessTitleFromCommandLine(NULL);
}

bool UpdateProcessTypeAndEnableSandbox(
    sandbox::bpf_dsl::Policy* (*broker_sandboxer_allocator)(void)) {
  DCHECK(broker_sandboxer_allocator);
  UpdateProcessTypeToMediaBroker();
  return SandboxSeccompBPF::StartSandboxWithExternalPolicy(
      make_scoped_ptr(broker_sandboxer_allocator()), base::ScopedFD());
}

}  // namespace

MediaProcessPolicy::MediaProcessPolicy() : broker_process_(NULL) {}

MediaProcessPolicy::~MediaProcessPolicy() {}

// Main policy for x86_64/i386.
ResultExpr MediaProcessPolicy::EvaluateSyscall(int sysno) const {
  // Uncomment next lines to enable UnsafeTrap below:
  // if (sandbox::SandboxBPF::IsRequiredForUnsafeTrap(sysno))
  //   return Allow();

  switch (sysno) {
#if !defined(NDEBUG)
    // TODO: check if this is really necessary for frame steping with gdb.
    case __NR_pause:
      return Allow();
#endif

    case __NR_clock_getres:
    // Allow the system calls below.
    case __NR_fdatasync:
#if defined(__i386__) || defined(__x86_64__) || defined(__mips__)
    case __NR_getrlimit:
#endif
#if defined(__i386__) || defined(__arm__)
    case __NR_ugetrlimit:
#endif
    case __NR_mremap:  // See crbug.com/149834.
    case __NR_pread64:
    case __NR_pwrite64:
    case __NR_sched_get_priority_max:
    case __NR_sched_get_priority_min:
    case __NR_sysinfo:
    case __NR_times:
      return Allow();

#if !defined(OS_CHROMEOS)
    case __NR_ftruncate:
#endif
    case __NR_fsync:
#if !defined(__aarch64__)
    // TODO: investigate who is using it in order to remove it.
    case __NR_getdents:  // EPERM not a valid errno.
#endif
    case __NR_getdents64:  // EPERM not a valid errno.
    case __NR_futex:
    case __NR_ioctl:
#if defined(__i386__) || defined(__x86_64__) || defined(__mips__)
    // The Nvidia driver uses flags not in the baseline policy
    // (MAP_LOCKED | MAP_EXECUTABLE | MAP_32BIT)
    case __NR_mmap:
#endif
    // We also hit this on the linux_chromeos bot but don't yet know what
    // weird flags were involved.
    case __NR_mprotect:
#if defined(__i386__) || defined(__arm__) || defined(__mips__)
    case __NR_stat64:
    case __NR_statfs64:
#endif
    case __NR_stat:
    case __NR_statfs:  // Required for shm_open.
    case __NR_unlink:
    case __NR_uname:
      return Allow();

    // Pulse audio, see pulsecore/core-utils.c::pa_make_secure_dir
    case __NR_fchown:
#if defined(__i386__) || defined(__arm__)
    case __NR_fchown32:
#endif
    case __NR_lstat:
    case __NR_mkdir:
      return Allow();

// SECCOMP_RET_TRAP:
// Send a catchable SIGSYS, giving a chance to emulate the
// syscall. Emulation is done in the media broker process.
#if !defined(__aarch64__)
    case __NR_access:
    case __NR_open:
#endif  // !defined(__aarch64__)
    case __NR_faccessat:
    case __NR_openat: {
      DCHECK(broker_process_);
      return Trap(MediaSIGSYS_Handler, broker_process_);

      // For debuging purposes in order to use SandboxBPF::ForwardSyscall
      // in MediaSIGSYS_Handler you need to set CHROME_SANDBOX_DEBUGGING=1
      // and uncomment the following line:
      // return UnsafeTrap(MediaSIGSYS_Handler, broker_process_);
    }

    // Other cases:
    default:
      if (SyscallSets::IsEventFd(sysno))
        return Allow();

      if (SyscallSets::IsUmask(sysno) ||
          SyscallSets::IsDeniedFileSystemAccessViaFd(sysno)) {
        return Error(EPERM);
      }

      // Debug
      if (SyscallSets::IsDebug(sysno))
        return Error(EPERM);

      // Kernel
      if (SyscallSets::IsKernelInternalApi(sysno) ||
          SyscallSets::IsKernelModule(sysno))
        return Error(EPERM);

      // System
      if (SyscallSets::IsAdminOperation(sysno) ||
          SyscallSets::IsAnySystemV(sysno) ||
          SyscallSets::IsGlobalFSViewChange(sysno))
        return Error(EPERM);

      // Seccomp
      if (SyscallSets::IsSeccomp(sysno))
        return Error(EPERM);

// Network: (duplicate of SandboxBPFBasePolicy::EvaluateSyscall)
#if defined(__x86_64__)
      if (sysno == __NR_socketpair) {
        // Only allow AF_UNIX, PF_UNIX. Crash if anything else is seen.
        static_assert(AF_UNIX == PF_UNIX,
                      "af_unix and pf_unix should not be different");
        const Arg<int> domain(0);
        return If(domain == AF_UNIX, Allow()).Else(sandbox::CrashSIGSYS());
      }

      // Pulseaudio, see socket-client.c::do_call
      if (sysno == __NR_getsockopt) {
        const Arg<int> level(1);
        return If(level == SOL_SOCKET, Allow()).Else(sandbox::CrashSIGSYS());
      }

      // Pulseaudio, see client-conf-x11::xcb_connect
      switch (sysno) {
#if defined(__x86_64__) || defined(__arm__) || defined(__mips__) || \
    defined(__aarch64__)
        case __NR_connect:
          return Allow();
        case __NR_socket:
          const Arg<int> domain(0);
          const Arg<int> type(1);
          return If(AllOf(domain == AF_UNIX, type == SOCK_STREAM), Allow())
              .ElseIf(AnyOf(domain == AF_UNIX, type == SOCK_DGRAM), Allow())
              .Else(Error(EPERM));
      }
#endif

      if (SyscallSets::IsNetworkSocketInformation(sysno) ||
          SyscallSets::IsDeniedGetOrModifySocket(sysno)) {
        return Error(EPERM);
      }
#endif

      // Process
      if (SyscallSets::IsCurrentDirectory(sysno) ||
          SyscallSets::IsFileSystem(sysno) ||
          SyscallSets::IsAllowedProcessStartOrDeath(sysno) ||
          SyscallSets::IsProcessGroupOrSession(sysno) ||
          SyscallSets::IsProcessPrivilegeChange(sysno))
        return Error(EPERM);

      return SandboxBPFBasePolicy::EvaluateSyscall(sysno);
  }
}

bool MediaProcessPolicy::PreSandboxHook() {
  // Warm up resources needed by the policy we're about to enable and
  // eventually start a broker process.

  DCHECK(!broker_process());
  // Create a new broker process.
  InitMediaBrokerProcess(
      MediaBrokerProcessPolicy::Create,
      std::vector<BrokerFilePermission>());  // No extra files in whitelist.
  return true;
}

static void splitAndAddReadOnlyPermission(const char* path,
    const std::string& delimiter,
    std::vector<BrokerFilePermission>& permissions)
{
  if (path) {
    std::vector<std::string> results;
    base::SplitStringUsingSubstr(path, delimiter, &results);
    for (auto it = results.begin(); it != results.end(); ++it) {
      if (!it->empty()) {
        if (*(it->rbegin()) != '/') {
          permissions.push_back(
              BrokerFilePermission::ReadOnly(*it));
          it->append("/");
        }
        permissions.push_back(
            BrokerFilePermission::ReadOnlyRecursive(*it));
      }
    }
  }
}

void MediaProcessPolicy::InitMediaBrokerProcess(
    sandbox::bpf_dsl::Policy* (*broker_sandboxer_allocator)(void),
    const std::vector<BrokerFilePermission>& permissions_extra) {
  CHECK(broker_process_ == NULL);

// dev folders
#if !defined(NDEBUG)
  // Core dumps. Can be chromium/src too. Use ulimit -c unlimited.
  static const char kVarCrashPath[] = "/var/crash/";
#endif

#if !defined(OFFICIAL_BUILD)
  // Gst plugins path
  static const char* kGstPluginPath = getenv("GST_PLUGIN_PATH");
  static const char* kGstPluginScanner = getenv("GST_PLUGIN_SCANNER");
  static const char* kGstRegistry = getenv("GST_REGISTRY");
  static const char* kLdLibraryPath = getenv("LD_LIBRARY_PATH");
#endif

  // Pulse audio.
  static const char kDevShmPath[] = "/dev/shm/";
  static const char kUsrLibPulseModulesPath[] = "/usr/lib/pulse-4.0/modules/";
  static const char kEtcPulsePath[] = "/etc/pulse/";
  const std::string kDevRunUserPulse =
      std::string(getenv("XDG_RUNTIME_DIR")) + "/pulse";
  const std::string kDevRunUserPulsePath =
      std::string(getenv("XDG_RUNTIME_DIR")) + "/pulse/";
  const std::string kHomePulsePath =
      std::string(getenv("HOME")) + "/.pulse/";
  const std::string kHomeConfigPulsePath =
      std::string(getenv("HOME")) + "/.config/pulse/";
  const std::string kHomeXauth =
      std::string(getenv("HOME")) + "/.Xauthority";

  // GStreamer registry:
  // TODO: Implement more fine grained approach
  const std::string kHomeCacheGstreamerPath =
      std::string(getenv("HOME")) + "/.cache/gstreamer-1.0/";
  const std::string kHomeCacheGstreamerRegistry =
      std::string(getenv("HOME")) + "/.cache/gstreamer-1.0/registry.x86_64.bin";

  // Shared libraries.
  // TODO: Implement more fine grained approach to only allow
  // required gstreamer plugins and its deps.
  static const char kDevLibPath[] = "/lib/";
  static const char kDevLib64Path[] = "/lib64/";
  static const char kDevUSRLib32Path[] = "/usr/lib32/";
  static const char kDevUSRLib64Path[] = "/usr/lib64/";
  static const char kDevUSRLocalLib32Path[] = "/usr/local/lib32/";
  static const char kDevUSRLocalLib64Path[] = "/usr/local/lib64/";
  static const char kDevUSRLibPath[] = "/usr/lib/";
  static const char kDevUSRLocalLibPath[] = "/usr/local/lib/";

  // TODO: check if these are required.
  static const char kLDCachePath[] = "/etc/ld.so.cache";
  static const char kDevUSRSharePath[] = "/usr/share/";
  static const char kDevUSRLocalSharePath[] = "/usr/local/share/";
  static const char kVarLibDBUSMachineID[] = "/var/lib/dbus/machine-id";

  std::vector<BrokerFilePermission> permissions;

#if !defined(NDEBUG)
  permissions.push_back(
      BrokerFilePermission::ReadWriteCreateRecursive(kVarCrashPath));
#endif

#if !defined(OFFICIAL_BUILD)
  if (kGstRegistry)
    permissions.push_back(
        BrokerFilePermission::ReadOnly(kGstRegistry));

  splitAndAddReadOnlyPermission(kGstPluginPath, ":", permissions);
  splitAndAddReadOnlyPermission(kLdLibraryPath, ":", permissions);
  splitAndAddReadOnlyPermission(kGstPluginScanner, ":", permissions);
#endif

  // Pulse audio
  permissions.push_back(
      BrokerFilePermission::ReadWriteCreateRecursive(kDevShmPath));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kUsrLibPulseModulesPath));
  permissions.push_back(BrokerFilePermission::ReadOnlyRecursive(kEtcPulsePath));
  permissions.push_back(BrokerFilePermission::ReadWriteCreateUnlinkRecursive(
      kDevRunUserPulsePath));
  permissions.push_back(
      BrokerFilePermission::ReadWriteCreate(kDevRunUserPulse));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kHomePulsePath));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kHomeConfigPulsePath));
  permissions.push_back(BrokerFilePermission::ReadOnly(kHomeXauth));

  // GStreamer registry
  permissions.push_back(BrokerFilePermission::ReadWriteCreateUnlinkRecursive(
      kHomeCacheGstreamerPath));
  permissions.push_back(
      BrokerFilePermission::ReadOnly(kHomeCacheGstreamerRegistry));

  // Shared libraries.
  permissions.push_back(BrokerFilePermission::ReadOnlyRecursive(kDevLibPath));
  permissions.push_back(BrokerFilePermission::ReadOnlyRecursive(kDevLib64Path));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLib32Path));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLib64Path));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLocalLib32Path));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLocalLib64Path));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLibPath));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLocalLibPath));

  // TODO: check if this is required.
  permissions.push_back(BrokerFilePermission::ReadOnly(kLDCachePath));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRSharePath));
  permissions.push_back(
      BrokerFilePermission::ReadOnlyRecursive(kDevUSRLocalSharePath));
  permissions.push_back(BrokerFilePermission::ReadOnly(kVarLibDBUSMachineID));

  // Add eventual extra files from permissions_extra.
  for (const auto& perm : permissions_extra) {
    permissions.push_back(perm);
  }

  broker_process_ = new BrokerProcess(GetFSDeniedErrno(), permissions);
  // The initialization callback will perform generic initialization and then
  // call broker_sandboxer_callback.
  CHECK(broker_process_->Init(base::Bind(&UpdateProcessTypeAndEnableSandbox,
                                         broker_sandboxer_allocator)));
}

}  // namespace content
