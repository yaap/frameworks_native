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

#include "ProtoLog.h"

#ifdef __ANDROID__

#include <android_tracing.h>
#include <log/log.h>
#include <perfetto/public/producer.h>
#include "DataSource.h"

#include "protos/trace/android/protolog.pzc.h"
#include "protos/trace/profiling/profile_common.pzc.h"

#include <time.h>
#include <cinttypes>
#include <cstdarg>
#include <string>
#include <utility>
#include <vector>

#include <perfetto/public/pb_utils.h>
#include <perfetto/public/protos/trace/interned_data/interned_data.pzc.h>
#include <perfetto/public/protos/trace/trace_packet.pzc.h>

#include <perfetto/public/data_source.h>
#include <string.h>

namespace android {
namespace protolog {

thread_local std::vector<std::pair<const std::string, datasource::InternResult>> new_strings;

void Initialize() {
    Initialize(PERFETTO_BACKEND_SYSTEM);
}

void Initialize(uint32_t backends) {
    ALOGD("Initializing ProtoLog");

    if (!android_tracing_native_proto_logging()) {
        ALOGD("ProtoLog is disabled. Skipping initialization.");
        return;
    }

    android::protolog::datasource::InitDataSource(backends);
}

static inline uint64_t GetBootTimeNanos() {
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + static_cast<uint64_t>(ts.tv_nsec);
}

static inline perfetto_protos_ProtoLogLevel GetLogLevelProtoValue(ProtoLogLevel level) {
    switch (level) {
        case ProtoLogLevel::UNDEFINED:
            return perfetto_protos_PROTOLOG_LEVEL_UNDEFINED;
        case ProtoLogLevel::VERBOSE:
            return perfetto_protos_PROTOLOG_LEVEL_VERBOSE;
        case ProtoLogLevel::DEBUG:
            return perfetto_protos_PROTOLOG_LEVEL_DEBUG;
        case ProtoLogLevel::INFO:
            return perfetto_protos_PROTOLOG_LEVEL_INFO;
        case ProtoLogLevel::WARN:
            return perfetto_protos_PROTOLOG_LEVEL_WARN;
        case ProtoLogLevel::ERROR:
            return perfetto_protos_PROTOLOG_LEVEL_ERROR;
        case ProtoLogLevel::WTF:
            return perfetto_protos_PROTOLOG_LEVEL_WTF;
    }
}

static inline datasource::IncrementalState* useIncrementalState(
        struct PerfettoDsTracerIterator* ctx) {
    datasource::IncrementalState* incr_state = static_cast<datasource::IncrementalState*>(
            PerfettoDsGetIncrementalState(&datasource::gDataSource, ctx));
    if (!incr_state->clearReported) {
        struct PerfettoDsRootTracePacket clear_packet;
        PerfettoDsTracerPacketBegin(ctx, &clear_packet);
        uint32_t flags =
                static_cast<uint32_t>(perfetto_protos_TracePacket_SEQ_INCREMENTAL_STATE_CLEARED);
        perfetto_protos_TracePacket_set_sequence_flags(&clear_packet.msg, flags);
        PerfettoDsTracerPacketEnd(ctx, &clear_packet);
        incr_state->clearReported = true;
    }

