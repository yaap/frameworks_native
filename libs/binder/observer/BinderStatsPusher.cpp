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
#define LOG_TAG "libbinder.BinderStatsPusher"

#include "BinderStatsPusher.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/os/binder/BinderCallsStats.h>
#include <android/os/binder/BinderSpamStats.h>
#include <android/os/binder/IBinderStatsConsumerService.h>
#include <android_os_binder_flags.h>
#include <binder/Functional.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <utils/SystemClock.h>
#include <algorithm>
#include <charconv>
#include "../BuildFlags.h"
#include "../JvmUtils.h"
#include "BinderStatsUtils.h"
#include "HistogramScale.h"

namespace android {
[[clang::no_destroy]] static const StaticString16 kBinderStatsServiceName(u"binder_stats_consumer");

sp<os::binder::IBinderStatsConsumerService> BinderStatsPusher::getBinderStatsServiceLocked(
        const int64_t nowSec) {
    // When this is removed, the device does not get past the boot animation
    // TODO(b/299356196): This might result in dropped stats for high usage apps like
    // servicemanager.
    if (!mLastServiceCheckSucceeded && (mServiceCheckTimeSec + kCheckServiceTimeoutSec > nowSec)) {
        return nullptr;
    };
    auto sm = defaultServiceManager();
    if (!sm) {
        LOG_ALWAYS_FATAL("defaultServiceManager() returned nullptr.");
    }
    auto service = interface_cast<os::binder::IBinderStatsConsumerService>(
            defaultServiceManager()->checkService(kBinderStatsServiceName));
    mServiceCheckTimeSec = nowSec;
    if (service == nullptr) {
        mLastServiceCheckSucceeded = false;
    } else {
        mLastServiceCheckSucceeded = true;
    }
    return service;
}

#if defined(LIBBINDER_BINDER_OBSERVER_V2)
constexpr int64_t kNanosPerSecond = 1000'000'000;
constexpr bool kBinderStatsLatencyHistogram = android::os::binder::flags::binder_stats_v3();

struct BinderCallDataByEndTimeLess {
    bool operator()(const BinderCallData& a, const BinderCallData& b) const {
        auto aEndTimeSec = a.endTimeNanos / kNanosPerSecond;
        auto bEndTimeSec = b.endTimeNanos / kNanosPerSecond;
        return std::tie(aEndTimeSec, a.interfaceDescriptor, a.transactionCode, a.senderUid) <
                std::tie(bEndTimeSec, b.interfaceDescriptor, b.transactionCode, b.senderUid);
    }
};

void aggregateSingleCallDataLocked(const BinderCallData& datum,
                                   os::binder::SingleSecondBinderStats& stat) {
    stat.callCount++;
    if (datum.hasLatencyData()) {
        stat.durationCount++;
        int64_t durationNanos;
        if (__builtin_sub_overflow(datum.endTimeNanos, datum.startTimeNanos, &durationNanos)) {
            ALOGW("Duration nanos calculation overflow");
            durationNanos = 0;
        }
        int64_t durationMicros = durationNanos / 1000;
        if (__builtin_add_overflow(durationMicros, stat.durationMicrosSum,
                                   &stat.durationMicrosSum)) {
            ALOGW("Duration Micros Sum calculation overflow");
            stat.durationMicrosSum = std::numeric_limits<int32_t>::max();
        }
        int64_t square;
        if (__builtin_mul_overflow(durationMicros, durationMicros, &square)) {
            ALOGW("Duration Micros calculation overflow");
            stat.durationMicrosSquaredSum = std::numeric_limits<int64_t>::max();
        } else {
            if (__builtin_add_overflow(stat.durationMicrosSquaredSum, square,
                                       &stat.durationMicrosSquaredSum)) {
                ALOGW("Duration Micros Squared Sum calculation overflow");
                stat.durationMicrosSquaredSum = std::numeric_limits<int64_t>::max();
            }
        }
        if (kBinderStatsLatencyHistogram) {
            stat.durationBinIndices.push_back(HistogramScale::getBinIndex(durationNanos));
        }
    }
    if (datum.cpuTimeNanos > 0) {
        stat.cpuTimeCount++;
        int64_t cpuTimeMicros = (datum.cpuTimeNanos) / 1000;

        if (__builtin_add_overflow(cpuTimeMicros, stat.cpuTimeMicrosSum, &stat.cpuTimeMicrosSum)) {
            stat.cpuTimeMicrosSum = std::numeric_limits<int32_t>::max();
            ALOGW("CPU Time Micros Sum calculation overflow");
        }

        int64_t square;
        if (__builtin_mul_overflow(cpuTimeMicros, cpuTimeMicros, &square)) {
            ALOGW("CPU Time Micros Squared calculation overflow");
            stat.cpuTimeMicrosSquaredSum = std::numeric_limits<uint64_t>::max();
        } else {
            if (__builtin_add_overflow(stat.cpuTimeMicrosSquaredSum, square,
                                       &stat.cpuTimeMicrosSquaredSum)) {
                ALOGW("CPU Time Micros Squared Sum calculation overflow");
                stat.cpuTimeMicrosSquaredSum = std::numeric_limits<uint64_t>::max();
            }
        }
    }
}

void BinderStatsPusher::sortAndAggregateStatsLocked(const int64_t nowSec,
                                                    std::vector<BinderCallData>& data) {
    if (data.empty()) {
        return;
    }
    sp<os::binder::IBinderStatsConsumerService> service = getBinderStatsServiceLocked(nowSec);

    // Ensure that if this is a local binder and this thread isn't attached
    // to the VM then skip pushing. This is required since StatsBootstrap is
    // a Java service and needs a JNI interface to be called from native code.
    bool isProcessSystemServer = false;
    if (service != nullptr) {
        isProcessSystemServer = IInterface::asBinder(service)->localBinder() != nullptr;
    }
    if (isProcessSystemServer) {
        if (!isThreadAttachedToJVM()) {
            // TODO(b/458340205): Attach thread to JVM instead of setting service to nullptr.
            // Setting it to nullptr will drop stats.
            service = nullptr;
        }
    }

    if (service == nullptr) {
        return;
    }

    int64_t callingIdentity;
    if (isProcessSystemServer) {
        callingIdentity = IPCThreadState::self()->clearCallingIdentity();
    }

    auto guard = binder::impl::make_scope_guard([&] {
        if (isProcessSystemServer) {
            IPCThreadState::self()->restoreCallingIdentity(callingIdentity);
        }
    });

    auto reportStatsAndClearBuffer = [&]() {
        if (service != nullptr) {
            auto status = service->reportSecondGranularityStats(mSingleSecondStats);
            if (!status.isOk()) {
                ALOGW("reportSecondGranularityStats failed: %s", status.toString8().c_str());
            }
            mSingleSecondStats.clear();
        }
    };

    // Sort data to group calls by interface, transaction code, and sender UID within the same
    // second. This allows for efficient chunking and aggregation of related binder calls.
    std::sort(data.begin(), data.end(), BinderCallDataByEndTimeLess());

    auto chunkStart = data.begin();
    os::binder::SingleSecondBinderStats chunkStat{};

    for (auto it = data.begin(); it != data.end(); ++it) {
        aggregateSingleCallDataLocked(*it, chunkStat);

        auto nextIt = it + 1;
        bool endOfChunk = (nextIt == data.end());
        // A chunk is a set of binderCallData which have the same endTimeSecond, senderUid,
        // interfaceDescriptor and transactionCode. After sorting they will be contiguous.
        if (!endOfChunk) {
            bool isSameChunk = chunkStart->endTimeNanos / kNanosPerSecond ==
                            nextIt->endTimeNanos / kNanosPerSecond &&
                    chunkStart->senderUid == nextIt->senderUid &&
                    chunkStart->transactionCode == nextIt->transactionCode &&
                    chunkStart->interfaceDescriptor == nextIt->interfaceDescriptor;
            endOfChunk = !isSameChunk;
        }

        if (endOfChunk) {
            if (mSingleSecondStats.size() >= kMaxStatsCount) {
                reportStatsAndClearBuffer();
            }

            if (chunkStat.callCount > 0) {
                chunkStat.interfaceDescriptor = chunkStart->interfaceDescriptor;
                chunkStat.aidlMethod = chunkStart->aidlMethodName;
                chunkStat.clientUid = chunkStart->senderUid;
                mSingleSecondStats.push_back(std::move(chunkStat));
            }

            chunkStart = nextIt;
            if (chunkStart != data.end()) {
                chunkStat = os::binder::SingleSecondBinderStats{};
            }
        }
    }

    if (!mSingleSecondStats.empty()) {
        reportStatsAndClearBuffer();
    }
}

// TODO(b/458340205): Remove the function.
void BinderStatsPusher::pushLocked(const int64_t /*nowSec*/) {
    // With LIBBINDER_BINDER_OBSERVER_V2 true, aggregation and reporting are handled by
    // BinderCallsVectorAggregation::addCallStatsLocked, so this function is empty.
}

#else  // !defined(LIBBINDER_BINDER_OBSERVER_V2)

void BinderStatsPusher::appendCallStatsToReportLocked(const BinderCallData& chunk,
                                                      const CallAggregation& agg) {
    if (agg.callsWithLatency > 0 || agg.cpuTimeCount > 0) {
        mCallStats.emplace_back(os::binder::BinderCallsStats());
        auto& callsStats = mCallStats.back();
        callsStats.clientUid = static_cast<int32_t>(chunk.senderUid);
        callsStats.interfaceDescriptor = chunk.interfaceDescriptor;
        callsStats.aidlMethod = chunk.aidlMethodName;
        callsStats.callCount = static_cast<int32_t>(agg.callsWithLatency);
        callsStats.durationSumMicros = agg.durationSumMicros;
        callsStats.secondsWithAtLeast10Calls = agg.secondsWithAtLeast10Calls;
        callsStats.secondsWithAtLeast50Calls = agg.secondsWithAtLeast50Calls;
        callsStats.cpuTimeCount = static_cast<int32_t>(agg.cpuTimeCount);
        callsStats.cpuTimeSumMicros = agg.cpuTimeSumMicros;
        callsStats.cpuTimeSumSquaredMicros = agg.cpuTimeSumSquaredMicros;
    }
}

__attribute__((no_sanitize("signed-integer-overflow"))) void
BinderStatsPusher::aggregateStatsLocked(const int64_t nowSec) {
    sp<os::binder::IBinderStatsConsumerService> service = getBinderStatsServiceLocked(nowSec);

    // Ensure that if this is a local binder and this thread isn't attached
    // to the VM then skip pushing. This is required since StatsBootstrap is
    // a Java service and needs a JNI interface to be called from native code.
    bool isProcessSystemServer = false;
    if (service != nullptr) {
        isProcessSystemServer = IInterface::asBinder(service)->localBinder() != nullptr;
    }
    if (isProcessSystemServer) {
        if (!isThreadAttachedToJVM()) {
            // TODO(b/458340205): Attach thread to JVM instead of setting service to nullptr.
            // Setting it to nullptr will drop stats.
            service = nullptr;
        }
    }

    if (service == nullptr) {
        return;
    }

    int64_t callingIdentity;
    if (isProcessSystemServer) {
        callingIdentity = IPCThreadState::self()->clearCallingIdentity();
    }

    auto guard = binder::impl::make_scope_guard([&] {
        if (isProcessSystemServer) {
            IPCThreadState::self()->restoreCallingIdentity(callingIdentity);
        }
    });

    auto& callsBuffer = mCallsAggregation.getBufferLocked();

    for (auto outerIt = callsBuffer.begin(); outerIt != callsBuffer.end();
         /* no increment */) {
        int32_t secondsWithAtLeast125Calls = 0;
        int32_t secondsWithAtLeast250Calls = 0;
        uint32_t peakCallCountPerSecond = 0;

        CallAggregation agg;

        for (auto innerIt = outerIt->second.begin(); innerIt != outerIt->second.end();
             /* no increment */) {
            // Check if the buffer period has passed.
            int64_t startTimeSec = innerIt->first;
            if (nowSec - startTimeSec >= kAggregationWindowSec) {
                uint32_t totalCalls = innerIt->second.totalCalls;
                if (totalCalls > kSpamFirstWatermark) {
                    secondsWithAtLeast125Calls++;
                    if (totalCalls > kSpamSecondWatermark) {
                        secondsWithAtLeast250Calls++;
                    }
                    peakCallCountPerSecond = std::max(peakCallCountPerSecond, totalCalls);
                }
                if (innerIt->second.callsWithLatency > 0) {
                    agg.callsWithLatency += innerIt->second.callsWithLatency;
                    agg.durationSumMicros += innerIt->second.durationSumMicros;
                    agg.callDurationSumSquaredMicros +=
                            innerIt->second.callDurationSumSquaredMicros;
                    if (innerIt->second.callsWithLatency >= kLatencyCountFirstWatermark) {
                        agg.secondsWithAtLeast10Calls++;
                        if (innerIt->second.callsWithLatency >= kLatencyCountSecondWatermark) {
                            agg.secondsWithAtLeast50Calls++;
                        }
                    }
                }
                if (innerIt->second.cpuTimeCount > 0) {
                    agg.cpuTimeCount += innerIt->second.cpuTimeCount;
                    agg.cpuTimeSumMicros += innerIt->second.cpuTimeSumMicros;
                    agg.cpuTimeSumSquaredMicros += innerIt->second.cpuTimeSumSquaredMicros;
                }
                // Erase the datum from the buffer so we don't aggregate it again
                innerIt = outerIt->second.erase(innerIt);
            } else {
                ++innerIt;
            }
        }
        if (mCallStats.size() >= kMaxStatsCount) {
            service->reportCallStats(mCallStats);
            mCallStats.clear();
        }
        appendCallStatsToReportLocked(outerIt->first, agg);
        agg.reset();

        if (mSpamStats.size() >= kMaxStatsCount) {
            service->reportSpamStats(mSpamStats);
            mSpamStats.clear();
        }
        if (secondsWithAtLeast125Calls > 0) {
            auto datum = outerIt->first;
            mSpamStats.emplace_back(os::binder::BinderSpamStats());
            auto& spamStats = mSpamStats.back();
            spamStats.clientUid = static_cast<int32_t>(datum.senderUid);
            spamStats.interfaceDescriptor = datum.interfaceDescriptor;
            spamStats.aidlMethod = datum.aidlMethodName;
            spamStats.secondsWithAtLeast125Calls = secondsWithAtLeast125Calls;
            spamStats.secondsWithAtLeast250Calls = secondsWithAtLeast250Calls;
            spamStats.peakCallCountPerSecond = peakCallCountPerSecond;
        }

        if (outerIt->second.empty()) {
            outerIt = callsBuffer.erase(outerIt);
        } else {
            ++outerIt;
        }
    }
    if (!mCallStats.empty()) {
        service->reportCallStats(mCallStats);
        mCallStats.clear();
    }
    if (!mSpamStats.empty()) {
        service->reportSpamStats(mSpamStats);
        mSpamStats.clear();
    }
}

void BinderStatsPusher::pushLocked(const int64_t nowSec) {
    aggregateStatsLocked(nowSec);
}
#endif // defined(LIBBINDER_BINDER_OBSERVER_V2)

BinderStatsPusher::AddCallDataFunctor BinderStatsPusher::getAddCallDataToBufferLockedFunction() {
    return AddCallDataFunctor(this);
}

} // namespace android
