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

#ifndef PipelineCallbackHandler_DEFINED
#define PipelineCallbackHandler_DEFINED

#include <android-base/thread_annotations.h>

#include <include/core/SkData.h>
#include <include/gpu/graphite/ContextOptions.h>

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace android::renderengine::skia {

class PipelineCallbackHandler {
public:
    static void Callback(void* context, skgpu::graphite::ContextOptions::PipelineCacheOp op,
                         const std::string& label, uint32_t uniqueKeyHash, bool fromPrecompile,
                         sk_sp<SkData> androidStyleKey) {
        PipelineCallbackHandler* handler = reinterpret_cast<PipelineCallbackHandler*>(context);
        handler->add(op, label, uniqueKeyHash, fromPrecompile, std::move(androidStyleKey));
    }

    PipelineCallbackHandler(bool isProtected, bool storeSerializedKeys);

    void beginWarmup() EXCLUDES(mMutex);
    void endWarmup() EXCLUDES(mMutex);

    // This is called by Skia to report some Pipeline caching event:
    //    PipelineCacheOp::kAddingPipeline --> a Pipeline is being added to the cache
    //    PipelineCacheOp::kPipelineFound  --> a preexisting Pipeline was found in the cache
    // Note that, due to purging, it is possible for the same Pipeline to be added to the cache
    // multiple times.
    void add(skgpu::graphite::ContextOptions::PipelineCacheOp op, const std::string& label,
             uint32_t uniqueKeyHash, bool fromPrecompile, sk_sp<SkData> androidStyleKey)
            EXCLUDES(mMutex);

    void report(const char* label, std::string& result) EXCLUDES(mMutex);

protected:
    std::mutex mMutex;

    void updateEpoch() EXCLUSIVE_LOCKS_REQUIRED(mMutex);

    bool maybeSaveCache() EXCLUSIVE_LOCKS_REQUIRED(mMutex);

    // This is held as a unique_ptr in 'mMap' to simplify sorting in report() and to provide
    // a stable std::string* for the PipelineKey to use.
    struct PipelineData {
        explicit PipelineData(const std::string& label, std::chrono::milliseconds creationTime,
                              sk_sp<SkData> serializedKey, uint32_t creationEpoch,
                              bool fromPrecompile, bool fromWarmup)
              : mLabel(label),
                mCreationTime(creationTime),
                mSerializedKey(std::move(serializedKey)),
                mLastUsageEpoch(creationEpoch),
                mUses(fromPrecompile ? 0 : (fromWarmup ? 0 : 1)),
                mFromPrecompile(fromPrecompile),
                mFromWarmup(fromWarmup) {}
        const std::string mLabel;
        const std::chrono::milliseconds mCreationTime;
        const sk_sp<SkData> mSerializedKey;
        uint32_t mLastUsageEpoch = 0;
        uint32_t mUses;
        const bool mFromPrecompile;
        const bool mFromWarmup;
    };

    static constexpr uint32_t kMaxNumSerializedPipelineKeys = 512;
    static constexpr uint32_t kMaxSerializedKeySizeInBytes = 1024;
    static constexpr uint32_t kMaxBlobSizeInBytes = 32768;

    struct SerializedKeyInfo {
        uint32_t mLastUsageEpoch;
        sk_sp<SkData> mSerializedKey;
    };

    static sk_sp<SkData> CreateBlob(const std::vector<const PipelineData*>& keys,
                                    uint32_t epochOfSave, uint32_t rebaseEpoch);
    static bool UnpackBlob(SkData* src, std::vector<SerializedKeyInfo>* keysOut,
                           uint32_t* epochOfSave);

    // 'mLabel' will either point to: a temporary search label for find()
    //                                or PipelineData::mLabel for emplace()
    struct PipelineKey {
        explicit PipelineKey(const std::string* label, uint32_t uniqueKeyHash)
              : mLabel(label), mUniqueKeyHash(uniqueKeyHash) {}
        PipelineKey() = default;

        std::size_t operator()(const PipelineKey& k) const { return k.mUniqueKeyHash; }

        bool operator==(const PipelineKey& other) const {
            return mUniqueKeyHash == other.mUniqueKeyHash && *mLabel == *other.mLabel;
        }

        const std::string* mLabel = nullptr;
        uint32_t mUniqueKeyHash = 0;
    };

    using PipelineMap = std::unordered_map<PipelineKey, std::unique_ptr<PipelineData>, PipelineKey>;

    static std::vector<const PipelineData*> Gather(const PipelineMap&, uint32_t currentEpoch,
                                                   uint32_t* rebaseEpoch);

    const std::chrono::time_point<std::chrono::steady_clock> mStartTime;
    PipelineMap mMap GUARDED_BY(mMutex);
    bool mInWarmup GUARDED_BY(mMutex) = false;
    const bool mIsProtected;
    // Although the SerializedPipelineKeyCache's behavior is separately enabled there is
    // still extra work in this object that can be avoided if the serialized keys will
    // never be saved.
    const bool mStoreSerializedKeys;

    bool mPipelineAddedSinceLastSave GUARDED_BY(mMutex) = false;

    static constexpr uint32_t kSecondsPerEpoch = 10;
    // Each epoch is a roughly 10s block of accumulated uptime.
    // Storing it in a uint32_t yields ~1362 years until it would overflow.
    // The epoch starting time is reset on every file load, however, so it is
    // extremely unlikely to ever overflow.
    uint32_t mCurrentEpoch GUARDED_BY(mMutex) = 0;
    uint32_t mEpochOfLastSave GUARDED_BY(mMutex) = 0;
    std::chrono::time_point<std::chrono::steady_clock> mLastEpochUpdateTime GUARDED_BY(mMutex);

    // Even when 'mPipelineAddedSinceLastSave' is true the next constant will be used to space out
    // saves to reduce disk thrashing.
    static constexpr uint32_t kNumEpochsBetweenNewPipelineSaves = 2; // 20s
    // Regardless of whether a new pipeline has been added, the file will be saved after the
    // following number of epochs. This serves to update the usage counts.
    static constexpr uint32_t kNumEpochsBetweenUsesSaves = 42; // 7 minutes

    static constexpr uint32_t kSecondsPerDay = 86400;
    // Pipelines older than this won't be serialized so, eventually, they will be removed from
    // the cache.
    static constexpr uint32_t kTooOldInEpochs = (7 * kSecondsPerDay) / kSecondsPerEpoch;
};

} // namespace android::renderengine::skia

#endif // PipelineCallbackHandler_DEFINED
