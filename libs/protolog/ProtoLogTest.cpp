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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ProtoLog.h"

// Perfetto headers for controlling tracing in the test
#include <perfetto/trace_processor/trace_processor.h>
#include <perfetto/tracing.h>

// Protobuf headers for parsing the trace output
#include "protos/perfetto/trace/android/protolog.pb.h"
#include "protos/perfetto/trace/interned_data/interned_data.pb.h"
#include "protos/perfetto/trace/trace.pb.h"
#include "protos/perfetto/trace/trace_packet.pb.h"

#include <android_tracing.h>
#include <flag_macros.h>
#include <log/log.h>
#include <thread>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define FLAG_PACKAGE android::tracing

namespace android {
namespace protolog {

const std::string DATASOURCE_NAME = "android.protolog";

class ProtoLogTest : public ::testing::Test {
protected:
    void SetUp() override { android::protolog::Initialize(perfetto::kInProcessBackend); }

    // void TearDown() override { android::protolog::Destroy(); }

    // Helper to start a tracing session configured for the ProtoLog data source.
    std::unique_ptr<perfetto::TracingSession> StartTracing(
            perfetto::protos::ProtoLogConfig_TracingMode tracing_mode =
                    perfetto::protos::ProtoLogConfig_TracingMode_ENABLE_ALL,
            perfetto::protos::ProtoLogLevel min_level =
                    perfetto::protos::ProtoLogLevel::PROTOLOG_LEVEL_INFO) {
        perfetto::TraceConfig cfg;
        cfg.add_buffers()->set_size_kb(1024);
        auto* ds_cfg = cfg.add_data_sources()->mutable_config();
        ds_cfg->set_name(DATASOURCE_NAME);

        perfetto::protos::ProtoLogConfig protolog_cfg;
        protolog_cfg.set_tracing_mode(tracing_mode);
        protolog_cfg.set_default_log_from_level(min_level);
        ds_cfg->set_protolog_config_raw(protolog_cfg.SerializeAsString());

        auto tracing_session = perfetto::Tracing::NewTrace();
        tracing_session->Setup(cfg);
        tracing_session->StartBlocking();
        return tracing_session;
    }

    // Helper to stop tracing and read the captured data.
    std::string StopAndReadTrace(perfetto::TracingSession* session) {
        session->StopBlocking();
        std::vector<char> trace_data = session->ReadTraceBlocking();
        return std::string(trace_data.begin(), trace_data.end());
    }

    // Helper to create a TraceProcessor instance from a raw trace string.
    std::unique_ptr<perfetto::trace_processor::TraceProcessor> GetTraceProcessor(
            const std::string& trace_proto) {
        const char* file_path = "/data/local/tmp/trace_output.pb";
        std::ofstream output_stream(file_path, std::ios::out | std::ios::binary);
        if (output_stream.is_open()) {
            output_stream.write(trace_proto.data(), trace_proto.size());
            output_stream.close();
        } else {
            ADD_FAILURE() << "Failed to open file for writing";
            return nullptr;
        }

        perfetto::trace_processor::Config config;
        auto tp = perfetto::trace_processor::TraceProcessor::CreateInstance(config);
        std::unique_ptr<uint8_t[]> buf(new uint8_t[trace_proto.size()]);
        memcpy(buf.get(), trace_proto.data(), trace_proto.size());
        auto status = tp->Parse(std::move(buf), trace_proto.size());
        if (!status.ok()) {
            PERFETTO_DFATAL_OR_ELOG("Failed to parse: %s", status.message().c_str());
        }
        tp->NotifyEndOfFile();
        return tp;
    }

    std::pair<int, int> analyzeConcurrentTrace(const std::string& trace, int start_counter,
                                               int end_counter) {
        auto tp = GetTraceProcessor(trace);
        auto it = tp->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'StressGroup' AND "
                                   "message LIKE 'Message %' ORDER BY ts");

        std::vector<std::string> messages;
        while (it.Next()) {
            messages.push_back(it.Get(0).AsString());
        }

        EXPECT_FALSE(messages.empty());

