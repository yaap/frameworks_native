/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "../skia/compat/PipelineCallbackHandler.h"

namespace android::renderengine {

namespace {

// This derived class splits open the PipelineCallbackHandler for testing
class PipelineCallbackHandlerTest : public skia::PipelineCallbackHandler {
public:
    // constants
    using skia::PipelineCallbackHandler::kMaxBlobSizeInBytes;
    using skia::PipelineCallbackHandler::kMaxNumSerializedPipelineKeys;
    using skia::PipelineCallbackHandler::kMaxSerializedKeySizeInBytes;
    using skia::PipelineCallbackHandler::kNumEpochsBetweenNewPipelineSaves;
    using skia::PipelineCallbackHandler::kNumEpochsBetweenUsesSaves;
    using skia::PipelineCallbackHandler::kTooOldInEpochs;

    // types
    using skia::PipelineCallbackHandler::PipelineData;
    using skia::PipelineCallbackHandler::PipelineKey;
    using skia::PipelineCallbackHandler::PipelineMap;
    using skia::PipelineCallbackHandler::SerializedKeyInfo;

    // methods
    using skia::PipelineCallbackHandler::CreateBlob;
    using skia::PipelineCallbackHandler::Gather;
    using skia::PipelineCallbackHandler::UnpackBlob;

    PipelineCallbackHandlerTest()
          : skia::PipelineCallbackHandler(/* isProtected= */ false,
                                          /* storeSerializedKeys= */ true) {}

    // This method plays a game with 'mLastEpochUpdateTime' in order to move the
    // PipelineCallbackHandler's current epoch forward. There is ~10s of leeway where the result of
    // getCurrentEpoch() will be as expected.
    void fastForward(uint32_t numEpochs) {
        std::lock_guard<std::mutex> guard(mMutex);

        std::chrono::time_point<std::chrono::steady_clock> curTime =
                std::chrono::steady_clock::now();
        mLastEpochUpdateTime = curTime - std::chrono::seconds(10 * numEpochs);

        std::chrono::seconds deltaSeconds =
                std::chrono::duration_cast<std::chrono::seconds>(curTime - mLastEpochUpdateTime);
        uint32_t tensOfSeconds = static_cast<uint32_t>(deltaSeconds.count() / 10);
        ASSERT_EQ(tensOfSeconds, numEpochs);
    }

    bool pipelineAddedSinceLastSave() EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        return mPipelineAddedSinceLastSave;
    }

    uint32_t getCurrentEpoch() EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        return mCurrentEpoch;
    }

    struct PipelineStats {
        uint32_t mLastUsageEpoch = 0;
        uint32_t mUses = 0;
        bool mHasData = false;
    };

    PipelineStats getPipelineStats(const char* label, uint32_t uniqueKeyHash) EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        std::string searchLabel(label);
        auto iter = mMap.find(PipelineKey(&searchLabel, uniqueKeyHash));
        if (iter != mMap.end()) {
            return {iter->second.get()->mLastUsageEpoch, iter->second->mUses,
                    iter->second->mSerializedKey != nullptr};
        }

        return {};
    }

    struct CacheStats {
        uint32_t mTotNumPipelines = 0;
        uint32_t mNumPipelinesWithData = 0;
        uint32_t mNumPipelinesWithOutData = 0;
        size_t mTotalMemInSerializedKeys = 0;
        uint32_t mEpochOfLastSave = 0;
        bool mPipelineAdded = false;
    };

    CacheStats getCacheStats() EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        CacheStats stats;

        stats.mTotNumPipelines = static_cast<uint32_t>(mMap.size());
        stats.mEpochOfLastSave = mEpochOfLastSave;
        stats.mPipelineAdded = mPipelineAddedSinceLastSave;
        for (const auto& [_, value] : mMap) {
            if (value->mSerializedKey) {
                ++stats.mNumPipelinesWithData;
                stats.mTotalMemInSerializedKeys += value->mSerializedKey->size();
            } else {
                ++stats.mNumPipelinesWithOutData;
            }
        }

        return stats;
    }

    bool publicMaybeSaveCache() EXCLUDES(mMutex) {
        std::lock_guard<std::mutex> guard(mMutex);

        this->updateEpoch();
        return this->maybeSaveCache();
    }
};

