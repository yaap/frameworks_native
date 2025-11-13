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
#include "BinderObserver.h"
#include <mutex>

#include <binder/IServiceManager.h>
#include <utils/SystemClock.h>
#include "BinderStatsUtils.h"

namespace android {
constexpr int kSendIntervalSec = 5;
bool BinderObserver::isFlushRequired(int64_t nowSec) {
    int64_t previousFlushTimeSec = mLastFlushTimeSec.load();
    return nowSec - previousFlushTimeSec >= kSendIntervalSec;
}

void BinderObserver::addStatMaybeFlush(const std::shared_ptr<BinderStatsSpscQueue>& queue,
                                       const BinderCallData& stat) {
    // If write fails, then buffer is full.
    int64_t nowSec = stat.endTimeNanos / 1000'000'000;
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
    std::vector<BinderCallData> data = mBinderStatsCollector.consumeData();
    mPusher.pushLocked(data, nowSec);
}
} // namespace android