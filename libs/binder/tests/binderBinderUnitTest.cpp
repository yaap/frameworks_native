/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <binder/Binder.h>
#include <binder/IInterface.h>
#include <gtest/gtest.h>

using android::BBinder;
using android::IBinder;
using android::OK;
using android::sp;
using android::TransactionCodeData;

const void* kObjectId1 = reinterpret_cast<const void*>(1);
const void* kObjectId2 = reinterpret_cast<const void*>(2);
void* kObject1 = reinterpret_cast<void*>(101);
void* kObject2 = reinterpret_cast<void*>(102);
void* kObject3 = reinterpret_cast<void*>(103);

TEST(Binder, AttachObject) {
    auto binder = sp<BBinder>::make();
    EXPECT_EQ(nullptr, binder->attachObject(kObjectId1, kObject1, nullptr, nullptr));
    EXPECT_EQ(nullptr, binder->attachObject(kObjectId2, kObject2, nullptr, nullptr));
    EXPECT_EQ(kObject1, binder->attachObject(kObjectId1, kObject3, nullptr, nullptr));
}

TEST(Binder, DetachObject) {
    auto binder = sp<BBinder>::make();
    EXPECT_EQ(nullptr, binder->attachObject(kObjectId1, kObject1, nullptr, nullptr));
    EXPECT_EQ(kObject1, binder->detachObject(kObjectId1));
    EXPECT_EQ(nullptr, binder->attachObject(kObjectId1, kObject2, nullptr, nullptr));
}

TEST(Binder, AttachExtension) {
    auto binder = sp<BBinder>::make();
    auto ext = sp<BBinder>::make();
    binder->setExtension(ext);
    EXPECT_EQ(ext, binder->getExtension());
}

struct MyCookie {
    bool* deleted;
};

class UniqueBinder : public BBinder {
public:
    UniqueBinder(const void* c) : cookie(reinterpret_cast<const MyCookie*>(c)) {
        *cookie->deleted = false;
    }
    ~UniqueBinder() { *cookie->deleted = true; }
    const MyCookie* cookie;
};

static sp<IBinder> make(const void* arg) {
    return sp<UniqueBinder>::make(arg);
}

TEST(Binder, LookupOrCreateWeak) {
    auto binder = sp<BBinder>::make();
    bool deleted;
    MyCookie cookie = {&deleted};
    sp<IBinder> createdBinder = binder->lookupOrCreateWeak(kObjectId1, make, &cookie);
    EXPECT_NE(binder, createdBinder);

    sp<IBinder> lookedUpBinder = binder->lookupOrCreateWeak(kObjectId1, make, &cookie);
    EXPECT_EQ(createdBinder, lookedUpBinder);
    EXPECT_FALSE(deleted);
}

TEST(Binder, LookupOrCreateWeakDropSp) {
    auto binder = sp<BBinder>::make();
    bool deleted1 = false;
    bool deleted2 = false;
    MyCookie cookie1 = {&deleted1};
    MyCookie cookie2 = {&deleted2};
    sp<IBinder> createdBinder = binder->lookupOrCreateWeak(kObjectId1, make, &cookie1);
    EXPECT_NE(binder, createdBinder);

    createdBinder.clear();
    EXPECT_TRUE(deleted1);

    sp<IBinder> lookedUpBinder = binder->lookupOrCreateWeak(kObjectId1, make, &cookie2);
    EXPECT_EQ(&cookie2, sp<UniqueBinder>::cast(lookedUpBinder)->cookie);
    EXPECT_FALSE(deleted2);
}

TEST(Binder, GetFunctionName) {
    auto binder = sp<BBinder>::make();

    const char* const kFunctionNames[] = {"foo", "bar"};
    alignas(16) TransactionCodeData data;
    data.names = kFunctionNames;
    data.count = 2;
    binder->setTransactionCodeMap(&data);

    EXPECT_EQ("foo", binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION));
    EXPECT_EQ("bar", binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION + 1));
}

TEST(Binder, GetFunctionNameNotFound) {
    auto binder = sp<BBinder>::make();
    std::string expected = "#" + std::to_string(IBinder::FIRST_CALL_TRANSACTION);
    EXPECT_EQ(expected, binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION));
}

