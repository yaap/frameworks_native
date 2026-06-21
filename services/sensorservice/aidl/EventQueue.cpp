/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "AidlSensorEventQueue"

#include "EventQueue.h"
#include "utils.h"

#include <android-base/logging.h>
#include <log/log.h>
#include <utils/Looper.h>

namespace android {
namespace frameworks {
namespace sensorservice {
namespace implementation {

using ::aidl::android::frameworks::sensorservice::IEventQueueCallback;
using ::aidl::android::hardware::sensors::Event;

class EventQueueLooperCallback : public ::android::LooperCallback {
public:
    EventQueueLooperCallback(sp<::android::SensorEventQueue> queue,
                             std::shared_ptr<IEventQueueCallback> callback)
          : mQueue(queue), mCallback(callback) {}

    int handleEvent(int /* fd */, int /* events */, void* /* data */) {
        ASensorEvent event;
        ssize_t actual;

        auto internalQueue = mQueue.promote();
        if (internalQueue == nullptr) {
            return 1;
        }

        while ((actual = internalQueue->read(&event, 1)) > 0) {
            internalQueue->sendAck(&event, actual);
            ndk::ScopedAStatus ret = mCallback->onEvent(convertEvent(event));
            if (!ret.isOk()) {
                LOG(ERROR) << "Failed to envoke EventQueueCallback: " << ret;
            }
        }

        return 1; // continue to receive callbacks
    }

private:
    wp<::android::SensorEventQueue> mQueue;
    std::shared_ptr<IEventQueueCallback> mCallback;
};

EventQueue::EventQueue(std::shared_ptr<IEventQueueCallback> callback, sp<::android::Looper> looper,
                       sp<::android::SensorEventQueue> internalQueue)
      : mLooper(looper), mInternalQueue(internalQueue) {
    mLooper->addFd(internalQueue->getFd(), ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                   new EventQueueLooperCallback(internalQueue, callback), nullptr);
}

EventQueue::~EventQueue() {
    std::vector<int32_t> activeSensors;
    {
        std::lock_guard<std::mutex> lock(mActiveSensorsLock);
        activeSensors.assign(mActiveSensors.begin(), mActiveSensors.end());
    }

    for (int32_t handle : activeSensors) {
        mInternalQueue->disableSensor(handle);
    }

    mLooper->removeFd(mInternalQueue->getFd());
}

ndk::ScopedAStatus EventQueue::enableSensor(int32_t in_sensorHandle, int32_t in_samplingPeriodUs,
                                            int64_t in_maxBatchReportLatencyUs) {
    status_t err = mInternalQueue->enableSensor(in_sensorHandle, in_samplingPeriodUs,
                                                in_maxBatchReportLatencyUs, 0);

    if (err == ::android::OK) {
        std::lock_guard<std::mutex> lock(mActiveSensorsLock);
        mActiveSensors.insert(in_sensorHandle);
    } else {
        ALOGE("Failed to enable sensor handle %d. Error: %d", in_sensorHandle, err);
    }

    return convertResult(err);
}

ndk::ScopedAStatus EventQueue::disableSensor(int32_t in_sensorHandle) {
    status_t err = mInternalQueue->disableSensor(in_sensorHandle);

    if (err == ::android::OK) {
        std::lock_guard<std::mutex> lock(mActiveSensorsLock);
        mActiveSensors.erase(in_sensorHandle);
    } else {
        ALOGE("Failed to disable sensor handle %d. Error: %d", in_sensorHandle, err);
    }

    return convertResult(err);
}

} // namespace implementation
} // namespace sensorservice
} // namespace frameworks
} // namespace android
