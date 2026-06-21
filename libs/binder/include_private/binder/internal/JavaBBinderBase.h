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

#pragma once

#include <binder/Binder.h>
#include <binder/Functional.h>

namespace android {

namespace internal {

// This API is internal to the binder platform. Many libbinder APIs are intended to be internal,
// but please avoid using this API directly.
class LIBBINDER_EXPORTED JavaBBinderBase : public BBinder {
public:
    JavaBBinderBase();

    // Only use this method to check if the binder belongs to type JavaBBinderExt
    static const void* getExtSubclassID();

    virtual void getFunctionName(
            uint32_t code,
            const android::binder::impl::SmallFunction<void(const char*)>& callback) const = 0;

protected:
    virtual ~JavaBBinderBase();
};

} // namespace internal

} // namespace android
