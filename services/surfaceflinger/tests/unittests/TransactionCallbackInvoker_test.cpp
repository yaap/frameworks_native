/*
 * Copyright 2024 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <binder/Binder.h>
#include <gui/ITransactionCompletedListener.h>
#include <ui/Fence.h>

#include "BackgroundExecutor.h"
#include "TransactionCallbackInvoker.h"

namespace android {

using namespace testing;

class MockTransactionCompletedListener : public BnTransactionCompletedListener {
public:
    MOCK_METHOD(void, onTransactionCompleted, (ListenerStats stats), (override));
    MOCK_METHOD(void, onReleaseBuffer,
                (ReleaseCallbackId callbackId, sp<Fence> releaseFence,
                 uint32_t currentMaxAcquiredBufferCount, bool removeFromCache),
                (override));
    MOCK_METHOD(void, onTransactionQueueStalled, (const String8& name), (override));
    MOCK_METHOD(void, onTrustedPresentationChanged, (int id, bool inTrustedPresentationState),
                (override));
};

class TransactionCallbackInvokerTest : public Test {
public:
    TransactionCallbackInvoker mInvoker;

    sp<IBinder> mSurfaceControl = sp<BBinder>::make();

    void flushCallbacks() { BackgroundExecutor::getInstanceForTransaction().flushQueue(); }
};

TEST_F(TransactionCallbackInvokerTest, sendCallbacks_callsListener) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();
    std::vector<CallbackId> callbackIds = {{1, CallbackId::Type::ON_COMPLETE}};

    CallbackHandle handle{listener, callbackIds, mSurfaceControl};
    handle.latchTime = 12345;
    mInvoker.addCallbackHandle(std::move(handle));

    sp<Fence> presentFence = sp<Fence>::make();
    mInvoker.addPresentFence(presentFence);

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](const ListenerStats& stats) {
        ASSERT_EQ(stats.transactionStats.size(), 1u);
        EXPECT_EQ(stats.transactionStats[0].latchTime, 12345);
        EXPECT_EQ(stats.transactionStats[0].presentFence, presentFence);
    });

    mInvoker.sendCallbacks(false);
    flushCallbacks();
}

TEST_F(TransactionCallbackInvokerTest, sendCallbacks_groupsTransactionStatsByListener) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();

    std::vector<CallbackId> callbackIds1 = {{1, CallbackId::Type::ON_COMPLETE}};
    std::vector<CallbackId> callbackIds2 = {{2, CallbackId::Type::ON_COMPLETE}};

    mInvoker.addCallbackHandle({listener, callbackIds1, mSurfaceControl});
    mInvoker.addCallbackHandle({listener, callbackIds2, mSurfaceControl});

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](const ListenerStats& stats) {
        EXPECT_EQ(stats.listener, listener);
        ASSERT_EQ(stats.transactionStats.size(), 2u);
        EXPECT_EQ(stats.transactionStats[0].callbackIds[0], callbackIds1[0]);
        EXPECT_EQ(stats.transactionStats[1].callbackIds[0], callbackIds2[0]);
    });

    mInvoker.sendCallbacks(false);
    flushCallbacks();
}

TEST_F(TransactionCallbackInvokerTest, sendCallbacks_groupsSurfaceStatsByCallbackIds) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();

    std::vector<CallbackId> callbackIds = {{1, CallbackId::Type::ON_COMPLETE}};
    sp<IBinder> surfaceControl2 = sp<BBinder>::make();

    mInvoker.addCallbackHandle({listener, callbackIds, mSurfaceControl});
    mInvoker.addCallbackHandle({listener, callbackIds, surfaceControl2});

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](const ListenerStats& stats) {
        EXPECT_EQ(stats.listener, listener);
        ASSERT_EQ(stats.transactionStats.size(), 1u);
        ASSERT_EQ(stats.transactionStats[0].surfaceStats.size(), 2u);
        EXPECT_EQ(stats.transactionStats[0].surfaceStats[0].surfaceControl, mSurfaceControl);
        EXPECT_EQ(stats.transactionStats[0].surfaceStats[1].surfaceControl, surfaceControl2);
    });

    mInvoker.sendCallbacks(false);
    flushCallbacks();
}

TEST_F(TransactionCallbackInvokerTest, sendCallbacks_onCommitOnlyFiltering) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();

    std::vector<CallbackId> completeIds = {{1, CallbackId::Type::ON_COMPLETE}};
    std::vector<CallbackId> commitIds = {{2, CallbackId::Type::ON_COMMIT}};

    mInvoker.addCallbackHandle({listener, completeIds, mSurfaceControl});
    mInvoker.addCallbackHandle({listener, commitIds, mSurfaceControl});

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](const ListenerStats& stats) {
        ASSERT_EQ(stats.transactionStats.size(), 1u);
        EXPECT_EQ(stats.transactionStats[0].callbackIds[0], commitIds[0]);
    });
    mInvoker.sendCallbacks(true /*onCommitOnly*/);
    flushCallbacks();

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](const ListenerStats& stats) {
        ASSERT_EQ(stats.transactionStats.size(), 1u);
        EXPECT_EQ(stats.transactionStats[0].callbackIds[0], completeIds[0]);
    });
    mInvoker.sendCallbacks(false /*onCommitOnly*/);
    flushCallbacks();
}

TEST_F(TransactionCallbackInvokerTest, sendCallbacks_emptyTransaction) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();
    std::vector<CallbackId> callbackIds = {{1, CallbackId::Type::ON_COMPLETE}};
    ListenerCallbacks listenerCallbacks{listener, callbackIds};

    mInvoker.addEmptyTransaction(listenerCallbacks);

    EXPECT_CALL(*listener, onTransactionCompleted(_)).WillOnce([&](ListenerStats stats) {
        ASSERT_EQ(stats.transactionStats.size(), 1u);
        EXPECT_EQ(stats.transactionStats[0].callbackIds[0], callbackIds[0]);
        EXPECT_TRUE(stats.transactionStats[0].surfaceStats.empty());
    });

    mInvoker.sendCallbacks(false);
    flushCallbacks();
}

TEST_F(TransactionCallbackInvokerTest, clearCompletedTransactions) {
    sp<MockTransactionCompletedListener> listener = sp<MockTransactionCompletedListener>::make();
    mInvoker.addCallbackHandle({listener, {{1, CallbackId::Type::ON_COMPLETE}}, mSurfaceControl});

    mInvoker.clearCompletedTransactions();

    EXPECT_CALL(*listener, onTransactionCompleted(_)).Times(0);
    mInvoker.sendCallbacks(false);
    flushCallbacks();
}

} // namespace android