        int first_msg_num;
        sscanf(messages.begin()->c_str(), "Message %d", &first_msg_num);

        int expected_msg_num = first_msg_num;
        for (auto mIt = messages.begin(); mIt != messages.end(); ++mIt) {
            int current_msg_num;
            if (sscanf(mIt->c_str(), "Message %d", &current_msg_num) == 1) {
                EXPECT_EQ(current_msg_num, expected_msg_num) << "Data messages are not consecutive";
                expected_msg_num++;
            }
        }

        int last_msg_num;
        sscanf(messages.back().c_str(), "Message %d", &last_msg_num);

        // Starting and stopping the trace is an async call and we have no guarantees of when it
        // actually starts and stops tracing, so the best we can do is assert there are no messages
        // from before and after we make the call to start and stop tracing respectively.
        EXPECT_GE(first_msg_num, start_counter);
        EXPECT_LE(last_msg_num, end_counter);

        return {first_msg_num, last_msg_num};
    }
};

std::string GetLine(const std::string& input, int line_number) {
    std::istringstream stream(input);
    std::string line;
    for (int i = 0; i <= line_number; i++) {
        std::getline(stream, line);
    }
    return line;
}

// Verifies that logging before, during, and after a trace session does not crash.
TEST_F_WITH_FLAGS(ProtoLogTest, LogsWithoutCrashing,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    // Log before a session is active. This message should be dropped.
    PROTOLOG_I("TestGroup", "Message before tracing starts");

    auto session = StartTracing();
    PROTOLOG_I("TestGroup", "A simple message with no params");
    PROTOLOG_E("TestGroup", "Message with params: %d, %s", 123, "test");
    std::string trace_str = StopAndReadTrace(session.get());

    // Log after a session has stopped. This message should also be dropped.
    PROTOLOG_I("TestGroup", "Message after tracing stops");

    auto tp = GetTraceProcessor(trace_str);
    auto it = tp->ExecuteQuery("SELECT count(*) FROM protolog WHERE tag = 'TestGroup'");
    ASSERT_TRUE(it.Next());
    EXPECT_EQ(it.Get(0).AsLong(), 2);
}

// Verifies that no logs are produced when the flag is disabled.
TEST_F_WITH_FLAGS(ProtoLogTest, NoOutputWhenFlagDisabled,
                  REQUIRES_FLAGS_DISABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();
    PROTOLOG_E("NoTraceGroup", "This message should not be logged");
    std::string trace_str = StopAndReadTrace(session.get());

    auto tp = GetTraceProcessor(trace_str);
    auto it = tp->ExecuteQuery("SELECT count(*) FROM protolog");
    ASSERT_TRUE(it.Next());
    EXPECT_EQ(it.Get(0).AsLong(), 0);
    ASSERT_FALSE(it.Next());
}

// Verifies that messages logged before a tracing session starts are not included in the trace.
TEST_F_WITH_FLAGS(ProtoLogTest, NoOutputWhenTracingIsOff,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    // Log a message with a unique string *before* starting the trace.
    PROTOLOG_E("NoTraceGroup", "This message has a unique string: %s", "unwanted");

    auto session = StartTracing();
    // Log a different message *during* the trace.
    PROTOLOG_I("TraceGroup", "This message is wanted: %s", "wanted");
    std::string trace_str = StopAndReadTrace(session.get());

    auto tp = GetTraceProcessor(trace_str);
    auto it = tp->ExecuteQuery("SELECT message FROM protolog");

    ASSERT_TRUE(it.Next());
    EXPECT_STREQ(it.Get(0).AsString(), "This message is wanted: wanted");
    ASSERT_FALSE(it.Next()) << "More than one message found in the trace.";
}

// Verifies that various parameter types are correctly serialized into the trace protobuf.
TEST_F_WITH_FLAGS(ProtoLogTest, LogsParametersCorrectly,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    const char* str_param = "test_string";
    double double_param = 3.14;
    int negative_int_param = -42;
    int positive_int_param = 42;
    bool bool_param_true = true;
    bool bool_param_false = false;
    PROTOLOG_W("MultiGroup", "Params: %s, %f, %d, %d, %b, %b", str_param, double_param,
               negative_int_param, positive_int_param, bool_param_true, bool_param_false);

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    auto it = tp->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'MultiGroup'");
    ASSERT_TRUE(it.Next());
    // Note: trace processor formats floating point numbers with up to 6 decimal places.
    EXPECT_STREQ(it.Get(0).AsString(), "Params: test_string, 3.140000, -42, 42, true, false");
    ASSERT_FALSE(it.Next());
}

// Verifies that more than 16 arguments can be logged correctly.
TEST_F_WITH_FLAGS(ProtoLogTest, LogsMoreThan16ParametersCorrectly,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    PROTOLOG_W("MultiGroup",
               "Params: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
               "%d, %d",
               1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20);

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    auto it = tp->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'MultiGroup'");
    ASSERT_TRUE(it.Next());
    EXPECT_STREQ(it.Get(0).AsString(),
                 "Params: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20");
    ASSERT_FALSE(it.Next());
}

// Verifies that duplicate strings are only interned and written to the trace once.
TEST_F_WITH_FLAGS(ProtoLogTest, InterningIsCorrect,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    // Log the same message, group, and string parameter twice.
    PROTOLOG_I("InternGroup", "Intern message %s", "intern_string");
    PROTOLOG_I("InternGroup", "Intern message %s", "intern_string");

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    auto it = tp->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'InternGroup'");
    ASSERT_TRUE(it.Next());
    EXPECT_STREQ(it.Get(0).AsString(), "Intern message intern_string");
    ASSERT_TRUE(it.Next());
    EXPECT_STREQ(it.Get(0).AsString(), "Intern message intern_string");
    ASSERT_FALSE(it.Next());
}

// Verifies that string arguments are traced and decoded properly.
TEST_F_WITH_FLAGS(ProtoLogTest, StringArgumentInterningIsCorrect,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    PROTOLOG_I("StringInternGroup", "Logging a string: %s", "unique_intern_string");
    PROTOLOG_I("StringInternGroup", "Logging the same string again: %s", "unique_intern_string");
    PROTOLOG_I("StringInternGroup", "Logging a different string: %s", "another_string");

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    // Check that the messages are correct in the protolog table.
    auto it_msg = tp->ExecuteQuery(
            "SELECT message FROM protolog WHERE tag = 'StringInternGroup' ORDER BY ts");
    ASSERT_TRUE(it_msg.Next());
    EXPECT_STREQ(it_msg.Get(0).AsString(), "Logging a string: unique_intern_string");
    ASSERT_TRUE(it_msg.Next());
    EXPECT_STREQ(it_msg.Get(0).AsString(), "Logging the same string again: unique_intern_string");
    ASSERT_TRUE(it_msg.Next());
    EXPECT_STREQ(it_msg.Get(0).AsString(), "Logging a different string: another_string");
    ASSERT_FALSE(it_msg.Next());
}

// Verifies that the LRU cache for string arguments evicts old entries and
// doesn't grow beyond its limit.
TEST_F_WITH_FLAGS(ProtoLogTest, LruCacheEviction,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    constexpr int kCacheSize = 1024;
    constexpr int kNumStrings = kCacheSize + 100;
    std::vector<std::string> test_strings;
    test_strings.reserve(kNumStrings);
    for (int i = 0; i < kNumStrings; ++i) {
        test_strings.push_back("string_" + std::to_string(i));
    }

    // Log kNumStrings unique strings to fill and overflow the cache.
    for (int i = 0; i < kNumStrings; ++i) {
        PROTOLOG_I("LruGroup", "Message %s", test_strings[i].c_str());
    }

    // Log the first string again. It should have been evicted.
    PROTOLOG_I("LruGroup", "Message %s", test_strings[0].c_str());

    // Log the last string again. It should still be in the cache.
    PROTOLOG_I("LruGroup", "Message %s", test_strings[kNumStrings - 1].c_str());

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    // Verify all messages are present and correct.
    auto it_msg =
            tp->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'LruGroup' ORDER BY ts");
    for (int i = 0; i < kNumStrings; ++i) {
        ASSERT_TRUE(it_msg.Next());
        std::string expected_msg = "Message " + test_strings[i];
        EXPECT_STREQ(it_msg.Get(0).AsString(), expected_msg.c_str());
    }
    ASSERT_TRUE(it_msg.Next());
    EXPECT_STREQ(it_msg.Get(0).AsString(), ("Message " + test_strings[0]).c_str());
    ASSERT_TRUE(it_msg.Next());
    EXPECT_STREQ(it_msg.Get(0).AsString(), ("Message " + test_strings[kNumStrings - 1]).c_str());
    ASSERT_FALSE(it_msg.Next());
}

// Verifies that multiple concurrent tracing sessions with different configs work correctly.
TEST_F_WITH_FLAGS(ProtoLogTest, ConcurrentSessions,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    perfetto::TraceConfig cfg1;
    cfg1.add_buffers()->set_size_kb(1024);

    perfetto::TraceConfig cfg2;
    cfg2.add_buffers()->set_size_kb(1024);

    // Session 1: Log INFO and above for "InfoGroup"
    auto* ds_cfg1 = cfg1.add_data_sources()->mutable_config();
    ds_cfg1->set_name(DATASOURCE_NAME);
    perfetto::protos::ProtoLogConfig protolog_cfg1;
    auto* group1_override = protolog_cfg1.add_group_overrides();
    group1_override->set_group_name("InfoGroup");
    group1_override->set_log_from(perfetto::protos::PROTOLOG_LEVEL_INFO);
    ds_cfg1->set_protolog_config_raw(protolog_cfg1.SerializeAsString());

    // Session 2: Log ERROR and above for "ErrorGroup"
    auto* ds_cfg2 = cfg2.add_data_sources()->mutable_config();
    ds_cfg2->set_name(DATASOURCE_NAME);
    perfetto::protos::ProtoLogConfig protolog_cfg2;
    auto* group2_override = protolog_cfg2.add_group_overrides();
    group2_override->set_group_name("ErrorGroup");
    group2_override->set_log_from(perfetto::protos::PROTOLOG_LEVEL_ERROR);
    ds_cfg2->set_protolog_config_raw(protolog_cfg2.SerializeAsString());

    auto tracing_session1 = perfetto::Tracing::NewTrace();
    tracing_session1->Setup(cfg1);
    tracing_session1->StartBlocking();

    auto tracing_session2 = perfetto::Tracing::NewTrace();
    tracing_session2->Setup(cfg2);
    tracing_session2->StartBlocking();

    PROTOLOG_D("DebugGroup", "Debug message, should be filtered");
    PROTOLOG_I("InfoGroup", "Info message, should be logged");
    PROTOLOG_W("WarningGroup", "Warning message, should be filtered");
    PROTOLOG_E("ErrorGroup", "Error message, should be logged");

    std::string trace_str1 = StopAndReadTrace(tracing_session1.get());
    auto tp1 = GetTraceProcessor(trace_str1);

    std::string trace_str2 = StopAndReadTrace(tracing_session2.get());
    auto tp2 = GetTraceProcessor(trace_str2);

    auto it_info = tp1->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'InfoGroup'");
    ASSERT_TRUE(it_info.Next());
    EXPECT_STREQ(it_info.Get(0).AsString(), "Info message, should be logged");
    ASSERT_FALSE(it_info.Next());

    auto it_count1 = tp1->ExecuteQuery("SELECT count(*) FROM protolog");
    ASSERT_TRUE(it_count1.Next());
    EXPECT_EQ(it_count1.Get(0).AsLong(), 1);

    auto it_error = tp2->ExecuteQuery("SELECT message FROM protolog WHERE tag = 'ErrorGroup'");
    ASSERT_TRUE(it_error.Next());
    EXPECT_STREQ(it_error.Get(0).AsString(), "Error message, should be logged");
    ASSERT_FALSE(it_error.Next());

    auto it_coun2 = tp2->ExecuteQuery("SELECT count(*) FROM protolog");
    ASSERT_TRUE(it_coun2.Next());
    EXPECT_EQ(it_coun2.Get(0).AsLong(), 1);
}

// Verifies that messages are filtered correctly based on the log level in the config.
TEST_F_WITH_FLAGS(ProtoLogTest, FiltersByLevel,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto count_messages = [this](perfetto::protos::ProtoLogLevel level) -> int {
        auto session = StartTracing(perfetto::protos::ProtoLogConfig_TracingMode_ENABLE_ALL, level);
        PROTOLOG_V("FilterGroup", "Verbose");
        PROTOLOG_D("FilterGroup", "Debug");
        PROTOLOG_I("FilterGroup", "Info");
        PROTOLOG_W("FilterGroup", "Warn");
        PROTOLOG_E("FilterGroup", "Error");
        PROTOLOG_WTF("FilterGroup", "Wtf");
        std::string trace_str = StopAndReadTrace(session.get());

        auto tp = GetTraceProcessor(trace_str);
        auto it = tp->ExecuteQuery("SELECT count(*) FROM protolog");
        if (!it.Next()) {
            return -1;
        }
        return static_cast<int>(it.Get(0).AsLong());
    };

    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_VERBOSE), 6);
    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_DEBUG), 5);
    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_INFO), 4);
    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_WARN), 3);
    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_ERROR), 2);
    EXPECT_EQ(count_messages(perfetto::protos::PROTOLOG_LEVEL_WTF), 1);
}

