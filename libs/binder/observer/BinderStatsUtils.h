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
#include <utils/String16.h>
#include "../BuildFlags.h"

namespace android {
// Data for a monitored binder transaction.
struct BinderCallData {
    // TODO(b/299356196): Use the receiver binder object instead and resolve interface lazily
    int64_t startTimeNanos;
    int64_t endTimeNanos;
    int64_t cpuTimeNanos;
    String16 interfaceDescriptor;
    String16 aidlMethodName;
    uint32_t transactionCode;
    uint32_t senderUid;

    bool hasLatencyData() const {
        if (kBinderObserverV2Enabled) {
            return startTimeNanos > 0;
        } else {
            return endTimeNanos > 0 && startTimeNanos > 0;
        }
    }
};

} // namespace android
