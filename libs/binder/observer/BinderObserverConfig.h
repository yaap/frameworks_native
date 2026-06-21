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
#pragma once

#include <log/log.h>

#include <atomic>
#include <fstream>
#include <limits>
#include <string>

namespace android {

class BinderObserverConfigTest;

/**
 * Manages the configuration of the binder observer.
 *
 * Applies sharding to determine whether the observer should be enabled for the current process
 * and which AIDL methods should be tracked.
 *
 * The configuration does not change within the lifecycle of the process.
 */
class BinderObserverConfig {
public:
    struct TrackingInfo {
        bool trackSpam;
        bool trackLatency;
        // trackCpu requires trackLatency to be true.
        bool trackCpu;

        bool operator==(const TrackingInfo&) const = default;
        bool isTracked() const { return trackSpam || trackLatency; }
    };

    static std::unique_ptr<BinderObserverConfig> createConfig() {
        return createConfig(std::make_unique<Environment>());
    }

    // Returns whether binder stats are enabled for the current process. Should not change
    // within the lifecycle of a process.
    bool isEnabled() { return mEnabled; }

    int64_t getCpuTimeNanos() { return mEnvironment->getCpuTimeNanos(); }

    TrackingInfo getTrackingInfo(const std::u16string_view& interfaceDescriptor, uint32_t txnCode);

private:
    friend class BinderObserverConfigTest;
    friend class BinderObserver_CpuTimeNanosCorrect_Test;
    friend class CpuTimeNanosCorrectTestEnvironment;

    struct ShardingConfig {
        size_t processMod;
        size_t spamMod;
        size_t callMod;
        // The sampling rate for CPU usage tracking. CPU usage is tracked for 1 in every
        // cpuSamplingMod calls for which latency is also being tracked.
        size_t cpuSamplingMod;
        bool operator==(const ShardingConfig&) const = default;
    };

    // Contains a 128-bit random number stable for the current session
    // Looks like this: "16e12b27-2a84-4355-84cd-948348d6c998"
    static constexpr const char* kBootIdPath = "/proc/sys/kernel/random/boot_id";

    static constexpr const char* kFullBinderSpamDetectionConfigPath =
            "/data/system/anomaly_service/anomaly_detection.full_binder_spam_detection";

    // The expected length of /proc/sys/kernel/random/boot_id
    static constexpr size_t kBootIdSize = 36;

    // Interacting with the environment abstracted out for testability.
    class Environment {
    public:
        virtual ~Environment() = default;
        virtual bool fileExists(const std::string& path);
        virtual std::string readFileLine(const std::string& path);
        virtual uid_t getUid();
        virtual std::string getProcessName();
        virtual ShardingConfig getSystemServerSharding(bool monitorAll);
        virtual ShardingConfig getOtherProcessesSharding();
        virtual int64_t getCpuTimeNanos();

        virtual size_t hashString8(const std::string& content) {
            return std::hash<std::string>{}(content);
        }

        virtual size_t hashString16(const std::u16string_view& content) {
            return std::hash<std::u16string_view>{}(content);
        }
    };

    static std::unique_ptr<BinderObserverConfig> createConfig(
            std::unique_ptr<BinderObserverConfig::Environment>&& environment);

    BinderObserverConfig(std::unique_ptr<BinderObserverConfig::Environment>&& environment,
                         bool enabled, BinderObserverConfig::ShardingConfig sharding,
                         size_t aidlOffset, size_t cpuSamplingOffset)
          : mEnvironment(std::move(environment)),
            mEnabled(enabled),
            mSharding(sharding),
            mAidlOffset(aidlOffset),
            mLatencySequenceNumber(cpuSamplingOffset) {
        if (mEnabled) {
            LOG_ALWAYS_FATAL_IF(mSharding.spamMod == 0, "Enabled but nothing to monitor.");

            // Call sharding must always be a multiple of spam sharding (or 0, which is also OK)
            LOG_ALWAYS_FATAL_IF(mSharding.callMod % mSharding.spamMod != 0,
                                "Call reporting must be a subset of spam reporting.");
        }
    }

    static std::tuple<size_t, size_t, size_t> getBootStableTokens(Environment& environment);

    // Atomically Adds 1 to mLatencySequenceNumber and returns.
    // Split out to allow no_sanitize(unsigned-integer-overflow).
    size_t fetchAddOneLatencySequenceNumber();

    const std::unique_ptr<Environment> mEnvironment;
    const bool mEnabled;
    const ShardingConfig mSharding;
    // A boot-stable random value used for sharding AIDL method tracking.
    const size_t mAidlOffset;
    // A counter for latency-tracked calls, used to sample CPU usage tracking. Initialized with a
    // "boot + process name"-stable random value to ensure different processes sample different
    // calls.
    std::atomic<size_t> mLatencySequenceNumber;
};
} // namespace android
