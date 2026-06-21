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
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <binderdebug/BinderDebug.h>
#include <binderdebug/FileReader.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <semaphore.h>
#include <thread>
#include <algorithm>

#include <android/binderdebug/test/BnControl.h>
#include <android/binderdebug/test/IControl.h>

namespace android {
namespace binderdebug {
namespace test {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;

class Control : public BnControl {
public:
    Control() {sem_init(&s, 1, 0);};
    ::android::binder::Status Continue() override;
    sem_t s;
};

::android::binder::Status Control::Continue() {
    IPCThreadState::self()->flushCommands();
    sem_post(&s);
    return binder::Status::ok();
}

TEST(BinderDebugTests, BinderPid) {
    BinderPidInfo pidInfo;
    const auto& status = getBinderPidInfo(BinderDebugContext::BINDER, getpid(), &pidInfo);
    ASSERT_EQ(status, OK);
    // There should be one referenced PID for servicemanager
    EXPECT_TRUE(!pidInfo.refPids.empty());
}

TEST(BinderDebugTests, BinderThreads) {
    BinderPidInfo pidInfo;
    const auto& status = getBinderPidInfo(BinderDebugContext::BINDER, getpid(), &pidInfo);
    ASSERT_EQ(status, OK);
    EXPECT_TRUE(pidInfo.threadUsage <= pidInfo.threadCount);
    // The second looper thread can sometimes take longer to spawn.
    EXPECT_GE(pidInfo.threadCount, 1);
}

TEST(BinderDebugTests, ServiceManagerBinderIsVisible) {
    sp<IBinder> binder = defaultServiceManager()->checkService(String16("activity"));
    ASSERT_NE(binder, nullptr);
    pid_t pid;
    status_t status = binder->getDebugPid(&pid);
    ASSERT_EQ(status, OK);
    BinderPidInfo pidInfo;
    status = getBinderPidInfo(BinderDebugContext::BINDER, pid, &pidInfo);
    ASSERT_EQ(status, OK);
    EXPECT_GT(pidInfo.threadCount, 0);
    EXPECT_GE(pidInfo.threadUsage, 0);

    bool found = std::any_of(pidInfo.refPids.begin(), pidInfo.refPids.end(),
                             [&](const auto& pair) {
                                 const auto& pids = pair.second;
                                 return std::find(pids.begin(), pids.end(), getpid()) != pids.end();
                             });
    EXPECT_TRUE(found);
}

class MockFileReader : public FileReader {
public:
    MockFileReader() = default;

    bool Open(const std::string& path) override {
        auto it = file_contents_.find(path);
        if (it == file_contents_.end()) {
            return false;
        }
        current_stream_.str(it->second);
        current_stream_.clear();
        current_stream_.seekg(0);
        return true;
    }

    bool IsOpen() const override { return !current_stream_.str().empty(); }

    bool GetLine(std::string& line) override {
        return static_cast<bool>(std::getline(current_stream_, line));
    }

    void Close() override {
        // We don't clear the stream here to allow multiple reads in the same test.
    }

    void SetFileContent(const std::string& path, const std::string& content) {
        file_contents_[path] = content;
    }

private:
    std::map<std::string, std::string> file_contents_;
    std::stringstream current_stream_;
};

TEST(BinderDebugParsingTest, GetBinderPidInfo_CBinder) {
    const pid_t pid = 1234;

    auto fileReader = std::make_unique<MockFileReader>();
    const std::string proc_content =
            "proc 1234\n"
            "context binder\n"
            "  thread 1235: l 00 need_return 0 tr 0\n" // isInUse = true,  isBinderThread = false
            "  thread 1236: l 11 need_return 1 tr 0\n" // isInUse = false, isBinderThread = true
            "  thread 1237: l 10 need_return 0 tr 0\n" // isInUse = false, isBinderThread = false
            "  thread 1238: l 12 need_return 0 tr 0\n" // isInUse = false, isBinderThread = true
            "  thread 1239: l 01 need_return 0 tr 0\n" // isInUse = true,  isBinderThread = true
            "  node 1: u0000000012345678 c0000000087654321 pri 0:120 hs 1 hw 1 ls 0 lw 0 is 2 iw 2 "
            "tr 1 proc 5678 9012\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/1234", proc_content);

