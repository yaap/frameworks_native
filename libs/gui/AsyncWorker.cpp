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

#include "AsyncWorker.h"

AsyncWorker::AsyncWorker() {}

AsyncWorker::~AsyncWorker() {
    {
        std::scoped_lock<std::mutex> lock(mMutex);
        mDone = true;
        mCv.notify_all();
    }

    if (mThread.joinable()) {
        mThread.join();
    }
}

void AsyncWorker::post(std::function<void()> runnable) {
    std::scoped_lock<std::mutex> lock(mMutex);
    // Not joinable means mThread is still default constructed
    // and needs initialization now.
    if (!mThread.joinable()) {
        mThread = std::thread(&AsyncWorker::run, this);
    }
    mRunnables.emplace_back(std::move(runnable));
    mCv.notify_one();
}

bool AsyncWorker::waitingDone() {
    return mDone || !mRunnables.empty();
}

void AsyncWorker::run() {
    std::unique_lock<std::mutex> lock(mMutex);
    android::base::ScopedLockAssertion lock_assertion(mMutex);
    while (!mDone) {
        while (!mRunnables.empty()) {
            std::deque<std::function<void()>> runnables = std::move(mRunnables);
            mRunnables.clear();
            lock.unlock();
            // Run outside the lock since the runnable might trigger another
            // post to the async worker.
            execute(runnables);
            lock.lock();
        }
        mCv.wait(lock, [this] {
            android::base::ScopedLockAssertion lock_assertion(mMutex);
            return waitingDone();
        });
    }
}

void AsyncWorker::execute(std::deque<std::function<void()>>& runnables) {
    while (!runnables.empty()) {
        std::function<void()> runnable = runnables.front();
        runnables.pop_front();
        runnable();
    }
}
