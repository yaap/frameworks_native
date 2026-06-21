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

// #define LOG_NDEBUG 0
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <android-base/strings.h>
#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <ftl/concat.h>
#include <gui/BufferItemConsumer.h>
#include <gui/Surface.h>
#include <hardware/gralloc.h>
#include <log/log_main.h>
#include <system/window.h>
#include <ui/DisplayId.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>
#include <utils/Errors.h>
#include <utils/Trace.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "VirtualDisplayThread.h"
#include "VirtualDisplayThreadManager.h"

namespace android {

std::shared_ptr<VirtualDisplayThread> VirtualDisplayThread::create() {
    // make_shared doesn't work with private ctors :(
    std::shared_ptr<VirtualDisplayThread> vdt(new VirtualDisplayThread());
    vdt->init();
    return vdt;
}

void VirtualDisplayThread::init() {
    mThread = std::thread([self = shared_from_this()]() { vdThreadMain(std::move(self)); });
}

void VirtualDisplayThread::destroy() {
    ATRACE_CALL();
    std::scoped_lock _l(mWorkMutex);

    submitWorkLocked(kInternalContextName, "destroy-task", [this]() {
        ATRACE_NAME("destroy-task");
        mState = SHUT_DOWN;
        // After this task, the worker thread should clean itself up, freeing the buffers
        // automatically.
    });
    mState = SHUTTING_DOWN;
}

VirtualDisplayThread::Client::Client(uid_t uid, std::shared_ptr<VirtualDisplayThread> thread)
      : mUid(uid), mThread(std::move(thread)) {}

VirtualDisplayThread::Client::~Client() {
    VirtualDisplayThreadManager::getInstance().releaseThread(mUid);
}

bool VirtualDisplayThread::Client::isFrozen() const {
    ATRACE_CALL();

    std::scoped_lock _l(mThread->mWorkMutex);
    if (!mThread->mIsWorking) {
        return false;
    }

    // TODO(b/340933138): revisit this
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastTask = now - mThread->mLastWorkStartedTimeNs;
    return timeSinceLastTask >= std::chrono::milliseconds(250);
}

void VirtualDisplayThread::Client::submitWork(const std::string& contextName,
                                              const std::string& label,
                                              std::function<void()>&& work) {
    ATRACE_CALL();

    std::scoped_lock _l(mThread->mWorkMutex);
    mThread->submitWorkLocked(contextName, label, std::move(work));
}

void VirtualDisplayThread::submitWorkLocked(const std::string& contextName,
                                            const std::string& label,
                                            std::function<void()>&& work) {
    ATRACE_CALL();

    VirtualDisplayThread::State state = mState.load();
    if (state == SHUT_DOWN || state == SHUTTING_DOWN) {
        return;
    }

    mWorkQueue.push({.work = std::move(work), .contextName = contextName, .label = label});
    mCondVar.notify_one();
}

void VirtualDisplayThread::clearQueueLocked() {
    while (!mWorkQueue.empty()) {
        mWorkQueue.pop();
    }
}

void VirtualDisplayThread::vdThreadMain(std::shared_ptr<VirtualDisplayThread> self) {
    self->mState = RUNNING;

    while (true) {
        if (!self->vdThreadPollAndWork()) {
            return;
        }
    }
}

bool VirtualDisplayThread::vdThreadPollAndWork() {
    auto lock = std::unique_lock(mWorkMutex);

    mCondVar.wait(lock, [this] { return !mWorkQueue.empty(); });

    auto task = std::move(mWorkQueue.front());
    mWorkQueue.pop();

    {
        ftl::Concat traceName("VirtualDisplayThread-RunTask-", ftl::truncated<64>(task.label));
        ATRACE_NAME(traceName.c_str());
        mIsWorking = true;
        mCurrentTaskContextName = task.contextName;
        mCurrentTaskLabel = task.label;
        mLastWorkStartedTimeNs = std::chrono::steady_clock::now();
        lock.unlock();
        // This task talks to the application-provide surface sink, and can arbitrarily
        // block or deadlock!
        task.work();
        lock.lock();

        mIsWorking = false;
        mTotalTasksProcessed++;
    }

    return mState != SHUT_DOWN;
}

void VirtualDisplayThread::dump(utils::Dumper& dumper) const {
    std::scoped_lock _l(mWorkMutex);
    const auto stateStr = [](State state) {
        switch (state) {
            case INITIALIZING:
                return "INITIALIZING";
            case RUNNING:
                return "RUNNING";
            case SHUTTING_DOWN:
                return "SHUTTING_DOWN";
            case SHUT_DOWN:
                return "SHUT_DOWN";
        }
    };

    using namespace std::string_view_literals;
    dumper.dump("state"sv, std::string_view(stateStr(mState)));
    dumper.dump("workQueueDepth"sv, mWorkQueue.size());
    dumper.dump("totalTasksProcessed"sv, mTotalTasksProcessed);

    if (mIsWorking) {
        auto now = std::chrono::steady_clock::now();
        auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastWorkStartedTimeNs);
        dumper.dump("status"sv, "WORKING"sv);
        dumper.dump("currentContext"sv, mCurrentTaskContextName);
        dumper.dump("currentTask"sv, mCurrentTaskLabel);
        dumper.dump("currentTaskDurationMs"sv, duration.count());
    } else {
        dumper.dump("status"sv, "IDLE"sv);
    }
}

} // namespace android