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
#define LOG_TAG "libbinder.BinderObserverConfig"

#include <cutils/android_filesystem_config.h> // for AID_SYSTEM
#include <stdlib.h>                           // for getprogname
#include <time.h>                             // for clock_gettime
#include <unistd.h>                           // for getuid()

#include "../BuildFlags.h"
#include "../OS.h"
#include "BinderObserverConfig.h"

namespace android {
#ifdef BINDER_OBSERVER_DROIDFOOD_CONFIG
constexpr bool kUseDroidfoodConfig = true;
#else
constexpr bool kUseDroidfoodConfig = false;
#endif // BINDER_OBSERVER_DROIDFOOD_CONFIG

// __BIONIC__ has getprogname() but __GLIBC__ and others have program_invocation_short_name
#if !defined(__BIONIC__)
const char* getprogname() {
    return program_invocation_short_name;
}
#endif

bool BinderObserverConfig::Environment::fileExists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

std::string BinderObserverConfig::Environment::readFileLine(const std::string& path) {
    std::string content;
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, content);
    }
    return content;
}

uid_t BinderObserverConfig::Environment::getUid() {
    return getuid();
}

std::string BinderObserverConfig::Environment::getProcessName() {
    return getprogname();
}

BinderObserverConfig::ShardingConfig BinderObserverConfig::Environment::getSystemServerSharding(
        bool debugMonitorAll) {
    if (debugMonitorAll) {
        return ShardingConfig{.processMod = 1,
                              .spamMod = 1,
                              .callMod = 1,
                              .cpuSamplingMod = kBinderObserverV2Enabled ? 10 : 0};
    }

    return kUseDroidfoodConfig
            ? ShardingConfig{.processMod = 1,
                             .spamMod = 10,
                             .callMod = 10,
                             .cpuSamplingMod = kBinderObserverV2Enabled ? 10 : 0}
            : ShardingConfig{.processMod = 10,
                             .spamMod = 100,
                             .callMod = 100,
                             .cpuSamplingMod = kBinderObserverV2Enabled ? 100 : 0};
}

BinderObserverConfig::ShardingConfig
BinderObserverConfig::Environment::getOtherProcessesSharding() {
    return kUseDroidfoodConfig
            ? ShardingConfig{.processMod = 5,
                             .spamMod = 2,
                             .callMod = 2,
                             .cpuSamplingMod = kBinderObserverV2Enabled ? 10 : 0}
            : ShardingConfig{.processMod = 50,
                             .spamMod = 20,
                             .callMod = 20,
                             .cpuSamplingMod = kBinderObserverV2Enabled ? 100 : 0};
}

int64_t BinderObserverConfig::Environment::getCpuTimeNanos() {
    timespec now;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == -1) {
        return 0;
    }
    return now.tv_sec * 1000'000'000LL + now.tv_nsec;
}

std::tuple<size_t, size_t, size_t> BinderObserverConfig::getBootStableTokens(
        Environment& environment) {
    std::string bootToken = environment.readFileLine(kBootIdPath);

    // Boot id looks like this: "16e12b27-2a84-4355-84cd-948348d6c998"
    LOG_ALWAYS_FATAL_IF(bootToken.size() != kBootIdSize, "Bad boot_id: '%s'", bootToken.c_str());

    // Use the first half for process sharding and the second half for AIDL sharding.
    size_t token1 = environment.hashString8(bootToken.substr(0, kBootIdSize / 2));
    size_t token2 = environment.hashString8(bootToken.substr(kBootIdSize / 2));
    // Use the middle substring for cpu Tracking offset.
    size_t token3 = environment.hashString8(bootToken.substr(kBootIdSize / 4, 3 * kBootIdSize / 4));

    return std::tie(token1, token2, token3);
}

std::unique_ptr<BinderObserverConfig> BinderObserverConfig::createConfig(
        std::unique_ptr<BinderObserverConfig::Environment>&& environment) {
    // Determine if we are in the system_server process and get the appropriate sharding config.
    uid_t uid = environment->getUid();
    std::string processName = environment->getProcessName();

    ShardingConfig sharding;
    if (uid == AID_SYSTEM && processName == "system_server") {
        // Limit full monitoring to the system server process to prevent excessive overhead.
        bool debugMonitorAll = environment->fileExists(kFullBinderSpamDetectionConfigPath);
        sharding = environment->getSystemServerSharding(debugMonitorAll);
    } else {
        sharding = environment->getOtherProcessesSharding();
    }

    if (sharding.processMod == 0) {
        // Sharding of 0 means disabled. No need to read further configuration.
        return std::unique_ptr<BinderObserverConfig>(
                new BinderObserverConfig(std::move(environment), false, sharding, 0, 0));
    }

    // Note: we want sharding to be stable for each session. Otherwise, for short-lived
    // proecsses, we will track different AIDLs each time the process runs, which will
    // increase the cardinality of the metrics we track.

    // We also want process sharding and AIDL sharding to be independent, as otherwise
    // certain process+AIDL combinations may be reported more frequently than others.
    // That's why we use two independent tokens.
    auto [processOffset, aidlOffset, cpuTrackingOffset] = getBootStableTokens(*environment);

    // We use simple modulo arithmetc to keep sharding easier to understand and test. Everything
    // is mod-ed before addition too to ensure we don't overflow.
    size_t modulo = sharding.processMod;

    size_t token = processOffset % modulo;
    token += uid % modulo;
    token += environment->hashString8(processName) % modulo;
    bool enabled = token % modulo == 0;

    if (sharding.cpuSamplingMod > 0) {
        cpuTrackingOffset %= sharding.cpuSamplingMod;
    }

    return std::unique_ptr<BinderObserverConfig>(
            new BinderObserverConfig(std::move(environment), enabled, sharding, aidlOffset,
                                     cpuTrackingOffset));
}

// mLatencySequenceNumber can safely overflow and wrap, as it's only used for modulo operations.
__attribute__((no_sanitize("unsigned-integer-overflow"))) size_t
BinderObserverConfig::fetchAddOneLatencySequenceNumber() {
    return mLatencySequenceNumber.fetch_add(1, std::memory_order_relaxed);
}

BinderObserverConfig::TrackingInfo BinderObserverConfig::getTrackingInfo(
        const std::u16string_view& interfaceDescriptor, uint32_t txnCode) {
    if (!mEnabled) {
        return {.trackSpam = false, .trackLatency = false, .trackCpu = false};
    }
    // If present callMod must be a multiple of spamMod, so use it as a modulo.
    size_t modulo = mSharding.callMod != 0 ? mSharding.callMod : mSharding.spamMod;
    // As above, mod before addition too to ensure we don't overflow.
    size_t token = mAidlOffset % modulo;
    token += txnCode % modulo;
    token += mEnvironment->hashString16(interfaceDescriptor) % modulo;

    bool trackSpam = mSharding.spamMod > 0 && token % mSharding.spamMod == 0;
    bool trackLatency = mSharding.callMod > 0 && token % mSharding.callMod == 0;
    bool trackCpu = false;
    if (trackLatency && mSharding.cpuSamplingMod > 0) {
        size_t latencySequenceNumber = fetchAddOneLatencySequenceNumber();
        trackCpu = latencySequenceNumber % mSharding.cpuSamplingMod == 0;
    }
    return {
            .trackSpam = trackSpam,
            .trackLatency = trackLatency,
            .trackCpu = trackCpu,
    };
}

} // namespace android