enum class ActionType {
    kAddPipeline,
    kUsePipeline,
    kCheckCacheStats,
    kCheckPipeline,
    kAdvanceEpoch,
    kCheckEpoch,
    kTryToSaveCache,
};

class Action {
public:
    static Action AddPipeline(const char* label, uint32_t hash, bool hasData,
                              uint32_t dataPayload) {
        Action action(ActionType::kAddPipeline);
        action.mLabel = label;
        action.mHash = hash;
        action.mData = hasData ? SkData::MakeWithCopy(&dataPayload, sizeof(dataPayload)) : nullptr;
        return action;
    }
    static Action UsePipeline(const char* label, uint32_t hash) {
        Action action(ActionType::kUsePipeline);
        action.mLabel = label;
        action.mHash = hash;
        return action;
    }
    static Action CheckCacheStats(const PipelineCallbackHandlerTest::CacheStats& expectedStats) {
        Action action(ActionType::kCheckCacheStats);
        action.mExpectedCacheStats = expectedStats;
        return action;
    }
    static Action CheckPipeline(const char* label, uint32_t hash,
                                const PipelineCallbackHandlerTest::PipelineStats& expectedStats) {
        Action action(ActionType::kCheckPipeline);
        action.mLabel = label;
        action.mHash = hash;
        action.mExpectedPipelineStats = expectedStats;
        return action;
    }
    static Action AdvanceEpoch(uint32_t epochs) {
        Action action(ActionType::kAdvanceEpoch);
        action.mEpochs = epochs;
        return action;
    }
    static Action CheckEpoch(uint32_t epoch) {
        Action action(ActionType::kCheckEpoch);
        action.mEpochs = epoch;
        return action;
    }
    static Action TryToSaveCache(bool succeeds) {
        Action action(ActionType::kTryToSaveCache);
        action.mSucceeds = succeeds;
        return action;
    }

    void execute(PipelineCallbackHandlerTest* handler, std::string& /* log */) const {
        using skgpu::graphite::ContextOptions::PipelineCacheOp::kAddingPipeline;
        using skgpu::graphite::ContextOptions::PipelineCacheOp::kPipelineFound;

        switch (mType) {
            case ActionType::kAddPipeline:
                handler->add(kAddingPipeline, mLabel, mHash, /* fromPrecompile= */ false, mData);
                break;
            case ActionType::kUsePipeline:
                handler->add(kPipelineFound, mLabel, mHash, /* fromPrecompile= */ false, nullptr);
                break;
            case ActionType::kCheckCacheStats: {
                PipelineCallbackHandlerTest::CacheStats stats = handler->getCacheStats();
                ASSERT_EQ(stats.mTotNumPipelines, mExpectedCacheStats.mTotNumPipelines);
                ASSERT_EQ(stats.mNumPipelinesWithData, mExpectedCacheStats.mNumPipelinesWithData);
                ASSERT_EQ(stats.mNumPipelinesWithOutData,
                          mExpectedCacheStats.mNumPipelinesWithOutData);
                ASSERT_EQ(stats.mTotalMemInSerializedKeys,
                          mExpectedCacheStats.mTotalMemInSerializedKeys);
                ASSERT_EQ(stats.mEpochOfLastSave, mExpectedCacheStats.mEpochOfLastSave);
                ASSERT_EQ(stats.mPipelineAdded, mExpectedCacheStats.mPipelineAdded);
            } break;
            case ActionType::kCheckPipeline: {
                PipelineCallbackHandlerTest::PipelineStats stats =
                        handler->getPipelineStats(mLabel, mHash);
                ASSERT_EQ(stats.mLastUsageEpoch, mExpectedPipelineStats.mLastUsageEpoch);
                ASSERT_EQ(stats.mUses, mExpectedPipelineStats.mUses);
                ASSERT_EQ(stats.mHasData, mExpectedPipelineStats.mHasData);
            } break;
            case ActionType::kAdvanceEpoch:
                handler->fastForward(mEpochs);
                break;
            case ActionType::kCheckEpoch:
                ASSERT_EQ(mEpochs, handler->getCurrentEpoch());
                break;
            case ActionType::kTryToSaveCache: {
                bool cacheSaved = handler->publicMaybeSaveCache();
                ASSERT_EQ(cacheSaved, mSucceeds);
            } break;
        }
    }

private:
    Action(ActionType type) : mType(type) {}

