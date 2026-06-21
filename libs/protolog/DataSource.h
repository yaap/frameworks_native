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

#ifndef ANDROID_INTERNAL_DATASOURCE_H
#define ANDROID_INTERNAL_DATASOURCE_H

#include <perfetto/public/data_source.h>
#include <utils/String8.h>
#include <functional>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include "ProtoLogConfig.h"
#include "ProtoLogTypes.h"

namespace android {
namespace protolog {
namespace datasource {

inline constexpr std::string_view NAME = "android.protolog";

extern struct PerfettoDs gDataSource;

extern std::shared_mutex g_config_mutex;
extern std::unordered_map<PerfettoDsInstanceIndex, std::shared_ptr<const ProtoLogConfig>>
        g_instance_configs;

template <typename K, typename V, typename Hash = std::hash<K>, typename Eq = std::equal_to<K>>
class LruCache {
public:
    LruCache(size_t maxSize) : mMaxSize(maxSize) {}

    template <typename Q, typename J>
    std::pair<V, bool> getOrEmplace(Q&& key, J&& newValue) {
        auto it = mMap.find(key);
        if (it != mMap.end()) {
            mList.splice(mList.begin(), mList, it->second);
            return {it->second->second, false};
        }

        if (mMap.size() >= mMaxSize) {
            // Map is full, reuse the least recently used node.
            auto lruNodeIt = std::prev(mList.end());
            auto node = mMap.extract(lruNodeIt->first);

            lruNodeIt->first = std::forward<Q>(key);
            lruNodeIt->second = std::forward<J>(newValue);
            mList.splice(mList.begin(), mList, lruNodeIt);

            node.key() = lruNodeIt->first;
            node.mapped() = mList.begin();
            mMap.insert(std::move(node));
        } else {
            // Map not full, create a new node.
            mList.emplace_front(std::forward<Q>(key), std::forward<J>(newValue));
            mMap.emplace_hint(it, mList.front().first, mList.begin());
        }

        return {mList.front().second, true};
    }

private:
    size_t mMaxSize;
    std::list<std::pair<K, V>> mList;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator, Hash, Eq> mMap;
};

struct InstanceContext {
    PerfettoDsInstanceIndex inst_id;
};

struct ProtoLogTlsState {
    std::shared_ptr<const ProtoLogConfig> config = nullptr;
};

void InitDataSource(uint32_t backends);

struct InternResult {
    uint64_t id;
    bool is_new;
};

using MessageKey = std::tuple<ProtoLogLevel, std::string, std::string>;

/* Custom hash function for MessageKey (a std::tuple). std::tuple does not have a default std::hash
 * specialization, so we need to provide our own to use it as a key in std::unordered_map. This
 * implementation uses a hash combining algorithm similar to the one found in Boost
 * (boost::hash_combine). It computes the hash of each element in the tuple and then combines them
 * into a single hash value. The combining formula uses a magic number (0x9e3779b9, related to the
 * golden ratio) and bitwise operations to ensure a good distribution of hash values, which is
 * crucial for the performance of the unordered_map by minimizing collisions. This makes it both
 * fast and effective for our use case.
 */
struct MessageKeyHash {
    using is_transparent = void;

    template <class T1, class T2, class T3>
    size_t operator()(const std::tuple<T1, T2, T3>& key) const {
        size_t h1 = std::hash<T1>()(std::get<0>(key));
        size_t h2 = std::hash<T2>()(std::get<1>(key));
        size_t h3 = std::hash<T3>()(std::get<2>(key));
        size_t seed = h1;
        auto hash_combine = [&](size_t other_hash) {
            seed ^= other_hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        hash_combine(h2);
        hash_combine(h3);
        return seed;
    }
};

struct StringHash {
    using is_transparent = void;
    size_t operator()(const char* txt) const { return std::hash<std::string_view>{}(txt); }
    size_t operator()(std::string_view txt) const { return std::hash<std::string_view>{}(txt); }
    size_t operator()(const std::string& txt) const { return std::hash<std::string>{}(txt); }
};

struct IncrementalState {
public:
    bool clearReported = false;

    InternResult internGroup(std::string_view group);
    InternResult internMessage(ProtoLogLevel level, std::string_view group,
                               std::string_view format);
    InternResult internString(std::string_view str);

private:
    static constexpr size_t kMaxInternedStrings = 1024;
    static constexpr size_t kMaxInternedMessages = 512;

    uint64_t next_string_id = 1;

    LruCache<std::string, uint64_t, StringHash, std::equal_to<>> group_intern_table{
            kMaxInternedStrings};
    LruCache<std::string, uint64_t, StringHash, std::equal_to<>> string_intern_table{
            kMaxInternedStrings};
    LruCache<MessageKey, uint64_t, MessageKeyHash, std::equal_to<>> message_intern_table{
            kMaxInternedMessages};
};

} // namespace datasource
} // namespace protolog
} // namespace android

#endif // ANDROID_INTERNAL_DATASOURCE_H
