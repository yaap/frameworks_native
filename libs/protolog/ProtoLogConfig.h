/*
 * Copyright 2025 The Android Open Source Project
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

#ifndef MAIN_FRAMEWORKS_BASE_PROTOLOGCONFIG_H
#define MAIN_FRAMEWORKS_BASE_PROTOLOGCONFIG_H

#include <cinttypes>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "ProtoLogConfig.h"
#include "ProtoLogTypes.h"

namespace android {
namespace protolog {

struct ProtoLogGroupConfig {
    bool collect_stacktrace = false;
    // To default to never log anything.
    enum ProtoLogLevel log_from = (ProtoLogLevel)(static_cast<uint32_t>(ProtoLogLevel::WTF) + 1);
};

struct StringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
};

struct ProtoLogConfig {
    std::unordered_map<std::string, ProtoLogGroupConfig, StringHash, std::equal_to<>> group_configs;
    TracingMode tracing_mode = TracingMode::DEFAULT;
    ProtoLogLevel default_log_from_level =
            static_cast<ProtoLogLevel>(static_cast<uint32_t>(ProtoLogLevel::WTF) + 1);

    ProtoLogGroupConfig getConfigForGroup(const std::string_view group_name) const;

    bool parse(const void* ds_config, size_t ds_config_size);
};
} // namespace protolog
} // namespace android

#endif // MAIN_FRAMEWORKS_BASE_PROTOLOGCONFIG_H
