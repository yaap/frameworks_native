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

#pragma once

#include <ftl/small_vector.h>
#include <log/log.h>

#include <functional>

namespace android {

/**
 * RAII class intended to log thread-specific debug state when LOG_THREAD_STATE_AND_CRASH is called.
 *
 * Note: must NOT be deleted manually. Scope-based LIFO destruction is required to maintain
 * thread-local state.
 */
class ThreadStateCrashLogger {
public:
    using CrashHandler = std::function<void()>;

    /**
     * [handler] will be called if LOG_THREAD_STATE_AND_CRASH is called on this thread before this
     * ThreadStateCrashLogger is destroyed / falls out of scope. Handlers are called in FIFO order.
     */
    ThreadStateCrashLogger(CrashHandler handler) {
        gThreadLocalHandlers.push_back(std::move(handler));
    }
    ~ThreadStateCrashLogger() {
        // Assumes scope-based LIFO destruction.
        gThreadLocalHandlers.pop_back();
    }

    ThreadStateCrashLogger(const ThreadStateCrashLogger&) = delete;
    ThreadStateCrashLogger& operator=(const ThreadStateCrashLogger&) = delete;

    ThreadStateCrashLogger(ThreadStateCrashLogger&&) = delete;
    ThreadStateCrashLogger& operator=(ThreadStateCrashLogger&&) = delete;

    static void internalNotifyReceivers_doNotCall() {
        for (const auto& handler : gThreadLocalHandlers) {
            handler();
        }
    }

private:
    inline static thread_local ftl::SmallVector<CrashHandler, 5> gThreadLocalHandlers;
};

/**
 * Calls the logging handlers for any ThreadStateCrashLogger instances on the current thread (FIFO),
 * then calls LOG_ALWAYS_FATAL with the given message.
 */
#define LOG_THREAD_STATE_AND_CRASH(...)                                         \
    do {                                                                        \
        ::android::ThreadStateCrashLogger::internalNotifyReceivers_doNotCall(); \
        LOG_ALWAYS_FATAL(__VA_ARGS__);                                          \
    } while (false)

/**
 * If the given condition evaluates to true: calls the logging handlers for any
 * ThreadStateCrashLogger instances on the current thread (FIFO), then calls LOG_ALWAYS_FATAL with
 * the given message.
 */
#define LOG_THREAD_STATE_AND_CRASH_IF(cond, ...)                                    \
    do {                                                                            \
        if (__predict_false(cond)) {                                                \
            ::android::ThreadStateCrashLogger::internalNotifyReceivers_doNotCall(); \
            LOG_ALWAYS_FATAL(__VA_ARGS__);                                          \
        }                                                                           \
    } while (false)

} // namespace android
