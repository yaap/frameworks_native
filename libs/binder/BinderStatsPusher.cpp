/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "BinderStatsPusher.h"
#include <android-base/properties.h>
#include <android/os/IStatsBootstrapAtomService.h>
#include <binder/IServiceManager.h>
#include <utils/SystemClock.h>
#include "BinderStatsUtils.h"
#include "JvmUtils.h"

namespace android {
// defined in stats/atoms/framework/framework_extension_atoms.proto
constexpr int32_t kBinderSpamAtomId = 1064;
[[clang::no_destroy]] static const StaticString16 kStatsBootstrapServiceName(u"statsbootstrap");

sp<os::IStatsBootstrapAtomService> BinderStatsPusher::getBootstrapAtomServiceLocked(
        const int64_t nowSec) {
    // When this is removed, the device does not get past the boot animation
    // TODO(b/299356196): This might result in dropped stats for high usage apps like
    // servicemanager.
    if (!mLastServiceCheckSucceeded && (mServiceCheckTimeSec + kCheckServiceTimeoutSec > nowSec)) {
        return nullptr;
    };
    if (!mBootComplete) {
        // TODO(b/299356196): use gSystemBootCompleted instead
        if (android::base::GetIntProperty("sys.boot_completed", 0) == 0) {
            return nullptr;
        }
    }
    // store a boolean to reduce GetProperty calls
    mBootComplete = true;
    auto sm = defaultServiceManager();
    if (!sm) {
        LOG_ALWAYS_FATAL("defaultServiceManager() returned nullptr.");
    }
    auto service = interface_cast<os::IStatsBootstrapAtomService>(
            defaultServiceManager()->checkService(kStatsBootstrapServiceName));
    mServiceCheckTimeSec = nowSec;
    if (service == nullptr) {
        mLastServiceCheckSucceeded = false;
    } else {
        mLastServiceCheckSucceeded = true;
    }
    return service;
}

void BinderStatsPusher::aggregateBinderSpamLocked(const std::vector<BinderCallData>& data,
                                                  const sp<os::IStatsBootstrapAtomService>& service,
                                                  const int64_t nowSec) {
    for (const auto& datum : data) {
        int64_t timeSec = static_cast<int64_t>(datum.startTimeNanos) / 1000'000'000;
        mSpamStatsBuffer[datum][timeSec]++;
    }
    // Ensure that if this is a local binder and this thread isn't attached
    // to the VM then skip pushing. This is required since StatsBootstrap is
    // a Java service and needs a JNI interface to be called from native code.
    // TODO(b/299356196): Ensure that mSpamStatsBuffer doesn't grow indefinitely.
    if (!service || (IInterface::asBinder(service)->localBinder() && getJavaVM() == nullptr)) {
        return;
    }

    for (auto outerIt = mSpamStatsBuffer.begin(); outerIt != mSpamStatsBuffer.end();
         /* no increment */) {
        bool hasSpam = false;
        int32_t secondsWithAtLeast125Calls = 0;
        int32_t secondsWithAtLeast250Calls = 0;
        const BinderCallData& datum = outerIt->first;
        std::unordered_map<int64_t, uint32_t>& innerMap = outerIt->second;
        for (auto innerIt = innerMap.begin(); innerIt != innerMap.end(); /* no increment */) {
            int64_t startTimeSec = innerIt->first;
            uint32_t count = innerIt->second;

            // Check if the delay period has passed.
            if (nowSec - startTimeSec >= kSpamAggregationWindowSec) {
                if (count >= kSpamFirstWatermark) {
                    hasSpam = true;
                    secondsWithAtLeast125Calls++;
                    if (count >= kSpamSecondWatermark) {
                        secondsWithAtLeast250Calls++;
                    }
                }
                innerIt = innerMap.erase(innerIt);
            } else {
                ++innerIt;
            }
        }
        if (hasSpam) {
            auto atom = os::StatsBootstrapAtom();
            atom.atomId = kBinderSpamAtomId;
            std::vector<os::StatsBootstrapAtomValue> values;
            atom.values.push_back(createPrimitiveValue(static_cast<int64_t>(datum.senderUid)));
            atom.values.push_back(createPrimitiveValue(static_cast<int64_t>(getuid()))); // host uid
            atom.values.push_back(createPrimitiveValue(datum.interfaceDescriptor));
            // TODO (b/299356196): get actual method name.
            atom.values.push_back(
                    createPrimitiveValue(std::to_string(datum.transactionCode))); // aidl method
            atom.values.push_back(
                    createPrimitiveValue(static_cast<int32_t>(secondsWithAtLeast125Calls)));
            atom.values.push_back(
                    createPrimitiveValue(static_cast<int32_t>(secondsWithAtLeast250Calls)));
            // TODO(b/414551350): combine multiple binder calls into one.
            service->reportBootstrapAtom(atom);
        }
        // If the inner map is now empty after removing expired entries, remove the outer entry.
        if (innerMap.empty()) {
            outerIt = mSpamStatsBuffer.erase(outerIt);
        } else {
            ++outerIt;
        }
    }
}

void BinderStatsPusher::pushLocked(const std::vector<BinderCallData>& data, const int64_t nowSec) {
    auto service = getBootstrapAtomServiceLocked(nowSec);
    aggregateBinderSpamLocked(data, service, nowSec);
}

} // namespace android
