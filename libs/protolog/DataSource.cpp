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

#define LOG_TAG "ProtoLog"

#include "DataSource.h"
#include "ProtoLogConfig.h"
#include "ProtoLogTypes.h"
#include "perfetto/public/protos/trace/trace.pzc.h"
#include "protos/trace/android/protolog.pzc.h"
#include "protos/trace/profiling/profile_common.pzc.h"

#include <config/android/protolog_config.pzc.h>
#include <log/log.h>
#include <perfetto/public/abi/pb_decoder_abi.h>
#include <perfetto/public/data_source.h>
#include <perfetto/public/pb_utils.h>
#include <perfetto/public/producer.h>
#include <perfetto/public/protos/config/data_source_config.pzc.h>
#include <perfetto/public/protos/trace/interned_data/interned_data.pzc.h>
#include <perfetto/public/protos/trace/trace_packet.pzc.h>
#include <string.h>
#include <utils/CallStack.h>
#include <utils/String8.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;
constexpr uint64_t FNV_PRIME = 0x100000001b3;

uint64_t fnv1a_64(std::string_view str, uint64_t hash = FNV_OFFSET_BASIS) {
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= FNV_PRIME;
    }
    return hash;
}
} // namespace

namespace android {
namespace protolog {
namespace datasource {

struct PerfettoDs gDataSource = PERFETTO_DS_INIT();
std::once_flag gInitializeFlag;
std::shared_mutex g_config_mutex;
std::unordered_map<PerfettoDsInstanceIndex, std::shared_ptr<const ProtoLogConfig>>
        g_instance_configs;

static void* OnSetupCb(struct PerfettoDsImpl* ds, PerfettoDsInstanceIndex inst_id, void* ds_config,
                       size_t ds_config_size, void* user_arg, struct PerfettoDsOnSetupArgs* args) {
    std::shared_ptr<ProtoLogConfig> instance_config = std::make_shared<ProtoLogConfig>();
    if (!instance_config->parse(ds_config, ds_config_size)) {
        ALOGE("Failed to parse config for instance %u", inst_id);
    }

    InstanceContext* context = new InstanceContext();
    context->inst_id = inst_id;

    {
        std::lock_guard<std::shared_mutex> lock(g_config_mutex);
        g_instance_configs[inst_id] = std::move(instance_config);
    }
    return context;
}

static void OnDestroyCb(struct PerfettoDsImpl*, void* user_arg, void* instance_state) {
    if (!instance_state) return;
    InstanceContext* context = static_cast<InstanceContext*>(instance_state);
    PerfettoDsInstanceIndex inst_id = context->inst_id;

    std::shared_ptr<const ProtoLogConfig> config_ptr = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(g_config_mutex);
        auto it = g_instance_configs.find(inst_id);
        if (it != g_instance_configs.end()) {
            config_ptr = it->second;
            g_instance_configs.erase(it);
        } else {
            ALOGE("Config not found for instance %u in on_destroy_cb!", inst_id);
        }
    }
    delete context;
}

static void* OnCreateTlsCb(struct PerfettoDsImpl* ds_impl, PerfettoDsInstanceIndex inst_id,
                           struct PerfettoDsTracerImpl* tracer, void* user_arg) {
    ProtoLogTlsState* tls_state = new ProtoLogTlsState();
    std::shared_ptr<const ProtoLogConfig> config_ptr = nullptr;
    {
        std::shared_lock lock(g_config_mutex);
        auto it = g_instance_configs.find(inst_id);
        if (it != g_instance_configs.end()) {
            config_ptr = it->second;
        }
    }
    if (config_ptr != nullptr) {
        tls_state->config = config_ptr;
    } else {
        ALOGE("Config not found for instance %u during on_create_tls_cb!", inst_id);
    }
    return tls_state;
}

static void OnDeleteTlsCb(void* ptr) {
    delete static_cast<ProtoLogTlsState*>(ptr);
}

static void* OnCreateIncrCb(struct PerfettoDsImpl*, PerfettoDsInstanceIndex,
                            struct PerfettoDsTracerImpl*, void*) {
    return new IncrementalState();
}

static void OnDeleteIncrCb(void* ptr) {
    delete static_cast<IncrementalState*>(ptr);
}

void InitDataSource(uint32_t backends) {
    ALOGD("Initializing ProtoLog DataSource");

    std::call_once(gInitializeFlag, [backends] {
        struct PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
        args.backends = backends;
        PerfettoProducerInit(args);

        struct PerfettoDsParams params = PerfettoDsParamsDefault();
        params.user_arg = nullptr;

        params.on_setup_cb = OnSetupCb;
        params.on_destroy_cb = OnDestroyCb;
        params.on_create_tls_cb = OnCreateTlsCb;
        params.on_delete_tls_cb = OnDeleteTlsCb;
        params.on_create_incr_cb = OnCreateIncrCb;
        params.on_delete_incr_cb = OnDeleteIncrCb;

        PerfettoDsRegister(&gDataSource, NAME.cbegin(), params);
    });
}

InternResult IncrementalState::internGroup(std::string_view group) {
    uint64_t hash = fnv1a_64(group); // Use the same FNV-1a helper
    auto [id, is_new] = group_intern_table.getOrEmplace(group, hash);
    return {id, is_new};
}

InternResult IncrementalState::internMessage(ProtoLogLevel level, std::string_view group,
                                             std::string_view format) {
    uint64_t hash = fnv1a_64(group);
    hash = fnv1a_64(format, hash);
    hash ^= static_cast<uint8_t>(level);
    hash *= FNV_PRIME;

    auto key = std::make_tuple(level, group, format);
    auto [id, is_new] = message_intern_table.getOrEmplace(key, hash);

    return {id, is_new};
}

InternResult IncrementalState::internString(std::string_view str) {
    auto [id, is_new] = string_intern_table.getOrEmplace(str, next_string_id);
    if (is_new) {
        next_string_id++;
    }
    return {id, is_new};
}

} // namespace datasource
} // namespace protolog
} // namespace android