    const ActionType mType;
    // kAddPipeline and kUsePipeline
    const char* mLabel = nullptr;
    uint32_t mHash = 0;
    // kAddPipeline
    sk_sp<SkData> mData = nullptr;

    // kCheckCacheStats
    PipelineCallbackHandlerTest::CacheStats mExpectedCacheStats;

    // kCheckPipeline
    PipelineCallbackHandlerTest::PipelineStats mExpectedPipelineStats;

    // kAdvanceEpoch and kCheckEpoch
    uint32_t mEpochs = 0;

    // kTryToSaveCache
    bool mSucceeds = false;
};

void run_test(SkSpan<const Action> actions) {
    std::string log;

    PipelineCallbackHandlerTest handler;

    // The cache should always start out unmodified
    ASSERT_EQ(handler.pipelineAddedSinceLastSave(), false);

    for (const Action& action : actions) {
        action.execute(&handler, log);
    }
}

} // anonymous namespace

// Just add a Pipeline and check that it got added
TEST(PipelineCallbackHandlerTest, smokeTest0) {
    const Action kActions[] = {
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 0, /* #withOut */ 1,
                                     /* totDataSize */ 0, /* lastSaveEpoch */ 0,
                                     /* pipelineAdded */ true}),
    };

    run_test(kActions);
}

// Add a Pipeline and then add a usage
TEST(PipelineCallbackHandlerTest, smokeTest1) {
    const Action kActions[] = {
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::UsePipeline("1", 1),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 0, /* #withOut */ 1,
                                     /* totDataSize */ 0, /* lastSaveEpoch */ 0,
                                     /* pipelineAdded */ true}),
            Action::CheckPipeline("1", 1,
                                  {/* lastUseEpoch */ 0, /* uses=*/2, /* mHasData */ false}),
    };

    run_test(kActions);
}

// Only the label and hash value are used for keying. Check that a hash collision works correctly.
TEST(PipelineCallbackHandlerTest, smokeTest2) {
    const Action kActions[] = {
            // Note that the hash values are the same (i.e., 1)
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::AddPipeline("2", 1, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 0, /* #withOut */ 2,
                                     /* totDataSize */ 0, /* lastSaveEpoch */ 0,
                                     /* pipelineAdded */ true}),
    };

    run_test(kActions);
}

// Add one key w/ data and w/o to check that the counting is correct.
TEST(PipelineCallbackHandlerTest, smokeTest3) {
    const Action kActions[] = {
            // Note that the hash values are the same (i.e., 1)
            Action::AddPipeline("1", 1, /* hasData= */ true, 1),
            Action::AddPipeline("2", 2, /* hasData= */ false, 0),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 1, /* #withOut */ 1,
                                     /* totDataSize */ 4, /* lastSaveEpoch */ 0,
                                     /* pipelineAdded */ true}),
    };

    run_test(kActions);
}

// Verify that the epoch counter is working as expected:
//   Indirectly that an epoch is ~10s (due to fastForward)
//   The current epoch is set as the initial last use epoch
//   add() is updating the current epoch
TEST(PipelineCallbackHandlerTest, checkEpochsCounter) {
    const Action kActions[] = {
            Action::CheckEpoch(0),
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::CheckPipeline("1", 1,
                                  {/* lastUseEpoch */ 0, /* uses=*/1, /* mHasData */ false}),

            Action::AdvanceEpoch(1),
            Action::AddPipeline("2", 2, /* hasData= */ false, 0),
            Action::CheckEpoch(1),
            Action::CheckPipeline("2", 2,
                                  {/* lastUseEpoch */ 1, /* uses=*/1, /* mHasData */ false}),
    };

    run_test(kActions);
}