    BinderPidInfo pidInfo;
    status_t status =
            getBinderPidInfo(BinderDebugContext::BINDER, pid, std::move(fileReader), &pidInfo);

    EXPECT_EQ(OK, status);
    EXPECT_EQ(3, pidInfo.threadCount);
    EXPECT_EQ(1, pidInfo.threadUsage);
    ASSERT_EQ(1, pidInfo.refPids.count(0x12345678));
    EXPECT_EQ(2, pidInfo.refPids[0x12345678].size());
    EXPECT_EQ(5678, pidInfo.refPids[0x12345678][0]);
    EXPECT_EQ(9012, pidInfo.refPids[0x12345678][1]);
}

TEST(BinderDebugParsingTest, GetBinderPidInfo_RustBinder) {
    const pid_t pid = 1234;

    auto fileReader = std::make_unique<MockFileReader>();
    const std::string proc_content = "proc 1234\n"
                                     "context binder\n"
                                     "  thread 1235: l 00 need_return false\n"
                                     "  thread 1236: l 11 need_return true\n"
                                     "  thread 1237: l 10 need_return false\n"
                                     "  thread 1238: l 12 need_return false\n"
                                     "  thread 1239: l 01 need_return false\n"
                                     "  node 1: u0000000012345678 c0000000087654321 pri 0:120 hs "
                                     "true hw true cs 1 cw 1 proc 5678 9012\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/1234", proc_content);

    BinderPidInfo pidInfo;
    status_t status =
            getBinderPidInfo(BinderDebugContext::BINDER, pid, std::move(fileReader), &pidInfo);

    EXPECT_EQ(OK, status);
    EXPECT_EQ(3, pidInfo.threadCount);
    EXPECT_EQ(1, pidInfo.threadUsage);
    ASSERT_EQ(1, pidInfo.refPids.count(0x12345678));
    EXPECT_EQ(2, pidInfo.refPids[0x12345678].size());
    EXPECT_EQ(5678, pidInfo.refPids[0x12345678][0]);
    EXPECT_EQ(9012, pidInfo.refPids[0x12345678][1]);
}

TEST(BinderDebugParsingTest, GetBinderClientPids_CBinder) {
    const pid_t clientPid = 1234;
    const pid_t servicePid = 5678;
    const int32_t handle = 912;

    auto fileReader = std::make_unique<MockFileReader>();
    const std::string proc_content_1234 =
            "proc 1234\n"
            "context binder\n"
            "  ref 52493: desc 910 node 52492 s 1 w 1 d 0000000000000000\n"
            "  ref 52495: desc 911 dead node 52494 s 1 w 1 d 0000000000000000\n"
            "  ref 52497: desc 912 node 52496 s 1 w 1 d 0000000000000000\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/1234", proc_content_1234);
    const std::string proc_content_5678 =
            "proc 5678\n"
            "context binder\n"
            "  node 52492: u0000000000000123 c0000000000000456 pri 0:120 hs 1 hw 1 ls 1 lw 0 is 0 "
            "iw 2 tr 1 proc 111 222\n"
            "  node 52496: u0000000000000123 c0000000000000456 pri 0:120 hs 1 hw 1 ls 0 lw 0 is 4 "
            "iw 4 tr 1 proc 333 444 555 666\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/5678", proc_content_5678);

    std::vector<pid_t> pids;
    status_t status = getBinderClientPids(BinderDebugContext::BINDER, clientPid, servicePid, handle,
                                          std::move(fileReader), &pids);

    ASSERT_EQ(OK, status);
    ASSERT_EQ(4, pids.size());
    EXPECT_EQ(333, pids[0]);
    EXPECT_EQ(444, pids[1]);
    EXPECT_EQ(555, pids[2]);
    EXPECT_EQ(666, pids[3]);
}

