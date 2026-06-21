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

#include "PipelineCallbackHandler.h"

#include <include/core/SkFourByteTag.h>

#include <android-base/stringprintf.h>
#include <common/trace.h>
#include <log/log_main.h>
#include <cstring>
#include "Base64.h"

#define SK_BEGIN_REQUIRE_DENSE \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic error \"-Wpadded\"")
#define SK_END_REQUIRE_DENSE _Pragma("clang diagnostic pop")

namespace android::renderengine::skia {

namespace {

void traceSerializedKey(sk_sp<SkData> data) {
    if (!data->size()) {
        SFTRACE_FORMAT("re_skia_serialized_key:invalid_key_empty");
        return;
    }

    std::string str;
    str.resize(Base64::EncodedSize(data->size()));
    Base64::Encode(data->data(), data->size(), str.data());

    SFTRACE_FORMAT("re_skia_serialized_key:%s", str.c_str());
}

} // anonymous namespace

PipelineCallbackHandler::PipelineCallbackHandler(bool isProtected, bool storeSerializedKeys)
      : mStartTime(std::chrono::steady_clock::now()),
        mIsProtected(isProtected),
        mStoreSerializedKeys(storeSerializedKeys) {
    mCurrentEpoch = 0;
    mLastEpochUpdateTime = mStartTime;
    mEpochOfLastSave = 0;
    // TODO(482036727): initialize 'mCurrentEpoch' and 'mEpochOfLastSave' from the cache file

    static_assert(kNumEpochsBetweenUsesSaves > kNumEpochsBetweenNewPipelineSaves);
}

void PipelineCallbackHandler::beginWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = true;
}
void PipelineCallbackHandler::endWarmup() {
    std::lock_guard<std::mutex> guard(mMutex);
    mInWarmup = false;
}

void PipelineCallbackHandler::updateEpoch() {
    std::chrono::time_point<std::chrono::steady_clock> curTime = std::chrono::steady_clock::now();

    std::chrono::seconds deltaSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(curTime - mLastEpochUpdateTime);

    uint32_t deltaEpochs = static_cast<uint32_t>(deltaSeconds.count() / kSecondsPerEpoch);

    constexpr std::chrono::seconds kActualSecondsPerEpoch(kSecondsPerEpoch);

    // Here we only count whole epochs. The partial epochs will be counted the next
    // time around. This means that 'mLastEpochUpdateTime' will always lag behind
    // now() a bit.
    mCurrentEpoch += deltaEpochs;
    mLastEpochUpdateTime += deltaEpochs * kActualSecondsPerEpoch;
}

void PipelineCallbackHandler::add(skgpu::graphite::ContextOptions::PipelineCacheOp op,
                                  const std::string& label, uint32_t uniqueKeyHash,
                                  bool fromPrecompile, sk_sp<SkData> serializedKey) {
    std::lock_guard<std::mutex> guard(mMutex);

    this->updateEpoch();

    auto iter = mMap.find(PipelineKey(&label, uniqueKeyHash));
    if (iter != mMap.end()) {
        // Pre-existing Pipeline - just update its usage(s)
        iter->second->mUses++;
        iter->second->mLastUsageEpoch = mCurrentEpoch;
    } else {
        mPipelineAddedSinceLastSave = true;

        if (serializedKey) {
            traceSerializedKey(serializedKey);
            if (!mStoreSerializedKeys) {
                serializedKey.reset();
            }
        }

        std::chrono::milliseconds creationTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - mStartTime);

        std::unique_ptr<PipelineData> newData =
                std::make_unique<PipelineData>(label, creationTime, std::move(serializedKey),
                                               mCurrentEpoch, fromPrecompile, mInWarmup);

        mMap.emplace(
                std::make_pair(PipelineKey(&newData->mLabel, uniqueKeyHash), std::move(newData)));
    }

    this->maybeSaveCache();
}