// Verify that using the Pipeline updates its last-use-epoch
TEST(PipelineCallbackHandlerTest, checkUseChangesLastUse) {
    const Action kActions[] = {
            Action::CheckEpoch(0),
            Action::AddPipeline("1", 1, /* hasData= */ false, 0),
            Action::CheckPipeline("1", 1,
                                  {/* lastUseEpoch */ 0, /* uses=*/1, /* mHasData */ false}),

            Action::AdvanceEpoch(10),
            Action::UsePipeline("1", 1),
            Action::CheckEpoch(10),
            Action::CheckPipeline("1", 1,
                                  {/* lastUseEpoch */ 10, /* uses=*/2, /* mHasData */ false}),
    };

    run_test(kActions);
}

// Verify that the Handler's 'mEpochOfLastSave' and 'mPipelineAddedSinceLastSave' update correctly
// wrt pipeline additions
TEST(PipelineCallbackHandlerTest, checkLastSaveEpochNewPipelines) {
    constexpr uint32_t kEpochsPerPipelineSave =
            PipelineCallbackHandlerTest::kNumEpochsBetweenNewPipelineSaves;

    const Action kActions[] = {
            Action::AddPipeline("1", 1, /* hasData= */ true, 0),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                                     /* totDataSize */ 4, /* lastSaveEpoch */ 0,
                                     /* pipelineAdded */ true}),
            Action::AdvanceEpoch(kEpochsPerPipelineSave + 1),
            Action::TryToSaveCache(true),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                                     /* totDataSize */ 4,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ false}),

            Action::AddPipeline("2", 2, /* hasData= */ true, 0),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 2, /* #withOut */ 0,
                                     /* totDataSize */ 8,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ true}),
            Action::TryToSaveCache(
                    false), // there is a new pipeline but the time limit hasn't been reached

            Action::AdvanceEpoch(kEpochsPerPipelineSave + 1),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 2, /* #withOut */ 0,
                                     /* totDataSize */ 8,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ true}),
            Action::TryToSaveCache(true),
            Action::CheckCacheStats({/* tot# */ 2, /* #withData */ 2, /* #withOut */ 0,
                                     /* totDataSize */ 8,
                                     /* lastSaveEpoch */ 2 * kEpochsPerPipelineSave + 2,
                                     /* pipelineAdded */ false}),

            Action::AdvanceEpoch(1),
            Action::TryToSaveCache(
                    false), // cache hasn't changed and neither time limit hasn't been reached
    };

    run_test(kActions);
}

// Verify that the Handler's 'mEpochOfLastSave' and 'mPipelineAddedSinceLastSave' update correctly
// wrt pipeline uses
TEST(PipelineCallbackHandlerTest, checkLastSaveEpochUses) {
    constexpr uint32_t kEpochsPerPipelineSave =
            PipelineCallbackHandlerTest::kNumEpochsBetweenNewPipelineSaves;
    constexpr uint32_t kEpochsPerUsesSave = PipelineCallbackHandlerTest::kNumEpochsBetweenUsesSaves;

    const Action kActions[] = {
            //----- Preamble - add a Pipeline and "save" the cache
            Action::AdvanceEpoch(kEpochsPerPipelineSave + 1),
            Action::AddPipeline("1", 1, /* hasData= */ true, 0),
            Action::CheckCacheStats(
                    {/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                     /* totDataSize */ 4, /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                     /* pipelineAdded */ false}), // f since AddPipeline triggered a save
            Action::TryToSaveCache(false),        // save should've occurred upon the AddPipeline
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                                     /* totDataSize */ 4,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ false}),

            //----- Actual test
            Action::UsePipeline("1", 1),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                                     /* totDataSize */ 4,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ false}),
            Action::TryToSaveCache(false), // haven't reached uses time limit

            Action::AdvanceEpoch(kEpochsPerUsesSave + 1),
            Action::CheckCacheStats({/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                                     /* totDataSize */ 4,
                                     /* lastSaveEpoch */ kEpochsPerPipelineSave + 1,
                                     /* pipelineAdded */ false}),
            Action::TryToSaveCache(true), // we have reached the uses time limit
            Action::CheckCacheStats(
                    {/* tot# */ 1, /* #withData */ 1, /* #withOut */ 0,
                     /* totDataSize */ 4,
                     /* lastSaveEpoch */ kEpochsPerUsesSave + kEpochsPerPipelineSave + 2,
                     /* pipelineAdded */ false}),
    };

    run_test(kActions);
}

