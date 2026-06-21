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

#pragma once

#include <assert.h>
#include <stdint.h>

namespace android {

// RPointer is a relative pointer. It stores a 32-bit offset to the target object,
// relative to its own address. This makes it suitable for use in data
// structures that are allocated in a shared memory buffer, written to a file, or
// otherwise relocated in memory. A raw pointer would be invalidated if the data
// is moved, but an RPointer will remain valid as long as the relative layout of
// the data is preserved.
template <typename T>
struct RPointer {
    T* get() {
        if (offset == 0) return nullptr;
        return (T*)(((uint8_t*)&offset) + offset);
    }
    const T* get() const {
        if (offset == 0) return nullptr;
        return (const T*)(((const uint8_t*)&offset) + offset);
    }

    T* operator->() { return get(); }
    const T* operator->() const { return get(); }

    void set(const T* p) {
        offset = p ? static_cast<int32_t>(((const uint8_t*)p) - ((const uint8_t*)this)) : 0;
    }
    void operator=(const T* p) { set(p); }

    void operator=(const RPointer& other) { set(other.get()); }

    operator T*() { return get(); }
    operator const T*() const { return get(); }

    explicit operator bool() const { return offset != 0; }

private:
    int32_t offset;
};

template <typename T>
struct RSpan {
    RPointer<T> data;
    uint32_t size;
};

} // namespace android