void PipelineCallbackHandler::report(const char* label, std::string& result) {
    // The assumption is that we're just doing this very infrequently so we just lock for the
    // entire method.
    std::lock_guard<std::mutex> guard(mMutex);

    std::vector<const PipelineData*> tmp;
    int precompileCount = 0, warmupCount = 0, normalCount = 0;

    tmp.reserve(mMap.size());
    for (const auto& [_, value] : mMap) {
        tmp.push_back(value.get());
        if (value->mFromPrecompile) {
            ++precompileCount;
        } else if (value->mFromWarmup) {
            ++warmupCount;
        } else {
            ++normalCount;
        }
    }

    std::sort(tmp.begin(), tmp.end(), [](const PipelineData* a, const PipelineData* b) {
        if (a->mUses != b->mUses) {
            return a->mUses > b->mUses;
        }
        return a->mLabel < b->mLabel;
    });

    base::StringAppendF(&result,
                        "%zu %s Pipelines (%d Warmup/%d Normal/%d Precompile) ----------\n",
                        tmp.size(), label, warmupCount, normalCount, precompileCount);

    for (const PipelineData* data : tmp) {
        base::StringAppendF(&result, "%c %d %.3fs %s %u %zuB\n",
                            data->mFromPrecompile ? 'P' : (data->mFromWarmup ? 'W' : 'N'),
                            data->mUses, data->mCreationTime.count() / 1000.0f,
                            data->mLabel.c_str(), data->mLastUsageEpoch,
                            data->mSerializedKey ? data->mSerializedKey->size() : 0);
    }
}

namespace {

static constexpr uint32_t kKeyCacheTag = SkSetFourByteTag('k', 'e', 'y', 's');
static constexpr uint32_t kKeyCacheVersion = 1;

SK_BEGIN_REQUIRE_DENSE
struct HeaderInfo {
    uint32_t fKeyCacheTag = kKeyCacheTag;
    uint32_t fKeyCacheVersion = kKeyCacheVersion;
    uint32_t fEpochOfSave;
    uint32_t fNumPipelineKeys;
};
SK_END_REQUIRE_DENSE
static constexpr uint32_t kHeaderSizeInBytes = sizeof(HeaderInfo);

SkSpan<uint8_t> write_header(SkSpan<uint8_t> dst, const HeaderInfo& info) {
    if (dst.size() < kHeaderSizeInBytes) {
        return {};
    }

    std::memcpy(dst.data(), &info, kHeaderSizeInBytes);
    return {dst.data() + kHeaderSizeInBytes, dst.size() - kHeaderSizeInBytes};
}

SkSpan<const uint8_t> read_header(SkSpan<const uint8_t> src, HeaderInfo* info) {
    if (src.size() < kHeaderSizeInBytes) {
        return {};
    }

    std::memcpy(info, src.data(), kHeaderSizeInBytes);
    if (info->fKeyCacheTag != kKeyCacheTag || info->fKeyCacheVersion != kKeyCacheVersion) {
        return {};
    }

    return {src.data() + kHeaderSizeInBytes, src.size() - kHeaderSizeInBytes};
}

SK_BEGIN_REQUIRE_DENSE
struct KeyPrefix {
    uint32_t fLastUseEpoch;
    uint32_t fKeySizeInBytes;
};
SK_END_REQUIRE_DENSE
static constexpr uint32_t kKeyPrefixSizeInBytes = sizeof(KeyPrefix);

SkSpan<uint8_t> write_key_prefix(SkSpan<uint8_t> dst, const KeyPrefix& prefix) {
    if (dst.size() < kKeyPrefixSizeInBytes) {
        return {};
    }

    std::memcpy(dst.data(), &prefix, kKeyPrefixSizeInBytes);
    return {dst.data() + kKeyPrefixSizeInBytes, dst.size() - kKeyPrefixSizeInBytes};
}

SkSpan<const uint8_t> read_key_prefix(SkSpan<const uint8_t> src, KeyPrefix* prefix) {
    if (src.size() < kKeyPrefixSizeInBytes) {
        return {};
    }

    std::memcpy(prefix, src.data(), kKeyPrefixSizeInBytes);
    return {src.data() + kKeyPrefixSizeInBytes, src.size() - kKeyPrefixSizeInBytes};
}

SkSpan<uint8_t> write_key_data(SkSpan<uint8_t> dst, SkData* data) {
    if (!data) {
        return dst;
    }

    if (dst.size() < data->size()) {
        return {};
    }

    std::memcpy(dst.data(), data->bytes(), data->size());
    return {dst.data() + data->size(), dst.size() - data->size()};
}

SkSpan<const uint8_t> read_key_data(SkSpan<const uint8_t> src, SkData* data) {
    if (!data) {
        return src;
    }

    if (src.size() < data->size()) {
        return {};
    }

    std::memcpy(data->writable_data(), src.data(), data->size());
    return {src.data() + data->size(), src.size() - data->size()};
}

} // anonymous namespace