namespace {

void create_keys(std::vector<const PipelineCallbackHandlerTest::PipelineData*>* keys,
                 uint32_t numKeys) {
    using PipelineData = PipelineCallbackHandlerTest::PipelineData;
    constexpr std::chrono::milliseconds kCreationTimeMS{0};

    for (uint32_t i = 0; i < numKeys; ++i) {
        sk_sp<SkData> data = SkData::MakeWithCopy(&i, sizeof(i));
        // Only 'data' and 'creationEpoch' are going to be serialized
        PipelineData* tmp = new PipelineData(std::to_string(i), kCreationTimeMS, std::move(data),
                                             /* creationEpoch= */ i, false, false);
        keys->push_back(tmp);
    }
}

void check_keys(const std::vector<const PipelineCallbackHandlerTest::PipelineData*>& orig,
                const std::vector<PipelineCallbackHandlerTest::SerializedKeyInfo>& readBack) {
    if (orig.size() != readBack.size()) {
        return;
    }

    for (size_t i = 0; i < orig.size(); ++i) {
        ASSERT_EQ(orig[i]->mLastUsageEpoch, readBack[i].mLastUsageEpoch);
        ASSERT_EQ(*orig[i]->mSerializedKey, *readBack[i].mSerializedKey);
    }
}

void run_serialize_keys_test(
        sk_sp<SkData> blob,
        const std::vector<const PipelineCallbackHandlerTest::PipelineData*>& orig,
        bool expectedToSucceed, uint32_t expectedEpoch) {
    using SerializedKeyInfo = PipelineCallbackHandlerTest::SerializedKeyInfo;

    // Usually the test creates the blob from 'orig'. When testing blob truncation however
    // a munged blob is passed in.
    if (!blob) {
        blob = PipelineCallbackHandlerTest::CreateBlob(orig, expectedEpoch, /* rebaseEpoch= */ 0);
        EXPECT_TRUE(blob != nullptr);
    }

    std::vector<SerializedKeyInfo> readBack;
    uint32_t readBackEpochOfSave = 0;
    bool result =
            PipelineCallbackHandlerTest::UnpackBlob(blob.get(), &readBack, &readBackEpochOfSave);
    ASSERT_EQ(result, expectedToSucceed);

    if (!expectedToSucceed) {
        return;
    }

    ASSERT_EQ(expectedEpoch, readBackEpochOfSave);
    ASSERT_EQ(orig.size(), readBack.size());
    check_keys(orig, readBack);
}

} // anonymous namespace

// Basic test to ensure data round trips through serialization
TEST(PipelineCallbackHandlerTest, blobCreationRoundTrip) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    create_keys(&orig, 7);
    ASSERT_EQ(orig.size(), 7ul);

    run_serialize_keys_test(nullptr, orig, /* expectedToSucceed= */ true, kEpochOfSave);
}

// Test out handling of a missing serializedKey. Even though such Pipelines should be
// filtered out during the gathering stage the blob serialization can deal with them.
TEST(PipelineCallbackHandlerTest, blobCreationMissingKey) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    create_keys(&orig, 3);
    ASSERT_EQ(orig.size(), 3ul);

    // Remove the serialized key from the second pipeline
    const_cast<sk_sp<SkData>*>(&orig[1]->mSerializedKey)->reset();

    run_serialize_keys_test(nullptr, orig, /* expectedToSucceed= */ true, kEpochOfSave);
}

