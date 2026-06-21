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

#include "ProtoLogConfig.h"

#include <config/android/protolog_config.pzc.h>
#include <log/log.h>
#include <perfetto/public/abi/pb_decoder_abi.h>
#include <perfetto/public/protos/config/data_source_config.pzc.h>
#include "ProtoLogTypes.h"

namespace android {
namespace protolog {

ProtoLogLevel ParseProtoLogLevel(uint64_t logLevel) {
    switch (logLevel) {
        case perfetto_protos_PROTOLOG_LEVEL_UNDEFINED:
            return ProtoLogLevel::UNDEFINED;
        case perfetto_protos_PROTOLOG_LEVEL_VERBOSE:
            return ProtoLogLevel::VERBOSE;
        case perfetto_protos_PROTOLOG_LEVEL_DEBUG:
            return ProtoLogLevel::DEBUG;
        case perfetto_protos_PROTOLOG_LEVEL_INFO:
            return ProtoLogLevel::INFO;
        case perfetto_protos_PROTOLOG_LEVEL_WARN:
            return ProtoLogLevel::WARN;
        case perfetto_protos_PROTOLOG_LEVEL_ERROR:
            return ProtoLogLevel::ERROR;
        case perfetto_protos_PROTOLOG_LEVEL_WTF:
            return ProtoLogLevel::WTF;
        default:
            return ProtoLogLevel::UNDEFINED;
    }
}

bool DeserializeProtoLogGroup(struct PerfettoPbDecoder* decoder, std::string* out_name,
                              ProtoLogGroupConfig* out_group) {
    struct PerfettoPbDecoderField field;
    while ((field = PerfettoPbDecoderParseField(decoder)).status == PERFETTO_PB_DECODER_OK) {
        switch (field.id) {
            case perfetto_protos_ProtoLogGroup_group_name_field_number:
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_DELIMITED) return false;
                out_name->assign((const char*)field.value.delimited.start,
                                 field.value.delimited.len);
                break;
            case perfetto_protos_ProtoLogGroup_log_from_field_number:
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_VARINT) return false;
                out_group->log_from = ParseProtoLogLevel(field.value.integer64);
                break;
            case perfetto_protos_ProtoLogGroup_collect_stacktrace_field_number:
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_VARINT) return false;
                out_group->collect_stacktrace = (field.value.integer64 != 0);
                break;
        }
    }
    return field.status == PERFETTO_PB_DECODER_DONE;
}

bool ParseProtoLogConfig(struct PerfettoPbDecoder* decoder, ProtoLogConfig* out_config) {
    TracingMode tracingMode = TracingMode::DEFAULT;
    ProtoLogLevel defaultLevel = ProtoLogLevel::UNDEFINED;

    struct PerfettoPbDecoderField field;
    while ((field = PerfettoPbDecoderParseField(decoder)).status == PERFETTO_PB_DECODER_OK) {
        switch (field.id) {
            case perfetto_protos_ProtoLogConfig_group_overrides_field_number: {
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_DELIMITED) return false;

                struct PerfettoPbDecoder group_overrides_decoder = {
                        field.value.delimited.start,
                        field.value.delimited.start + field.value.delimited.len,
                };
                std::string group_name;
                ProtoLogGroupConfig group_cfg;

                if (!DeserializeProtoLogGroup(&group_overrides_decoder, &group_name, &group_cfg))
                    return false;

                if (!group_name.empty()) {
                    out_config->group_configs[group_name] = group_cfg;
                }
                break;
            }
            case perfetto_protos_ProtoLogConfig_tracing_mode_field_number:
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_VARINT) return false;
                tracingMode = (enum TracingMode)field.value.integer64;
                out_config->tracing_mode = tracingMode;
                break;
            case perfetto_protos_ProtoLogConfig_default_log_from_level_field_number:
                if (field.wire_type != PERFETTO_PB_WIRE_TYPE_VARINT) return false;
                defaultLevel = ParseProtoLogLevel(field.value.integer64);
                break;
        }

        if (tracingMode == TracingMode::ENABLE_ALL && defaultLevel == ProtoLogLevel::UNDEFINED) {
            defaultLevel = ProtoLogLevel::VERBOSE;
        }

        out_config->default_log_from_level = defaultLevel;

        for (auto& [group_name, group_cfg] : out_config->group_configs) {
            if (group_cfg.log_from == ProtoLogLevel::UNDEFINED) {
                group_cfg.log_from = defaultLevel;
            }
        }
    }

    return field.status == PERFETTO_PB_DECODER_DONE;
}

ProtoLogGroupConfig ProtoLogConfig::getConfigForGroup(const std::string_view group_name) const {
    auto it = group_configs.find(group_name);
    if (it != group_configs.end()) {
        return it->second;
    }
    ProtoLogGroupConfig default_cfg;
    if (tracing_mode == TracingMode::ENABLE_ALL) {
        default_cfg.log_from = default_log_from_level;
    }
    return default_cfg;
}

bool ProtoLogConfig::parse(const void* ds_config, size_t ds_config_size) {
    if (!ds_config || ds_config_size == 0) {
        ALOGI("No config provided, using defaults.");
        return true;
    }

    this->group_configs.clear();

    struct PerfettoPbDecoder config_decoder = {
            (const uint8_t*)ds_config,
            (const uint8_t*)ds_config + ds_config_size,
    };

    struct PerfettoPbDecoderField field;
    while ((field = PerfettoPbDecoderParseField(&config_decoder)).status ==
           PERFETTO_PB_DECODER_OK) {
        switch (field.id) {
            case perfetto_protos_DataSourceConfig_protolog_config_field_number: {
                struct PerfettoPbDecoder protolog_config_decoder = {
                        field.value.delimited.start,
                        field.value.delimited.start + field.value.delimited.len,
                };
                ParseProtoLogConfig(&protolog_config_decoder, this);
                break;
            }
            default: {
                ALOGD("Skipping unknown field in DataSourceConfig %d", field.id);
            }
        }
    }

    return field.status == PERFETTO_PB_DECODER_DONE;
}
} // namespace protolog
} // namespace android