// The structure of a cache blob is:
//     // header
//     uint32_t magic ID ("keys")
//     uint32_t version number
//     uint32_t epoch-of-the-save
//     uint32_t num-pipelines-being-saved
//     for (each pipeline being saved) {
//         // key prefix
//         uint32_t last use epoch
//         uint32_t size-in-bytes
//         // key data
//         the-actual-bytes
//     }
sk_sp<SkData> PipelineCallbackHandler::CreateBlob(const std::vector<const PipelineData*>& keys,
                                                  uint32_t epochOfSave, uint32_t rebaseEpoch) {
    if (keys.size() > kMaxNumSerializedPipelineKeys) {
        ALOGW("Failed max number of serialized keys limit");
    }

    if (epochOfSave < rebaseEpoch) {
        ALOGW("Invalid rebaseEpoch");
        return nullptr;
    }

    epochOfSave -= rebaseEpoch;

    uint32_t requiredBytes = kHeaderSizeInBytes + keys.size() * kKeyPrefixSizeInBytes;
    for (const PipelineData* key : keys) {
        uint32_t keySizeInBytes =
                key->mSerializedKey ? static_cast<uint32_t>(key->mSerializedKey->size()) : 0;
        if (keySizeInBytes > kMaxSerializedKeySizeInBytes) {
            ALOGW("Failed max serialized key size limit");
        }
        requiredBytes += keySizeInBytes;
    }

    std::unique_ptr<uint8_t> base(static_cast<uint8_t*>(malloc(requiredBytes)));

    HeaderInfo info;
    info.fEpochOfSave = epochOfSave;
    info.fNumPipelineKeys = keys.size();
    SkSpan<uint8_t> bytes = write_header({base.get(), requiredBytes}, info);
    if (!bytes.begin()) {
        ALOGE("Failed to write serialized key blob header");
        return nullptr;
    }
    for (const PipelineData* key : keys) {
        uint32_t keySizeInBytes =
                key->mSerializedKey ? static_cast<uint32_t>(key->mSerializedKey->size()) : 0;
        if (key->mLastUsageEpoch < rebaseEpoch) {
            ALOGW("Invalid rebaseEpoch");
            return nullptr;
        }
        bytes = write_key_prefix(bytes, {key->mLastUsageEpoch - rebaseEpoch, keySizeInBytes});
        if (!bytes.begin()) {
            ALOGE("Failed to write serialized key blob key prefix");
            return nullptr;
        }
        bytes = write_key_data(bytes, key->mSerializedKey.get());
        if (!bytes.begin()) {
            ALOGE("Failed to write serialized key blob key data");
            return nullptr;
        }
    }

    return SkData::MakeFromMalloc(base.release(), requiredBytes);
}

bool PipelineCallbackHandler::UnpackBlob(SkData* src, std::vector<SerializedKeyInfo>* keysOut,
                                         uint32_t* epochOfSave) {
    HeaderInfo info;
    SkSpan<const uint8_t> bytes = read_header(src->byteSpan(), &info);
    if (!bytes.begin()) {
        ALOGE("Failed to read serialized key blob header");
        return false;
    }
    if (info.fNumPipelineKeys > kMaxNumSerializedPipelineKeys) {
        ALOGE("Failed max number of keys in blob invariant %u", info.fNumPipelineKeys);
        return false;
    }
    *epochOfSave = info.fEpochOfSave;

    keysOut->reserve(info.fNumPipelineKeys);
    for (int i = 0; i < info.fNumPipelineKeys; ++i) {
        KeyPrefix prefix;
        bytes = read_key_prefix(bytes, &prefix);
        if (!bytes.begin()) {
            ALOGE("Failed to read serialized key blob key prefix");
            return false;
        }
        if (prefix.fKeySizeInBytes > kMaxSerializedKeySizeInBytes) {
            ALOGE("Failed max serialized key size invariant %u", prefix.fKeySizeInBytes);
            return false;
        }
        sk_sp<SkData> key = prefix.fKeySizeInBytes
                ? SkData::MakeUninitialized(prefix.fKeySizeInBytes)
                : nullptr;
        bytes = read_key_data(bytes, key.get());
        if (!bytes.begin()) {
            ALOGE("Failed to read serialized key blob key data");
            return false;
        }

        keysOut->push_back({prefix.fLastUseEpoch, std::move(key)});
    }

    return true;
}