// Test out handling of too large of a serialized key. In this case we suspect some sort
// of corruption of the file since the gathering phase should eliminate such keys.
TEST(PipelineCallbackHandlerTest, blobCreationKeyTooLarge) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    create_keys(&orig, 3);
    ASSERT_EQ(orig.size(), 3ul);

    // Overwrite the second serialized key with an invalidly large one
    sk_sp<SkData> tooBig = SkData::MakeZeroInitialized(
            2 * PipelineCallbackHandlerTest::kMaxSerializedKeySizeInBytes);
    const_cast<sk_sp<SkData>*>(&orig[1]->mSerializedKey)->swap(tooBig);

    run_serialize_keys_test(nullptr, orig, /* expectedToSucceed= */ false, kEpochOfSave);
}

// Test out handling of too many keys. In this case we suspect some sort
// of corruption of the file since the gathering phase should limit the number of keys.
TEST(PipelineCallbackHandlerTest, blobCreationTooManyKeys) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    create_keys(&orig, PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys + 1);
    ASSERT_EQ(orig.size(), PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys + 1);

    run_serialize_keys_test(nullptr, orig, /* expectedToSucceed= */ false, kEpochOfSave);
}

// Test out creating a blob with no keys. This should never happen but shouldn't crash.
TEST(PipelineCallbackHandlerTest, blobCreationNoKeys) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    ASSERT_EQ(orig.size(), 0ul);

    run_serialize_keys_test(nullptr, orig, /* expectedToSucceed= */ true, kEpochOfSave);
}

// Test out truncated blobs. These should all be detected and fail unpacking since
// we suspect corruption of the file.
TEST(PipelineCallbackHandlerTest, blobCreationTruncatedBlobs) {
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> orig;
    constexpr uint32_t kEpochOfSave = 77;

    create_keys(&orig, 1);
    ASSERT_EQ(orig.size(), 1ul);

    sk_sp<SkData> blob =
            PipelineCallbackHandlerTest::CreateBlob(orig, kEpochOfSave, /* rebaseEpoch= */ 0);
    EXPECT_TRUE(blob != nullptr);

    size_t numSlices = blob->size() / sizeof(uint32_t);

    for (size_t i = 0; i < numSlices; i++) {
        sk_sp<SkData> tmp = blob->shareSubset(0, i * sizeof(uint32_t));
        run_serialize_keys_test(std::move(tmp), orig, /* expectedToSucceed= */ false, kEpochOfSave);
    }
}

