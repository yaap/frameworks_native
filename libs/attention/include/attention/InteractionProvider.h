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

#include <android-base/thread_annotations.h>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "AttentionTypes.h"

namespace android {
namespace attention {

/**
 *  A helper class for applications to provide interaction events to the AttentionManagerService.
 *  This class handles the binder implementation details and ensures that only the latest event
 *  for each interaction type is reported.
 */
class InteractionProvider {
public:
    void reportInteraction(InteractionType interactionType,
                           std::chrono::milliseconds interactionTime);

    std::vector<InteractionState> getSourceInteractions();

    void requestWakeupCallback(std::function<void()> callback);

private:
    std::mutex mMutex;
    std::map<int32_t, std::chrono::milliseconds> mInteractions GUARDED_BY(mMutex);
    std::function<void()> mCallback GUARDED_BY(mMutex);
};

} // namespace attention
} // namespace android