// Gather reduces the current set of tracked Pipelines down to a set to be
// serialized and returns that set. As part of this it enforces the following
// constraints:
//    Each individual serialized key is <= kMaxSerializedKeySizeInBytes
//    The total number of keys to be saved is <= kMaxNumSerializedPipelineKeys
//    The total memory required to store the set of keys is <= kMaxBlobSizeInBytes
//    All of the keys being serialized were used in the last seven days of uptime.
//
// Additionally, Gather returns an epoch that can be used to rebase all the other epochs.
// This value may be less than the actual minimum 'mLastUsageEpoch' of the returned
// PipelinesData's.
std::vector<const PipelineCallbackHandler::PipelineData*> PipelineCallbackHandler::Gather(
        const PipelineMap& map, uint32_t currentEpoch, uint32_t* rebaseEpoch) {
    std::vector<const PipelineData*> result;

    size_t totalMemInBlob = kHeaderSizeInBytes;
    *rebaseEpoch = UINT32_MAX;

    result.reserve(map.size());
    for (const auto& [_, value] : map) {
        if (value->mSerializedKey) {
            if (value->mSerializedKey->size() > kMaxSerializedKeySizeInBytes) {
                continue;
            }
            uint32_t epochDelta = currentEpoch - value->mLastUsageEpoch;
            if (epochDelta > kTooOldInEpochs) {
                continue;
            }
            totalMemInBlob += kKeyPrefixSizeInBytes;
            totalMemInBlob += value->mSerializedKey->size();
            *rebaseEpoch = std::min(*rebaseEpoch, value->mLastUsageEpoch);
            result.push_back(value.get());
        }
    }

    std::sort(result.begin(), result.end(), [](const PipelineData* a, const PipelineData* b) {
        // Sort into MRU -> LRU order
        return a->mLastUsageEpoch > b->mLastUsageEpoch;
    });

    while (totalMemInBlob > kMaxBlobSizeInBytes) {
        totalMemInBlob -= kKeyPrefixSizeInBytes;
        totalMemInBlob -= result.back()->mSerializedKey->size();
        result.pop_back();
    }

    if (result.size() > kMaxNumSerializedPipelineKeys) {
        // If necessary, drop the less recently used Pipelines. This should be very very rare.
        ALOGW("Hit max serialized key limit - dropping less recently used Pipelines");
        result.resize(kMaxNumSerializedPipelineKeys);
    }

    return result;
}

bool PipelineCallbackHandler::maybeSaveCache() {
    if (!mStoreSerializedKeys) {
        return false;
    }

    uint32_t epochDelta = mCurrentEpoch - mEpochOfLastSave;

    // TODO: we also need to ensure that no saves occur during warmup and or precompile.
    bool saveForNew = mPipelineAddedSinceLastSave && epochDelta > kNumEpochsBetweenNewPipelineSaves;
    bool saveForUses = epochDelta > kNumEpochsBetweenUsesSaves;

    if (!saveForNew && !saveForUses) {
        // In this case we don't need to create the blob.
        return false;
    }

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineData*> pipelinesToSave = Gather(mMap, mCurrentEpoch, &rebaseEpoch);

    sk_sp<SkData> blob = CreateBlob(pipelinesToSave, mCurrentEpoch, rebaseEpoch);
    if (!blob) {
        return false;
    }

    mPipelineAddedSinceLastSave = false;
    mEpochOfLastSave = mCurrentEpoch;

    // TODO(b/482036727): save the blob to the cache here
    return true;
}

} // namespace android::renderengine::skia