namespace {

void add(PipelineCallbackHandlerTest::PipelineMap* map, uint32_t uniqueKeyHash,
         size_t dataSizeInBytes, uint32_t epoch) {
    std::string label = std::to_string(uniqueKeyHash);

    sk_sp<SkData> serializedKey =
            dataSizeInBytes ? SkData::MakeZeroInitialized(dataSizeInBytes) : nullptr;

    constexpr std::chrono::milliseconds kDefaultCreationTime = std::chrono::milliseconds(0);

    auto newData =
            std::make_unique<PipelineCallbackHandlerTest::PipelineData>(label, kDefaultCreationTime,
                                                                        std::move(serializedKey),
                                                                        epoch,
                                                                        /* fromPrecompile= */ false,
                                                                        /* fromWarmup= */ false);

    map->emplace(std::make_pair(PipelineCallbackHandlerTest::PipelineKey(&newData->mLabel,
                                                                         uniqueKeyHash),
                                std::move(newData)));
}

void check_result(const std::vector<const PipelineCallbackHandlerTest::PipelineData*>& actual,
                  SkSpan<const uint32_t> expectedLastUsageEpochs) {
    ASSERT_EQ(expectedLastUsageEpochs.size(), actual.size());
    if (expectedLastUsageEpochs.size() != actual.size()) {
        return;
    }

    for (size_t i = 0; i < expectedLastUsageEpochs.size(); i++) {
        ASSERT_EQ(expectedLastUsageEpochs[i], actual[i]->mLastUsageEpoch);
    }
}

// Check that all the serialization invariants are met
void check_limits(const std::vector<const PipelineCallbackHandlerTest::PipelineData*>& keys,
                  uint32_t currentEpoch) {
    ASSERT_TRUE(keys.size() <= PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys);

    for (auto key : keys) {
        ASSERT_NE(key->mSerializedKey, nullptr);
        if (key->mSerializedKey) {
            ASSERT_TRUE(key->mSerializedKey->size() <=
                        PipelineCallbackHandlerTest::kMaxSerializedKeySizeInBytes);

            uint32_t epochDelta = currentEpoch - key->mLastUsageEpoch;
            ASSERT_TRUE(epochDelta <= PipelineCallbackHandlerTest::kTooOldInEpochs);
        }
    }

    sk_sp<SkData> blob = PipelineCallbackHandlerTest::CreateBlob(keys, /* epochOfSave= */ 0,
                                                                 /* rebaseEpoch= */ 0);
    ASSERT_NE(blob, nullptr);

    if (blob) {
        ASSERT_TRUE(blob->size() <= PipelineCallbackHandlerTest::kMaxBlobSizeInBytes);
    }
}

} // anonymous namespace

// Check basic MRU ordering behavior of the gather step
TEST(PipelineCallbackHandlerTest, gatherMRUOrder) {
    PipelineCallbackHandlerTest::PipelineMap map;

    add(&map, /* uniqueKeyHash= */ 1, /* dataSizeInBytes= */ 4, /* epoch= */ 1);
    add(&map, /* uniqueKeyHash= */ 2, /* dataSizeInBytes= */ 4, /* epoch= */ 2);
    add(&map, /* uniqueKeyHash= */ 3, /* dataSizeInBytes= */ 4, /* epoch= */ 3);

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, 3, &rebaseEpoch);

    ASSERT_EQ(rebaseEpoch, 1u);
    check_limits(keys, /* currentEpoch= */ 3);
    check_result(keys, {3, 2, 1});
}

// Pipelines w/o a serialized key should never be serialized
TEST(PipelineCallbackHandlerTest, gatherMissingData) {
    PipelineCallbackHandlerTest::PipelineMap map;

    add(&map, /* uniqueKeyHash= */ 1, /* dataSizeInBytes= */ 4, /* epoch= */ 1);
    add(&map, /* uniqueKeyHash= */ 2, /* dataSizeInBytes= */ 0, /* epoch= */ 2);
    add(&map, /* uniqueKeyHash= */ 3, /* dataSizeInBytes= */ 4, /* epoch= */ 3);

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, 3, &rebaseEpoch);

    ASSERT_EQ(rebaseEpoch, 1u);
    check_limits(keys, /* currentEpoch= */ 3);
    check_result(keys, {3, 1});
}

// Serialized keys that are too big individually should be eliminated
TEST(PipelineCallbackHandlerTest, gatherKeyTooBig) {
    PipelineCallbackHandlerTest::PipelineMap map;

    constexpr uint32_t kTooBig = PipelineCallbackHandlerTest::kMaxSerializedKeySizeInBytes + 1;
    add(&map, /* uniqueKeyHash= */ 1, /* dataSizeInBytes= */ 4, /* epoch= */ 1);
    add(&map, /* uniqueKeyHash= */ 2, /* dataSizeInBytes= */ kTooBig, /* epoch= */ 2);
    add(&map, /* uniqueKeyHash= */ 3, /* dataSizeInBytes= */ 4, /* epoch= */ 3);

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, 3, &rebaseEpoch);

    ASSERT_EQ(rebaseEpoch, 1u);
    check_limits(keys, /* currentEpoch= */ 3);
    check_result(keys, {3, 1});
}

