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
#define LOG_TAG "libbinder.BinderObserver"

#include "BinderObserver.h"
#include <mutex>

#include <binder/IServiceManager.h>
#include <binder/internal/JavaBBinderBase.h>
#include <utils/SystemClock.h>
#include "../BuildFlags.h"
#include "BinderStatsUtils.h"

namespace android {
#if !defined(LIBBINDER_BINDER_OBSERVER_V2)
constexpr int kSendIntervalSec = 5;
#endif

constexpr int64_t kNanoSecondsPerSec = 1000'000'000LL;

BinderObserver::BinderObserver(std::unique_ptr<BinderObserverConfig> config)
      : mConfig(std::move(config)) {}

BinderObserver::CallInfo BinderObserver::onBeginTransaction(BBinder* binder, uint32_t code,
                                                            uid_t callingUid) {
    LOG_ALWAYS_FATAL_IF(binder == nullptr, "Null binder sent to binder observer.");
    if (!mConfig->isEnabled()) {
        return {};
    }

    // Sharding based on the pointer would be faster but would also increase the cardinality of
    // the different AIDLs that we report. Ideally, we want something stable within the
    // current boot session, so we use the interface descriptor.
    const String16& interfaceDescriptor = binder->getInterfaceDescriptor();
    BinderObserverConfig::TrackingInfo trackingInfo =
            mConfig->getTrackingInfo(interfaceDescriptor, code);
    // For V1, the aggregation strategy requires a start time for every tracked transaction. For
    // V2, we only need the start time if we are tracking latency for a transaction.
    bool trackStartTime =
            kBinderObserverV2Enabled ? trackingInfo.trackLatency : trackingInfo.isTracked();

    String16 aidlMethodName;
    if (trackingInfo.isTracked()) {
        if (binder->checkSubclass(android::internal::JavaBBinderBase::getExtSubclassID())) {
            static_cast<internal::JavaBBinderBase*>(binder)
                    ->getFunctionName(code, [&aidlMethodName](const char* name) {
                        if (name) {
                            aidlMethodName = String16(name);
                        }
                    });
        } else {
            aidlMethodName = String16(binder->getFunctionName(code).c_str());
        }
    }

    return {
            .startTimeNanos = trackStartTime ? uptimeNanos() : 0,
            .cpuUsageStartTimeNanos = trackingInfo.trackCpu ? mConfig->getCpuTimeNanos() : 0,
            .interfaceDescriptor = interfaceDescriptor,
            // TODO(b/299356196): Reduce std::string and String16 allocations.
            .aidlMethodName = aidlMethodName,
            .code = code,
            .callingUid = callingUid,
            .trackingInfo = trackingInfo,
    };
}

void BinderObserver::onEndTransaction(std::shared_ptr<BinderStatsSpscQueue>& queue,
                                      const CallInfo& callInfo) {
    if (!mConfig->isEnabled() || !callInfo.trackingInfo.isTracked()) {
        return;
    }
    if (queue == nullptr) {
        queue = std::make_shared<BinderStatsSpscQueue>();
        mBinderStatsCollector.registerQueue(queue);
    }
    // For V2, the aggregation strategy requires an end time for every transaction. For V1, we
    // only record an end time if we are tracking latency.
    bool shouldRecordEndTime = kBinderObserverV2Enabled || callInfo.trackingInfo.trackLatency;
    int64_t endTimeNanos = shouldRecordEndTime ? uptimeNanos() : 0;
    BinderCallData observerData = {
            .startTimeNanos = callInfo.startTimeNanos,
            .endTimeNanos = endTimeNanos,
            .cpuTimeNanos = callInfo.trackingInfo.trackCpu && callInfo.cpuUsageStartTimeNanos != 0
                    ? mConfig->getCpuTimeNanos() - callInfo.cpuUsageStartTimeNanos
                    : 0,
            .interfaceDescriptor = callInfo.interfaceDescriptor,
            .aidlMethodName = callInfo.aidlMethodName,
            .transactionCode = callInfo.code,
            .senderUid = callInfo.callingUid,
    };
    addStatMaybeFlush(queue, observerData);
}

void BinderObserver::deregisterThread(std::shared_ptr<BinderStatsSpscQueue>& queue) {
    if (queue != nullptr) {
        mBinderStatsCollector.deregisterQueue(queue);
        queue = nullptr;
    }
}

bool BinderObserver::isFlushRequired(int64_t nowSec) {
    int64_t previousFlushTimeSec = mLastFlushTimeSec.load();
#if defined(LIBBINDER_BINDER_OBSERVER_V2)
    return previousFlushTimeSec < nowSec;
#else
    return nowSec - previousFlushTimeSec >= kSendIntervalSec;
#endif
}

void BinderObserver::addStatMaybeFlush(const std::shared_ptr<BinderStatsSpscQueue>& queue,
                                       const BinderCallData& stat) {
    // If write fails, then buffer is full.
    int64_t nowSec = stat.endTimeNanos / kNanoSecondsPerSec;
    if (!queue->push(stat)) {
        flushStats(nowSec);
        // If write fails again, we drop the stat.
        // TODO(b/299356196): Track dropped stats separately.
        queue->push(stat);
        return;
    }
    if (isFlushRequired(nowSec)) {
        flushStats(nowSec);
    }
}

void BinderObserver::flushStats(int64_t nowSec) {
    std::unique_lock<std::mutex> lock(mFlushLock, std::defer_lock);
    // skip flushing if flushing is already in progress
    if (!lock.try_lock()) {
        return;
    }
    // flush
    mLastFlushTimeSec = nowSec;
    mBinderStatsCollector.consumeData(mPusher.getAddCallDataToBufferLockedFunction());
    mPusher.pushLocked(nowSec);
}
} // namespace android