TEST(BinderDebugParsingTest, GetBinderClientPids_RustBinder) {
    const pid_t clientPid = 1234;
    const pid_t servicePid = 5678;
    const int32_t handle = 912;

    auto fileReader = std::make_unique<MockFileReader>();
    const std::string proc_content_1234 = "proc 1234\n"
                                          "context binder\n"
                                          "  ref 52493: desc 910 node 52492 s 1 w 1\n"
                                          "  ref 52495: desc 911 dead node 52494 s 1 w 1\n"
                                          "  ref 52497: desc 912 node 52496 s 1 w 1\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/1234", proc_content_1234);
    const std::string proc_content_5678 = "proc 5678\n"
                                          "context binder\n"
                                          "  node 52492: u0000000000000123 c0000000000000456 pri "
                                          "0:120 hs true hw true cs 2 cw 2 proc 111 222\n"
                                          "  node 52496: u0000000000000123 c0000000000000456 pri "
                                          "0:120 hs true hw true cs 4 cw 4 proc 333 444 555 666\n";
    fileReader->SetFileContent("/dev/binderfs/binder_logs/proc/5678", proc_content_5678);

    std::vector<pid_t> pids;
    status_t status = getBinderClientPids(BinderDebugContext::BINDER, clientPid, servicePid, handle,
                                          std::move(fileReader), &pids);

    ASSERT_EQ(OK, status);
    ASSERT_EQ(4, pids.size());
    EXPECT_EQ(333, pids[0]);
    EXPECT_EQ(444, pids[1]);
    EXPECT_EQ(555, pids[2]);
    EXPECT_EQ(666, pids[3]);
}

TEST(BinderDebugParsingTest, GetBinderTransactions) {
    const pid_t pid = 1234;

    auto fileReader = std::make_unique<MockFileReader>();
    const std::string transactions_content =
            "proc 3456\n"
            "context binder\n"
            "  node 1: u0000000012345678 c0000000087654321 pri 0:120 hs true hw true cs 1 cw 1 "
            "proc 5678 9012\n"
            "proc 1234\n"
            "context binder\n"
            "  pending transaction 123 from ...\n"
            "  more data for 1234\n"
            "proc 6543\n"
            "context binder\n"
            "  node work 1111: u0000000012345678 c0000000087654321\n";

    fileReader->SetFileContent("/dev/binderfs/binder_logs/transactions", transactions_content);

    std::string output;
    status_t status = getBinderTransactions(pid, std::move(fileReader), output);
    ASSERT_EQ(OK, status);
    EXPECT_EQ("proc 1234\ncontext binder\n  pending transaction 123 from ...\n  more data for "
              "1234\n",
              output);
}

} // namespace  test
} // namespace  binderdebug
} // namespace  android

int main(int argc, char** argv) {
    using namespace android;
    using namespace android::binderdebug;
    using namespace android::binderdebug::test;

    ::testing::InitGoogleTest(&argc, argv);

    // Create a child/client process to call into the main process so we can ensure
    // looper thread has been registered before attempting to get the BinderPidInfo
    pid_t pid = fork();
    if (pid == 0) {
        sp<IBinder> binder = android::defaultServiceManager()->getService(String16("binderdebug"));
        sp<IControl> service;
        if (binder != nullptr) {
            service = android::interface_cast<IControl>(binder);
        }
        service->Continue();
        exit(0);
    }
    sp<Control> iface = new Control;
    android::defaultServiceManager()->addService(String16("binderdebug"), iface);
    android::ProcessState::self()->setThreadPoolMaxThreadCount(8);
    ProcessState::self()->startThreadPool();
    sem_wait(&iface->s);

    return RUN_ALL_TESTS();
}
