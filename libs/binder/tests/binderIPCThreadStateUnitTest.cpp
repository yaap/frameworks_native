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

#include <binder/Binder.h>
#include <binder/IInterface.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <fakeservicemanager/FakeServiceManager.h>
#include <gtest/gtest.h>
#include <private/android_filesystem_config.h>

// This test has to be in a separate file because setDefaultServiceManager is global, and we can't
// call setDefaultServiceManager() twice. The tests with the service present are in binderLibTest's
// PccAuditTestLogPccTransaction.

TEST(BinderIPCThreadStateTest, PccAuditTestLogPccTransactionNoService) {
    android::sp<android::ProcessState> ps = android::ProcessState::self();
    uid_t nonPccUid = 10000;

    // No service, callingUid not PCC -> returns false
    android::sp<android::FakeServiceManager> fakeSM =
            android::sp<android::FakeServiceManager>::make();
    android::setDefaultServiceManager(fakeSM);
    android::sp<android::BBinder> binder = android::sp<android::BBinder>::make();
    fakeSM->addService(android::String16("pcc_sandbox_native"), nullptr);
    EXPECT_FALSE(android::IPCThreadState::logPccTransaction(binder.get(), 0, nonPccUid));
}