    return incr_state;
}

static inline uint64_t internProtoLogMessage(struct PerfettoDsTracerIterator* ctx,
                                             datasource::IncrementalState* incr_state,
                                             ProtoLogLevel level, const std::string_view group,
                                             const char* format, uint64_t ts) {
    auto group_res = incr_state->internGroup(group);
    auto msg_res = incr_state->internMessage(level, group, format);
    bool needs_viewer_config_packet = group_res.is_new || msg_res.is_new;

    if (needs_viewer_config_packet) {
        struct PerfettoDsRootTracePacket intern_packet;
        PerfettoDsTracerPacketBegin(ctx, &intern_packet);
        perfetto_protos_TracePacket_set_timestamp(&intern_packet.msg, ts);
        // Add viewer config for new groups/messages
        if (group_res.is_new || msg_res.is_new) {
            struct perfetto_protos_ProtoLogViewerConfig viewer_config_data;
            perfetto_protos_TracePacket_begin_protolog_viewer_config(&intern_packet.msg,
                                                                     &viewer_config_data);
            if (group_res.is_new) {
                struct perfetto_protos_ProtoLogViewerConfig_Group group_data;
                perfetto_protos_ProtoLogViewerConfig_begin_groups(&viewer_config_data, &group_data);
                perfetto_protos_ProtoLogViewerConfig_Group_set_id(&group_data, group_res.id);
                perfetto_protos_ProtoLogViewerConfig_Group_set_name(&group_data, group.cbegin(),
                                                                    group.length());
                perfetto_protos_ProtoLogViewerConfig_Group_set_tag(&group_data, group.cbegin(),
                                                                   group.length());
                perfetto_protos_ProtoLogViewerConfig_end_groups(&viewer_config_data, &group_data);
            }
            if (msg_res.is_new) {
                struct perfetto_protos_ProtoLogViewerConfig_MessageData message_data;
                perfetto_protos_ProtoLogViewerConfig_begin_messages(&viewer_config_data,
                                                                    &message_data);
                perfetto_protos_ProtoLogViewerConfig_MessageData_set_message(&message_data, format,
                                                                             strlen(format));
                perfetto_protos_ProtoLogViewerConfig_MessageData_set_message_id(&message_data,
                                                                                msg_res.id);
                perfetto_protos_ProtoLogViewerConfig_MessageData_set_level(&message_data,
                                                                           GetLogLevelProtoValue(
                                                                                   level));
                perfetto_protos_ProtoLogViewerConfig_MessageData_set_group_id(&message_data,
                                                                              group_res.id);
                perfetto_protos_ProtoLogViewerConfig_end_messages(&viewer_config_data,
                                                                  &message_data);
            }
            perfetto_protos_TracePacket_end_protolog_viewer_config(&intern_packet.msg,
                                                                   &viewer_config_data);
        }
        PerfettoDsTracerPacketEnd(ctx, &intern_packet);
    }

    return msg_res.id;
}

static inline void internStringArgs(
        struct PerfettoDsTracerIterator* ctx,
        const std::vector<std::pair<const std::string, datasource::InternResult>>& strings) {
    struct PerfettoDsRootTracePacket intern_packet;
    PerfettoDsTracerPacketBegin(ctx, &intern_packet);
    struct perfetto_protos_InternedData interned_data;
    perfetto_protos_TracePacket_begin_interned_data(&intern_packet.msg, &interned_data);
    for (const auto& pair : strings) {
        if (pair.second.is_new) {
            struct perfetto_protos_InternedString internedString;
            perfetto_protos_InternedData_begin_protolog_string_args(&interned_data,
                                                                    &internedString);
            perfetto_protos_InternedString_set_iid(&internedString, pair.second.id);
            perfetto_protos_InternedString_set_str(&internedString, pair.first.c_str(),
                                                   pair.first.length());
            perfetto_protos_InternedData_end_protolog_string_args(&interned_data, &internedString);
        }
    }
    perfetto_protos_TracePacket_end_interned_data(&intern_packet.msg, &interned_data);
    PerfettoDsTracerPacketEnd(ctx, &intern_packet);
}

template <typename MessageIdGetter, typename ArgProcessor>
void LogInternal(ProtoLogLevel level, const std::string_view group,
                 MessageIdGetter message_id_getter, ArgProcessor arg_processor,
                 bool incrementalStateRequired) {
    if (!android_tracing_native_proto_logging()) {
        return;
    }
    auto current_boottime_ns = GetBootTimeNanos();
    PERFETTO_DS_TRACE(datasource::gDataSource, ctx) {
        datasource::ProtoLogTlsState* tls_state = static_cast<datasource::ProtoLogTlsState*>(
                PerfettoDsGetCustomTls(&datasource::gDataSource, &ctx));
        auto group_config = tls_state->config->getConfigForGroup(group);
        if (level < group_config.log_from) {
            continue;
        }

        datasource::IncrementalState* incr_state = useIncrementalState(&ctx);

        datasource::InternResult stacktrace_res = {0, false};
        if (group_config.collect_stacktrace) {
            // TODO(b/477870887): Unsupported for now. Context: b/473868195.
        }

        uint64_t msgId = message_id_getter(ctx, *incr_state, current_boottime_ns);

        struct PerfettoDsRootTracePacket msg_packet;
        PerfettoDsTracerPacketBegin(&ctx, &msg_packet);
        perfetto_protos_TracePacket_set_timestamp(&msg_packet.msg, current_boottime_ns);
        struct perfetto_protos_ProtoLogMessage protolog_msg;
        perfetto_protos_TracePacket_begin_protolog_message(&msg_packet.msg, &protolog_msg);
        perfetto_protos_ProtoLogMessage_set_message_id(&protolog_msg, msgId);

        bool needsIncrementalState = incrementalStateRequired;
        if (stacktrace_res.id != 0) {
            perfetto_protos_ProtoLogMessage_set_stacktrace_iid(&protolog_msg, stacktrace_res.id);
            needsIncrementalState = true;
        }

        new_strings.clear();
        if (arg_processor(protolog_msg, *incr_state)) {
            needsIncrementalState = true;
        }

        perfetto_protos_TracePacket_end_protolog_message(&msg_packet.msg, &protolog_msg);

        if (needsIncrementalState) {
            perfetto_protos_TracePacket_set_sequence_flags(
                    &msg_packet.msg,
                    static_cast<uint32_t>(perfetto_protos_TracePacket_SEQ_NEEDS_INCREMENTAL_STATE));
        }
        PerfettoDsTracerPacketEnd(&ctx, &msg_packet);

        if (!new_strings.empty()) {
            internStringArgs(&ctx, new_strings);
        }
    }
}

class VarArgumentProvider : public IArgumentProvider {
public:
    explicit VarArgumentProvider(va_list& original_args) : original_args_(original_args) {}

