/*
 * Copyright 2026 The Android Open Source Project
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

#include <procpartition/procpartition.h>
#include <stdlib.h>
#include <string>

#include <android-base/stringprintf.h>
#include <android-base/logging.h>
#include <binder/IServiceManager.h>
#include <gtest/gtest.h>

namespace android {
namespace procpartition {

pid_t getPidByName(std::string& name) {
    pid_t pid = -1;
    auto sm = defaultServiceManager();
    auto rpcBinder = sm->checkService(String16(name.c_str()));
    if (!rpcBinder) {
        return pid;
    }
    rpcBinder->getDebugPid(&pid);
    return pid;
}

TEST(ProcPartition, testExe) {
    std::string name = "SurfaceFlinger";
    pid_t pid = getPidByName(name);
    Partition part = procpartition::getPartition(pid);
    ASSERT_EQ(part, Partition::SYSTEM);
}

TEST(ProcPartition, testCmdline) {
    std::string name = "manager";
    pid_t pid = getPidByName(name);
    Partition part = procpartition::getPartition(pid);
    ASSERT_EQ(part, Partition::SYSTEM);
}

TEST(ProcPartition, testApex) {
   std::string name = "stats";
   pid_t pid = getPidByName(name);
   // We know this is an APEX and will hit the parseApex path
   Partition part = procpartition::getPartition(pid);
   ASSERT_EQ(part, Partition::SYSTEM);
}

TEST(ProcPartition, testApk) {
    // com.android.internal.telephone.ITelephony
    std::string name = "phone";
    pid_t pid = getPidByName(name);
    Partition part = procpartition::getPartition(pid);
    ASSERT_EQ(part, Partition::SYSTEM);
}

TEST(ProcPartition, invalidTarget) {
    Partition part = procpartition::getPartition(static_cast<pid_t>(-1));
    ASSERT_EQ(part, Partition::UNKNOWN);
}

} // procpartition
} // android