// Test out the hard limit on the number of keys
TEST(PipelineCallbackHandlerTest, gatherTooManyKeys) {
    PipelineCallbackHandlerTest::PipelineMap map;

    constexpr uint32_t kMaxEpoch = PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys + 10;
    for (uint32_t i = 1; i <= kMaxEpoch; ++i) {
        add(&map, /* uniqueKeyHash= */ i, /* dataSizeInBytes= */ 4, /* epoch= */ i);
    }

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, kMaxEpoch, &rebaseEpoch);

    // Although the older Pipelines get removed that doesn't happen until after the
    // initial pass.
    ASSERT_EQ(rebaseEpoch, 1u);
    check_limits(keys, kMaxEpoch);

    ASSERT_EQ(PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys, keys.size());
    uint32_t expectedEpoch = kMaxEpoch;
    for (auto key : keys) {
        ASSERT_EQ(expectedEpoch--, key->mLastUsageEpoch);
    }
}

// Test out the serialized cache's max size limit
TEST(PipelineCallbackHandlerTest, gatherTooBig) {
    PipelineCallbackHandlerTest::PipelineMap map;

    // Add the max number of allowed pipelines each of which has the max allowed sized serialized
    // key
    constexpr uint32_t kMaxEpoch = PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys;
    for (uint32_t i = 1; i <= kMaxEpoch; ++i) {
        add(&map, /* uniqueKeyHash= */ i, PipelineCallbackHandlerTest::kMaxSerializedKeySizeInBytes,
            /* epoch= */ i);
    }

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, kMaxEpoch, &rebaseEpoch);

    ASSERT_TRUE(PipelineCallbackHandlerTest::kMaxNumSerializedPipelineKeys > keys.size());
    // Although the excess Pipelines get removed that doesn't happen until after the
    // initial pass.
    ASSERT_EQ(rebaseEpoch, 1u);
    check_limits(keys, kMaxEpoch);
}

// Test out the age limit and Blob rebasing
TEST(PipelineCallbackHandlerTest, gatherTooOld) {
    PipelineCallbackHandlerTest::PipelineMap map;

    constexpr uint32_t kAncientEpoch = PipelineCallbackHandlerTest::kTooOldInEpochs + 10;
    add(&map, /* uniqueKeyHash= */ 1, /* dataSizeInBytes= */ 4, /* epoch= */ kAncientEpoch - 1);
    add(&map, /* uniqueKeyHash= */ 2, /* dataSizeInBytes= */ 4, /* epoch= */ 2);
    add(&map, /* uniqueKeyHash= */ 3, /* dataSizeInBytes= */ 4, /* epoch= */ kAncientEpoch - 3);

    uint32_t rebaseEpoch = 0;
    std::vector<const PipelineCallbackHandlerTest::PipelineData*> keys =
            PipelineCallbackHandlerTest::Gather(map, kAncientEpoch, &rebaseEpoch);

    ASSERT_EQ(rebaseEpoch, kAncientEpoch - 3);
    check_limits(keys, /* currentEpoch= */ kAncientEpoch);
    check_result(keys, {kAncientEpoch - 1, kAncientEpoch - 3});

    // Verify that rebasing occurs correctly during serialization
    sk_sp<SkData> blob = PipelineCallbackHandlerTest::CreateBlob(keys, kAncientEpoch, rebaseEpoch);
    ASSERT_NE(blob, nullptr);

    std::vector<PipelineCallbackHandlerTest::SerializedKeyInfo> readBack;
    uint32_t readBackEpochOfSave = 0;
    bool result =
            PipelineCallbackHandlerTest::UnpackBlob(blob.get(), &readBack, &readBackEpochOfSave);
    ASSERT_EQ(result, true);

    ASSERT_EQ(readBackEpochOfSave, 3u);
    ASSERT_EQ(readBack.size(), 2u);
    ASSERT_EQ(readBack[0].mLastUsageEpoch, 2u);
    ASSERT_EQ(readBack[1].mLastUsageEpoch, 0u);
}

} // namespace android::renderengine
