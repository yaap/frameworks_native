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
#define LOG_TAG "libbinder.Binder"

#include <binder/internal/JavaBBinderBase.h>

namespace android::internal {

JavaBBinderBase::JavaBBinderBase() = default;
JavaBBinderBase::~JavaBBinderBase() = default;

const void* JavaBBinderBase::getExtSubclassID() {
    static const char* const kSubclassID = "JavaBBinderExt";
    return kSubclassID;
}

} // namespace android::internal
