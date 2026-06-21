/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "dumpstate"

#include "DumpstateUtil.h"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <dirent.h>
#include <fcntl.h>
#include <log/log.h>
#include <poll.h>
#include <sys/pidfd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__BIONIC__)
#include <sys/pidfd.h>
#else
// Not available in glibc.
// See: https://man7.org/linux/man-pages/man2/pidfd_open.2.html#SYNOPSIS
// This allows the file to compile, but we also check that pidfd_open
// returns a sensible value in tests.
#include <sys/syscall.h>
static int pidfd_open(pid_t pid, unsigned int flags) {
    return syscall(SYS_pidfd_open, pid, flags);
}
#endif

#include <vector>

#include "DumpstateInternal.h"

namespace android {
namespace os {
namespace dumpstate {

namespace {

static constexpr const char* kSuPath = "/system/xbin/su";

struct ExitStatus {
    // true: exited, see exit_code_or_signal for exit code, zero means normal
    // exit.
    // false: terminated, see exit_code_or_signal for signal that terminated it,
    // but zero means abnormal exit.
    bool exited = false;
    int exit_code_or_signal = 0;
};

// Wait for process `pid` to stop executing, or a timeout in milliseconds.
//
// Args:
// - `pid`: the process ID of the process to wait on.
// - `timeout_ms`: timeout in milliseconds to give up after.
// - `ret_on_signal`: whether to treat subprocess death as an error. `false`
//    mostly, but `true` when we use this wait after we send the process
//    a signal.
// - `status`: populated process details.
//
// Returns:
// - `true` if the process terminated normally. status is populated.
// - `false` otherwise:
//   - process was terminated by a signal, `status.exit_code_or_signal` is
//     populated.
//   - setup has failed, in which case `status.exit_code_or_signal == 0`,
static bool waitpid_with_timeout(
        pid_t pid, int timeout_ms, bool ret_on_signal, ExitStatus* status) {
    // `pidfd_open` should work even if the process that `pid` refers to has
    // terminated between we created it with `fork` and when we get here.
    //
    // Exceptions noted in pidfd_open(2) section NOTES should not apply, listed
    // here for easy referencing:
    // - the disposition of SIGCHLD has not been explicitly set to SIG_IGN
    //   (see sigaction(2));
    // - the SA_NOCLDWAIT flag was not specified while establishing a handler
    //   for SIGCHLD or while setting the disposition of that signal to SIG_DFL
    //   (see sigaction(2)); and
    // - the zombie process was not reaped elsewhere in the program (e.g.,
    //   either by an asynchronously executed signal handler or by wait(2)
    //   or similar in another thread).
    int fd = pidfd_open(pid, 0);
    if (fd < 0) {
        if (errno == ESRCH) {
            // There is no such PID, presumably this should be OK if the process
            // is already done. This will not happen when we actually wait
            // for the process to complete.
            //
            // But, we also use this function in calling code to
            // check if the process has already exited and figure out if
            // it should be sent TERM and KILL. If the process is no
            // longer present, we would get ESRCH here, and would conclude that
            // all is well.
            status->exited = true;
            return true;
        } else {
            // Based on above, fd should not be equal to `ESRCH`, so any negative
            // value here is an error.
            printf("*** pidfd_open failed: %d, %d:%s\n",
                   fd, errno, strerror(errno));
            return false;
        }
    }
    auto close_on_return = android::base::make_scope_guard([fd] { close(fd); });

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    int ready = 0;
    do {
        uint64_t begin_poll_ns = Nanotime();
        ready = poll(&pfd, 1, timeout_ms);
        uint64_t end_poll_ns = Nanotime();
        int wait_duration_ms = (end_poll_ns - begin_poll_ns) / 1000000;
        // If we get a EINTR, we need to restart the poll.
        // If we need to continue waiting, the next timeout will be reduced
        // by the time we spent waiting already. However, we also want to bail
        // if we used up all the `timeout_ms`.
        timeout_ms = std::max(timeout_ms - wait_duration_ms, 0);
    } while (timeout_ms > 0 && (ready == -1 && errno == EINTR));

    if (ready < 0) {
        // Poll failed, return with error.
        return false;
    }
    if (ready == 0) {
        // Calling code detects a timeout via errno.
        errno = ETIMEDOUT;
        return false;
    }
    // ready > 0
    if (pfd.revents & POLLIN) {
        siginfo_t info{};
        // WEXITED: Wait for processes that have exited.
        // WNOHANG: Don't block (though we know it's ready from poll).
        if (waitid(P_PIDFD, fd, &info, WEXITED | WNOHANG) == 0) {
            status->exit_code_or_signal = info.si_status;
            if (info.si_code == CLD_EXITED) {
                status->exited = true;
            } else if (info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED) {
                status->exited = ret_on_signal;
            }
        } else {
            return false;
        }
    }
    return true;
}

}  // unnamed namespace

CommandOptions CommandOptions::DEFAULT = CommandOptions::WithTimeout(10).Build();
CommandOptions CommandOptions::AS_ROOT = CommandOptions::WithTimeout(10).AsRoot().Build();

CommandOptions::CommandOptionsBuilder::CommandOptionsBuilder(int64_t timeout_ms)
    : values(timeout_ms) {
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::Always() {
    values.always_ = true;
    return *this;
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::AsRoot() {
    if (!PropertiesHelper::IsUnroot()) {
        values.account_mode_ = SU_ROOT;
    }
    return *this;
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::AsRootIfAvailable() {
    if (!PropertiesHelper::IsUserBuild()) {
        return AsRoot();
    }
    return *this;
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::DropRoot() {
    values.account_mode_ = DROP_ROOT;
    return *this;
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::RedirectStderr() {
    values.output_mode_ = REDIRECT_TO_STDERR;
    return *this;
}

CommandOptions::CommandOptionsBuilder&
CommandOptions::CommandOptionsBuilder::CloseAllFileDescriptorsOnExec() {
    values.close_all_fds_on_exec_ = true;
    return *this;
}

CommandOptions::CommandOptionsBuilder& CommandOptions::CommandOptionsBuilder::Log(
    const std::string& message) {
    values.logging_message_ = message;
    return *this;
}

CommandOptions CommandOptions::CommandOptionsBuilder::Build() {
    return CommandOptions(values);
}

CommandOptions::CommandOptionsValues::CommandOptionsValues(int64_t timeout_ms)
    : timeout_ms_(timeout_ms),
      always_(false),
      close_all_fds_on_exec_(false),
      account_mode_(DONT_DROP_ROOT),
      output_mode_(NORMAL_OUTPUT),
      logging_message_("") {
}

CommandOptions::CommandOptions(const CommandOptionsValues& values) : values(values) {
}

int64_t CommandOptions::Timeout() const {
    return MSEC_TO_SEC(values.timeout_ms_);
}

int64_t CommandOptions::TimeoutInMs() const {
    return values.timeout_ms_;
}

bool CommandOptions::Always() const {
    return values.always_;
}

bool CommandOptions::ShouldCloseAllFileDescriptorsOnExec() const {
    return values.close_all_fds_on_exec_;
}

PrivilegeMode CommandOptions::PrivilegeMode() const {
    return values.account_mode_;
}

OutputMode CommandOptions::OutputMode() const {
    return values.output_mode_;
}

std::string CommandOptions::LoggingMessage() const {
    return values.logging_message_;
}

CommandOptions::CommandOptionsBuilder CommandOptions::WithTimeout(int64_t timeout_sec) {
    return CommandOptions::CommandOptionsBuilder(SEC_TO_MSEC(timeout_sec));
}

CommandOptions::CommandOptionsBuilder CommandOptions::WithTimeoutInMs(int64_t timeout_ms) {
    return CommandOptions::CommandOptionsBuilder(timeout_ms);
}

std::string PropertiesHelper::build_type_ = "";
int PropertiesHelper::dry_run_ = -1;
int PropertiesHelper::unroot_ = -1;
int PropertiesHelper::parallel_run_ = -1;
int PropertiesHelper::strict_run_ = -1;

bool PropertiesHelper::IsUserBuild() {
    if (build_type_.empty()) {
        build_type_ = android::base::GetProperty("ro.build.type", "user");
    }
    return "user" == build_type_;
}

bool PropertiesHelper::IsDryRun() {
    if (dry_run_ == -1) {
        dry_run_ = android::base::GetBoolProperty("dumpstate.dry_run", false) ? 1 : 0;
    }
    return dry_run_ == 1;
}

bool PropertiesHelper::IsUnroot() {
    if (unroot_ == -1) {
        unroot_ = android::base::GetBoolProperty("dumpstate.unroot", false) ? 1 : 0;
    }
    return unroot_ == 1;
}

bool PropertiesHelper::IsParallelRun() {
    if (parallel_run_ == -1) {
        parallel_run_ = android::base::GetBoolProperty("dumpstate.parallel_run",
                                                       /* default_value = */ true)
                            ? 1
                            : 0;
    }
    return parallel_run_ == 1;
}

bool PropertiesHelper::IsStrictRun() {
    if (strict_run_ == -1) {
        // Defaults to using stricter timeouts.
        strict_run_ = android::base::GetBoolProperty("dumpstate.strict_run", true) ? 1 : 0;
    }
    return strict_run_ == 1;
}

int DumpFileToFd(int out_fd, const std::string& title, const std::string& path) {
    android::base::unique_fd fd(
        TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)));
    if (fd.get() < 0) {
        int err = errno;
        if (title.empty()) {
            dprintf(out_fd, "*** Error dumping %s: %s\n", path.c_str(), strerror(err));
        } else {
            dprintf(out_fd, "*** Error dumping %s (%s): %s\n", path.c_str(), title.c_str(),
                    strerror(err));
        }
        return -1;
    }
    return DumpFileFromFdToFd(title, path, fd.get(), out_fd, PropertiesHelper::IsDryRun());
}

int RunCommandToFd(int fd, const std::string& title, const std::vector<std::string>& full_command,
                   const CommandOptions& options) {
    if (full_command.empty()) {
        MYLOGE("No arguments on RunCommandToFd(%s)\n", title.c_str());
        return -1;
    }

    int size = full_command.size() + 1;  // null terminated
    int starting_index = 0;
    if (options.PrivilegeMode() == SU_ROOT) {
        starting_index = 2;  // "su" "root"
        size += starting_index;
    }

    std::vector<const char*> args;
    args.resize(size);

    std::string command_string;
    if (options.PrivilegeMode() == SU_ROOT) {
        args[0] = kSuPath;
        command_string += kSuPath;
        args[1] = "root";
        command_string += " root ";
    }
    for (size_t i = 0; i < full_command.size(); i++) {
        args[i + starting_index] = full_command[i].data();
        command_string += args[i + starting_index];
        if (i != full_command.size() - 1) {
            command_string += " ";
        }
    }
    args[size - 1] = nullptr;

    const char* command = command_string.c_str();

    if (options.PrivilegeMode() == SU_ROOT && PropertiesHelper::IsUserBuild()) {
        dprintf(fd, "Skipping '%s' on user build.\n", command);
        return 0;
    }

    if (!title.empty()) {
        dprintf(fd, "------ %s (%s) ------\n", title.c_str(), command);
    }

    const std::string& logging_message = options.LoggingMessage();
    if (!logging_message.empty()) {
        MYLOGI(logging_message.c_str(), command_string.c_str());
    }

    bool silent = (options.OutputMode() == REDIRECT_TO_STDERR ||
                   options.ShouldCloseAllFileDescriptorsOnExec());
    bool redirecting_to_fd = STDOUT_FILENO != fd;

    if (PropertiesHelper::IsDryRun() && !options.Always()) {
        if (!title.empty()) {
            dprintf(fd, "\t(skipped on dry run)\n");
        } else if (redirecting_to_fd) {
            // There is no title, but we should still print a dry-run message
            dprintf(fd, "%s: skipped on dry run\n", command_string.c_str());
        }
        return 0;
    }

    const char* path = args[0];

    uint64_t start = Nanotime();
    // `vfork` avoids process table entry copying vs `fork`, which took significant time.
    pid_t pid = vfork();

    /* handle error case */
    if (pid < 0) {
        if (!silent) dprintf(fd, "*** fork: %s\n", strerror(errno));
        MYLOGE("*** fork: %s\n", strerror(errno));
        return pid;
    }

    /* handle child case */
    if (pid == 0) {
        if (options.PrivilegeMode() == DROP_ROOT && !DropRootUser()) {
            if (!silent) {
                dprintf(fd, "*** failed to drop root before running %s: %s\n", command,
                        strerror(errno));
            }
            MYLOGE("*** could not drop root before running %s: %s\n", command, strerror(errno));
            _exit(EXIT_FAILURE);
        }

        if (options.ShouldCloseAllFileDescriptorsOnExec()) {
            int devnull_fd = TEMP_FAILURE_RETRY(open("/dev/null", O_RDONLY));
            TEMP_FAILURE_RETRY(dup2(devnull_fd, STDIN_FILENO));
            close(devnull_fd);
            devnull_fd = TEMP_FAILURE_RETRY(open("/dev/null", O_WRONLY));
            TEMP_FAILURE_RETRY(dup2(devnull_fd, STDOUT_FILENO));
            TEMP_FAILURE_RETRY(dup2(devnull_fd, STDERR_FILENO));
            close(devnull_fd);
            // This is to avoid leaking FDs that, accidentally, have not been
            // marked as O_CLOEXEC. Leaking FDs across exec can cause failures
            // when execing a process that has a SELinux auto_trans rule.
            // Here we assume that the dumpstate process didn't open more than
            // 1000 FDs. In theory we could iterate through /proc/self/fd/, but
            // doing that in a fork-safe way is too complex and not worth it
            // (opendir()/readdir() do heap allocations and take locks).
            for (int i = 0; i < 1000; i++) {
                if (i != STDIN_FILENO && i != STDOUT_FILENO && i != STDERR_FILENO) {
                    close(i);
                }
            }
        } else if (silent) {
            // Redirects stdout to stderr
            TEMP_FAILURE_RETRY(dup2(STDERR_FILENO, STDOUT_FILENO));
        } else if (redirecting_to_fd) {
            // Redirect stdout to fd
            TEMP_FAILURE_RETRY(dup2(fd, STDOUT_FILENO));
            close(fd);
        }

        /* make sure the child dies when dumpstate dies */
        prctl(PR_SET_PDEATHSIG, SIGKILL);

        /* just ignore SIGPIPE, will go down with parent's */
        struct sigaction sigact;
        memset(&sigact, 0, sizeof(sigact));
        sigact.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sigact, nullptr);

        execvp(path, (char**)args.data());
        // execvp's result will be handled after waitpid_with_timeout() below, but
        // if it failed, it's safer to exit dumpstate.
        MYLOGD("execvp on command '%s' failed (error: %s)\n", command, strerror(errno));
        // Must call _exit (instead of exit), otherwise it will corrupt the zip
        // file.
        _exit(EXIT_FAILURE);
    }

    /* handle parent case */
    ExitStatus status{};
    bool ret = waitpid_with_timeout(
            pid, options.TimeoutInMs(), /*ret_on_signal=*/false, &status);

    uint64_t elapsed = Nanotime() - start;
    if (!ret) {
        if (errno == ETIMEDOUT) {
            if (!silent)
                dprintf(fd, "*** command '%s' timed out after %.3fs (killing pid %d)\n", command,
                        static_cast<float>(elapsed) / NANOS_PER_SEC, pid);
            MYLOGE("*** command '%s' timed out after %.3fs (killing pid %d)\n", command,
                   static_cast<float>(elapsed) / NANOS_PER_SEC, pid);
        } else {
            if (!silent)
                dprintf(fd, "*** command '%s': Error after %.4fs (killing pid %d)\n", command,
                        static_cast<float>(elapsed) / NANOS_PER_SEC, pid);
            MYLOGE("command '%s': Error after %.4fs (killing pid %d)\n", command,
                   static_cast<float>(elapsed) / NANOS_PER_SEC, pid);
        }
        // We signal and wait for timeouts, in this case we treat notice of process termination
        // by signal as "expected"
        kill(pid, SIGTERM);
        if (!waitpid_with_timeout(pid, 5001, /*ret_on_signal=*/true, &status)) {
            kill(pid, SIGKILL);
            if (!waitpid_with_timeout(pid, 5002, /*ret_on_signal=*/true, &status)) {
                if (!silent)
                    dprintf(fd, "could not kill command '%s' (pid %d) even with SIGKILL.\n",
                            command, pid);
                MYLOGE("could not kill command '%s' (pid %d) even with SIGKILL.\n", command, pid);
            }
        }
        return -1;
    }

    if (!status.exited) {
        if (!silent)
            dprintf(fd, "*** command '%s' failed: killed by signal %d\n", command,
                    status.exit_code_or_signal);
        MYLOGE("*** command '%s' failed: killed by signal %d\n", command,
               status.exit_code_or_signal);
    } else if (status.exited && status.exit_code_or_signal != 0) {
        int exit_code = status.exit_code_or_signal;
        if (!silent) dprintf(fd, "*** command '%s' failed: exit code %d\n", command, exit_code);
        MYLOGE("*** command '%s' failed: exit code %d\n", command, exit_code);
    }

    return status.exit_code_or_signal;
}

}  // namespace dumpstate
}  // namespace os
}  // namespace android