    ~VarArgumentProvider() override { va_end(current_args_); }

    void newPass() override { va_copy(current_args_, original_args_); }

    void endPass() override { va_end(current_args_); }

    long long nextInt() override { return va_arg(current_args_, int); }

    double nextDouble() override { return va_arg(current_args_, double); }

    const char* nextString() override {
        const char* str = va_arg(current_args_, const char*);
        return str ? str : "null";
    }

    void releaseString() override {
        // no-op
    }

    bool nextBool() override { return va_arg(current_args_, int) != 0; }

    // Disallow copy and assignment.
    VarArgumentProvider(const VarArgumentProvider&) = delete;
    VarArgumentProvider& operator=(const VarArgumentProvider&) = delete;

private:
    va_list& original_args_; // Reference to the va_list in the Log function's scope
    va_list current_args_;   // Per-pass copy of the va_list
};

void Log(ProtoLogLevel level, const std::string_view group, const char* format, ...) {
    va_list args;
    va_start(args, format);
    VarArgumentProvider arg_provider(args);
    Log(level, group, format, arg_provider);
    va_end(args);
}

void Log(ProtoLogLevel level, const std::string_view group, const char* format,
         IArgumentProvider& args_provider) {
    LogInternal(
            level, group,
            [&](struct PerfettoDsTracerIterator& ctx, datasource::IncrementalState& incr_state,
                uint64_t ts) {
                return internProtoLogMessage(&ctx, &incr_state, level, group, format, ts);
            },
            [&](perfetto_protos_ProtoLogMessage& protolog_msg,
                datasource::IncrementalState& incr_state) {
                args_provider.newPass();
                bool needsIncrementalState = false;
                const char* p = format;
                while (*p) {
                    if (*p == '%') {
                        p++;
                        switch (*p) {
                            case 'd':
                            case 'i':
                                perfetto_protos_ProtoLogMessage_set_sint64_params(
                                        &protolog_msg, args_provider.nextInt());
                                break;
                            case 'f':
                                perfetto_protos_ProtoLogMessage_set_double_params(
                                        &protolog_msg, args_provider.nextDouble());
                                break;
                            case 's': {
                                const char* str = args_provider.nextString();
                                if (!str) {
                                    str = "null";
                                }
                                auto res = incr_state.internString(str);
                                if (res.is_new) {
                                    new_strings.emplace_back(str, res);
                                }
                                args_provider.releaseString();
                                perfetto_protos_ProtoLogMessage_set_str_param_iids(&protolog_msg,
                                                                                   res.id);
                                needsIncrementalState = true;
                                break;
                            }
                            case 'b':
                                perfetto_protos_ProtoLogMessage_set_boolean_params(
                                        &protolog_msg, args_provider.nextBool());
                                break;
                            case '%':
                                break; // Skip '%%'
                            default:
                                ALOGE("Unknown format specifier: %c", *p);
                                break;
                        }
                    }
                    p++;
                }

                args_provider.endPass();
                return needsIncrementalState;
            },
            true);
}

void Log(ProtoLogLevel level, const std::string_view group, uint64_t messageHash,
         uint64_t paramsMask, int argCount, IArgumentProvider& args_provider) {
    LogInternal(
            level, group,
            [&](struct PerfettoDsTracerIterator&, datasource::IncrementalState&, uint64_t) {
                return messageHash;
            },
            [&](perfetto_protos_ProtoLogMessage& protolog_msg,
                datasource::IncrementalState& incr_state) {
                args_provider.newPass();
                bool needsIncrementalState = false;

                for (int i = 0; i < argCount; i++) {
                    int type = (paramsMask >> (i * 2)) & 0b11;
                    switch (type) {
                        case 0b00: { // String
                            const char* str = args_provider.nextString();
                            if (!str) str = "null";
                            auto res = incr_state.internString(str);
                            if (res.is_new) {
                                new_strings.emplace_back(str, res);
                            }
                            args_provider.releaseString();
                            perfetto_protos_ProtoLogMessage_set_str_param_iids(&protolog_msg,
                                                                               res.id);
                            needsIncrementalState = true;
                            break;
                        }
                        case 0b01: { // Integer
                            perfetto_protos_ProtoLogMessage_set_sint64_params(&protolog_msg,
                                                                              args_provider
                                                                                      .nextInt());
                            break;
                        }
                        case 0b10: { // Float
                            perfetto_protos_ProtoLogMessage_set_double_params(
                                    &protolog_msg, args_provider.nextDouble());
                            break;
                        }
                        case 0b11: { // Boolean
                            perfetto_protos_ProtoLogMessage_set_boolean_params(&protolog_msg,
                                                                               args_provider
                                                                                       .nextBool());
                            break;
                        }
                    }
                }

                args_provider.endPass();
                return needsIncrementalState;
            },
            false);
}

} // namespace protolog
} // namespace android

#endif
