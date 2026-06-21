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

#ifndef ANDROID_INTERNAL_PROTOLOG_H
#define ANDROID_INTERNAL_PROTOLOG_H

#include <cstdarg>
#include <string>
#include <vector>
#include "ProtoLogTypes.h"

namespace android {
namespace protolog {

class IArgumentProvider {
public:
    virtual ~IArgumentProvider() = default;
    virtual void newPass() = 0;
    virtual void endPass() = 0;
    virtual long long nextInt() = 0;
    virtual double nextDouble() = 0;
    virtual const char* nextString() = 0;
    virtual void releaseString() = 0;
    virtual bool nextBool() = 0;
};

#ifdef __ANDROID__
/**
 * Initializes the C++ ProtoLog data source and registers it with Perfetto.
 */
void Initialize();

/**
 * Initializes the C++ ProtoLog data source with specific backends.
 */
void Initialize(uint32_t backends);

/**
 * The core logging function. Called by the macros.
 */
void Log(ProtoLogLevel level, const std::string_view group, const char* format, ...)
        __attribute__((format(printf, 3, 4)));
void Log(ProtoLogLevel level, const std::string_view group, const char* format,
         IArgumentProvider& provider);
void Log(ProtoLogLevel level, const std::string_view group, uint64_t messageHash,
         uint64_t paramsMask, int argCount, IArgumentProvider& provider);

#define PROTOLOG(level, group, format, ...) \
    android::protolog::Log(level, group, format, ##__VA_ARGS__)

#define PROTOLOG_V(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::VERBOSE, group, format, ##__VA_ARGS__)
#define PROTOLOG_D(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::DEBUG, group, format, ##__VA_ARGS__)
#define PROTOLOG_I(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::INFO, group, format, ##__VA_ARGS__)
#define PROTOLOG_W(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::WARN, group, format, ##__VA_ARGS__)
#define PROTOLOG_E(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::ERROR, group, format, ##__VA_ARGS__)
#define PROTOLOG_WTF(group, format, ...) \
    PROTOLOG(::android::protolog::ProtoLogLevel::WTF, group, format, ##__VA_ARGS__)

#else
// Stub out ProtoLog for host builds
inline void Initialize() {}
inline void Initialize(uint32_t backends) {}
inline void Destroy() {}
inline void Log(ProtoLogLevel level, const std::string_view group, const std::string_view format,
                ...) {}
inline void Log(ProtoLogLevel level, const std::string_view group, const char* format,
                IArgumentProvider& provider) {}
inline void Log(ProtoLogLevel level, const std::string_view group, uint64_t messageHash,
                uint64_t paramsMask, int argCount, IArgumentProvider& provider) {}

#define PROTOLOG(...) (void)0
#define PROTOLOG_V(...) (void)0
#define PROTOLOG_D(...) (void)0
#define PROTOLOG_I(...) (void)0
#define PROTOLOG_W(...) (void)0
#define PROTOLOG_E(...) (void)0
#define PROTOLOG_WTF(...) (void)0
#endif

} // namespace protolog
} // namespace android

#endif // ANDROID_INTERNAL_PROTOLOG_H
