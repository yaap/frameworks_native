/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "MemtrackProxy.h"

#include <string.h>

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <private/android_filesystem_config.h>

namespace aidl {
namespace android {
namespace hardware {
namespace memtrack {

namespace {

// LINT.IfChange
constexpr char kMemtrackDefaultMsg[] = "memtrack default implementation";
// LINT.ThenChange(/hardware/interfaces/memtrack/aidl/default/Memtrack.cpp)

static inline bool isMemtrackDefaultImpl(const ndk::ScopedAStatus& status) {
    return !status.isOk()
            && status.getExceptionCode() == EX_UNSUPPORTED_OPERATION
            && strcmp(status.getMessage(), kMemtrackDefaultMsg) == 0;
}

}  // namespace

std::shared_ptr<V1_aidl::IMemtrack> MemtrackProxy::MemtrackAidlInstance() {
    const auto instance = std::string() + V1_aidl::IMemtrack::descriptor + "/default";
    bool declared = AServiceManager_isDeclared(instance.c_str());
    if (!declared) {
        return nullptr;
    }
    ndk::SpAIBinder memtrack_binder =
            ndk::SpAIBinder(AServiceManager_waitForService(instance.c_str()));
    return V1_aidl::IMemtrack::fromBinder(memtrack_binder);
}

bool MemtrackProxy::CheckUid(uid_t calling_uid) {
    // Allow AID_SHELL for adb shell dumpsys meminfo
    return calling_uid == AID_SYSTEM || calling_uid == AID_ROOT || calling_uid == AID_SHELL;
}

bool MemtrackProxy::CheckPid(pid_t calling_pid, pid_t request_pid) {
    return calling_pid == request_pid;
}

MemtrackProxy::MemtrackProxy() {
    memtrack_aidl_instance_ = MemtrackProxy::MemtrackAidlInstance();
    is_get_memory_supported_ = true;
}

ndk::ScopedAStatus MemtrackProxy::getMemory(int pid, MemtrackType type,
                                            std::vector<MemtrackRecord>* _aidl_return) {
    if (pid < 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    if (!MemtrackProxy::CheckPid(AIBinder_getCallingPid(), pid) &&
        !MemtrackProxy::CheckUid(AIBinder_getCallingUid())) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                EX_SECURITY,
                "Only AID_ROOT, AID_SYSTEM and AID_SHELL can request getMemory() for PIDs other "
                "than the calling PID");
    }

    if (type != MemtrackType::OTHER && type != MemtrackType::GL && type != MemtrackType::GRAPHICS &&
        type != MemtrackType::MULTIMEDIA && type != MemtrackType::CAMERA) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    _aidl_return->clear();

    if (memtrack_aidl_instance_) {
        if (!is_get_memory_supported_) {
            return ndk::ScopedAStatus::ok();
        }

        ndk::ScopedAStatus aidl_status =
            memtrack_aidl_instance_->getMemory(pid, type, _aidl_return);

        if (isMemtrackDefaultImpl(aidl_status)) {
            // The default memory track HAL doesn't support the |getMemory| method.
            // Therefore we prevent making additional binder calls to the HAL once
            // we know the operation isn't supported.
            is_get_memory_supported_ = false;
            return ndk::ScopedAStatus::ok();
        }

        return aidl_status;
    }

    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_NULL_POINTER,
                                                            "Memtrack HAL service not available");
}

ndk::ScopedAStatus MemtrackProxy::getGpuDeviceInfo(std::vector<DeviceInfo>* _aidl_return) {
    if (!MemtrackProxy::CheckUid(AIBinder_getCallingUid())) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_SECURITY,
                "Only AID_ROOT, AID_SYSTEM and AID_SHELL can request getGpuDeviceInfo()");
    }

    _aidl_return->clear();

    if (memtrack_aidl_instance_ ||
        (memtrack_aidl_instance_ = MemtrackProxy::MemtrackAidlInstance())) {
        return memtrack_aidl_instance_->getGpuDeviceInfo(_aidl_return);
    }

    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_NULL_POINTER,
                                                            "Memtrack HAL service not available");
}

} // namespace memtrack
} // namespace hardware
} // namespace android
} // namespace aidl