TEST(Binder, GetFunctionNameInvalidCode) {
    auto binder = sp<BBinder>::make();
    const char* const kFunctionNames[] = {"foo", "bar"};
    alignas(16) TransactionCodeData data;
    data.names = kFunctionNames;
    data.count = 2;
    binder->setTransactionCodeMap(&data);

    std::string expectedBefore = "#" + std::to_string(IBinder::FIRST_CALL_TRANSACTION - 1);
    EXPECT_EQ(expectedBefore, binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION - 1));
    std::string expectedAfter = "#" + std::to_string(IBinder::FIRST_CALL_TRANSACTION + 2);
    EXPECT_EQ(expectedAfter, binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION + 2));
    std::string expectedZero = "#0";
    EXPECT_EQ(expectedZero, binder->getFunctionName(0));
    std::string expectedNegative = "#" + std::to_string(static_cast<size_t>(-1));
    EXPECT_EQ(expectedNegative, binder->getFunctionName(-1));
}

TEST(Binder, GetFunctionNameNullMap) {
    auto binder = sp<BBinder>::make();
    EXPECT_DEATH(binder->setTransactionCodeMap(nullptr), "TransactionCodeData pointer is null!");
}

TEST(Binder, SetTransactionCodeMapTwice) {
    auto binder = sp<BBinder>::make();

    const char* const kFunctionNames1[] = {"foo", "bar"};
    alignas(16) TransactionCodeData data1;
    data1.names = kFunctionNames1;
    data1.count = 2;
    binder->setTransactionCodeMap(&data1);

    const char* const kFunctionNames2[] = {"baz", "qux"};
    alignas(16) TransactionCodeData data2;
    data2.names = kFunctionNames2;
    data2.count = 2;
    EXPECT_DEATH(binder->setTransactionCodeMap(&data2), "TransactionCodeData already set!");
}

TEST(Binder, GetFunctionNameMalformed) {
    auto binder = sp<BBinder>::make();
    alignas(16) TransactionCodeData malformedData;
    malformedData.names = nullptr;
    malformedData.count = 2;
    binder->setTransactionCodeMap(&malformedData);
    std::string expected = "#" + std::to_string(IBinder::FIRST_CALL_TRANSACTION);
    EXPECT_EQ(expected, binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION));
}

class BinderTest : public ::testing::Test {
public:
    void setStability(const sp<BBinder>& binder, int16_t level) {
        binder->getPrivateAccessor().setStability(level);
    }

    int16_t getStability(const sp<BBinder>& binder) {
        return binder->getPrivateAccessor().getStability();
    }
};

TEST_F(BinderTest, PackedDataFieldsInitialState) {
    auto binder = sp<BBinder>::make();
    EXPECT_EQ(0, getStability(binder)); // Corresponds to STABILITY_UNDECLARED
    EXPECT_FALSE(binder->wasParceled());
}

TEST_F(BinderTest, PackedDataFieldsStability) {
    auto binder = sp<BBinder>::make();
    setStability(binder, 3); // Corresponds to STABILITY_VENDOR
    EXPECT_EQ(3, getStability(binder));
    setStability(binder, 12); // Corresponds to STABILITY_SYSTEM
    EXPECT_EQ(12, getStability(binder));
    setStability(binder, 0);
    EXPECT_EQ(0, getStability(binder));
    setStability(binder, -1);
    EXPECT_EQ(0, getStability(binder));
}

TEST_F(BinderTest, PackedDataFieldsParceled) {
    auto binder = sp<BBinder>::make();
    binder->setParceled();
    EXPECT_TRUE(binder->wasParceled());
}

TEST_F(BinderTest, PackedDataFieldsAllAtOnce) {
    auto binder = sp<BBinder>::make();

    // Set all packed fields
    setStability(binder, 12); // Corresponds to STABILITY_SYSTEM
    binder->setParceled();
    const char* const kFunctionNames[] = {"foo"};
    alignas(16) TransactionCodeData data;
    data.names = kFunctionNames;
    data.count = 1;
    binder->setTransactionCodeMap(&data);

    // Verify all fields
    EXPECT_EQ(12, getStability(binder));
    EXPECT_TRUE(binder->wasParceled());
    EXPECT_EQ("foo", binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION));

    // Change stability and verify others are unaffected
    setStability(binder, 3); // Corresponds to STABILITY_VENDOR
    EXPECT_EQ(3, getStability(binder));
    EXPECT_TRUE(binder->wasParceled());
    EXPECT_EQ("foo", binder->getFunctionName(IBinder::FIRST_CALL_TRANSACTION));
}
