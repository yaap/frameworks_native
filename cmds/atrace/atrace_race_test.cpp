/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <cutils/properties.h>
#include <cutils/trace.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <android-base/macros.h>
#include <android-base/stringprintf.h>

#include "atrace.h"

class AtraceCleanupRaceTest : public ::testing::Test {
protected:
    const char* k_traceAppsNumberProperty = "debug.atrace.app_number";
    const char* k_traceApp0Property = "debug.atrace.app_0";
    const char* k_traceTagsProperty = "debug.atrace.tags.enableflags";

    std::string app_tag;

    std::atomic<bool> g_stop_reader{false};

    void SetUp() override {
        app_tag = android::base::StringPrintf("%#" PRIx64, (uint64_t)ATRACE_TAG_APP);
        ASSERT_EQ(0, property_set(k_traceAppsNumberProperty, "1"));
        ASSERT_EQ(0, property_set(k_traceApp0Property, "*"));
        ASSERT_EQ(0, property_set(k_traceTagsProperty, app_tag.c_str()));
        atrace_update_tags();
    }

    void TearDown() override {
        property_set(k_traceAppsNumberProperty, "");
        property_set(k_traceTagsProperty, "0");
        atrace_update_tags();
    }

public:
    void ReaderThread() {
        while (!g_stop_reader) {
            uint64_t prop_tags = property_get_int64(k_traceTagsProperty, 0);
            if (prop_tags == 0) {
                ASSERT_EQ(0, atrace_get_enabled_tags() & ATRACE_TAG_APP)
                        << "Inconsistency found during cleanup.";
            }
        }
    }
};

TEST_F(AtraceCleanupRaceTest, CleanupIsConsistent) {
    ASSERT_NE(0, atrace_get_enabled_tags() & ATRACE_TAG_APP)
            << "App tracing should be enabled at start.";

    auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < end_time) {
        // Restore properties to the enabled state for the test iteration.
        property_set(k_traceAppsNumberProperty, "1");
        property_set(k_traceTagsProperty, app_tag.c_str());

        g_stop_reader = false;
        std::thread reader(&AtraceCleanupRaceTest::ReaderThread, this);

        cleanUpUserspaceTracing();

        g_stop_reader = true;
        reader.join();
    }
}