// Verifies that stack traces are captured when configured.
TEST_F_WITH_FLAGS(ProtoLogTest, LogsWithStacktrace,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    // TODO(b/477870887): Unsupported for now, so test was removed. Context: b/473868195.
}

// Verifies that logging from multiple threads simultaneously works correctly.
TEST_F_WITH_FLAGS(ProtoLogTest, MultiThreadedLogging,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    auto session = StartTracing();

    constexpr int kNumThreads = 10;
    constexpr int kNumMessagesPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < kNumMessagesPerThread; ++j) {
                PROTOLOG_I("MultiThreaded", "Message from thread %d, iteration %d", i, j);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::string trace_str = StopAndReadTrace(session.get());
    auto tp = GetTraceProcessor(trace_str);

    auto it = tp->ExecuteQuery("SELECT count(*) FROM protolog WHERE tag = 'MultiThreaded'");
    ASSERT_TRUE(it.Next());
    EXPECT_EQ(it.Get(0).AsLong(), kNumThreads * kNumMessagesPerThread);
    ASSERT_FALSE(it.Next());
}

// Verifies that concurrent sessions starting and stopping during continuous
// logging capture the correct messages. This test uses in-band markers to
// define the capture window, making it robust against timing-related flakiness.
TEST_F_WITH_FLAGS(ProtoLogTest, StressTestConcurrentSessions,
                  REQUIRES_FLAGS_ENABLED(ACONFIG_FLAG(FLAG_PACKAGE, native_proto_logging))) {
    std::atomic<bool> stop_logging(false);
    std::atomic<int> message_counter(0);

    std::thread logging_thread([&]() {
        while (!stop_logging.load(std::memory_order_relaxed)) {
            PROTOLOG_I("StressGroup", "Message %d",
                       message_counter.fetch_add(1, std::memory_order_relaxed));
            // Log at a high rate to increase the chance of hitting boundary conditions.
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Let some messages be logged before any session starts.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int start_counter1 = message_counter.load();
    auto session1 = StartTracing();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int start_counter2 = message_counter.load();
    auto session2 = StartTracing();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string trace1 = StopAndReadTrace(session1.get());
    int end_counter1 = message_counter.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string trace2 = StopAndReadTrace(session2.get());
    int end_counter2 = message_counter.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    stop_logging = true;
    logging_thread.join();

    // Analyze trace 1
    auto range1 = analyzeConcurrentTrace(trace1, start_counter1, end_counter1);

    // Analyze trace 2
    auto range2 = analyzeConcurrentTrace(trace2, start_counter2, end_counter2);

    // Session 2 should have started after session 1.
    EXPECT_LT(range1.first, range2.first);
    // Session 1 stopped before session 2.
    EXPECT_LT(range1.second, range2.second);
}

} // namespace protolog
} // namespace android