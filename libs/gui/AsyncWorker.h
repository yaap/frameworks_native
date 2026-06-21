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
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// A single worker thread that services a list of runnables.
class AsyncWorker {
public:
    AsyncWorker();
    ~AsyncWorker();

    void post(std::function<void()> runnable);

private:
    std::thread mThread;
    bool mDone GUARDED_BY(mMutex) = false;
    std::deque<std::function<void()>> mRunnables GUARDED_BY(mMutex);
    std::mutex mMutex;
    std::condition_variable mCv;

    void run();
    void execute(std::deque<std::function<void()>>& runnables);
    bool waitingDone() REQUIRES(mMutex);
};
