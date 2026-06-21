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

#define LOG_TAG "Surface2HGBPTest"

#include <android-base/stringprintf.h>
#include <gtest/gtest.h>
#include <gui/BufferItemConsumer.h>
#include <gui/BufferQueue.h>
#include <gui/bufferqueue/2.0/H2BGraphicBufferProducer.h>
#include <gui/bufferqueue/2.0/types.h>
#include <hardware/gralloc.h>
#include <system/window.h>
#include <ui/Fence.h>
#include <ui/Rect.h>
#include <utils/Log.h>
#include <utils/RefBase.h>

#include <gui/bufferqueue/2.0/Surface2HGraphicBufferProducer.h>

namespace android {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::graphics::bufferqueue::V2_0::utils::Surface2HGraphicBufferProducer;
using ::android::hardware::graphics::common::V1_2::HardwareBuffer;

using HConnectionType = ::android::hardware::graphics::bufferqueue::V2_0::ConnectionType;
using HGraphicBufferProducer =
        ::android::hardware::graphics::bufferqueue::V2_0::IGraphicBufferProducer;
using HProducerListener = ::android::hardware::graphics::bufferqueue::V2_0::IProducerListener;
using HStatus = ::android::hardware::graphics::bufferqueue::V2_0::Status;
using HDataSpace = ::android::hardware::graphics::common::V1_0::Dataspace;

const uint64_t kDefaultUsage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

class Surface2HGraphicBufferProducerTest : public ::testing::Test {
protected:
    void SetUp() override {
        sp<Surface> surface;
        std::tie(mConsumer, surface) = BufferItemConsumer::create(kDefaultUsage);
        mHGBP = sp<Surface2HGraphicBufferProducer>::make(surface);
    }

    sp<BufferItemConsumer> mConsumer;
    sp<Surface2HGraphicBufferProducer> mHGBP;

    // Wrapper functions for Surface2HGraphicBufferProducer methods

    Return<HStatus> setMaxDequeuedBufferCount(int32_t maxDequeuedBuffers) {
        return mHGBP->setMaxDequeuedBufferCount(maxDequeuedBuffers);
    }

    std::tuple<HStatus, HardwareBuffer, uint32_t> requestBuffer(int32_t slot) {
        HStatus outStatus;
        HardwareBuffer outBuffer;
        uint32_t outGenerationNumber;
        mHGBP->requestBuffer(slot,
                             [&](HStatus status, const HardwareBuffer& buffer,
                                 uint32_t generationNumber) {
                                 outStatus = status;
                                 outBuffer = buffer;
                                 outGenerationNumber = generationNumber;
                             });
        return {outStatus, outBuffer, outGenerationNumber};
    }

    Return<HStatus> setAsyncMode(bool async) { return mHGBP->setAsyncMode(async); }

    std::tuple<HStatus, int32_t, HGraphicBufferProducer::DequeueBufferOutput> dequeueBuffer(
            const HGraphicBufferProducer::DequeueBufferInput& input) {
        HStatus outStatus;
        int32_t outSlot;
        HGraphicBufferProducer::DequeueBufferOutput outOutput;
        mHGBP->dequeueBuffer(input,
                             [&](HStatus status, int32_t slot,
                                 const HGraphicBufferProducer::DequeueBufferOutput& output) {
                                 outStatus = status;
                                 outSlot = slot;
                                 outOutput = output;
                             });
        return {outStatus, outSlot, outOutput};
    }

    Return<HStatus> detachBuffer(int32_t slot) { return mHGBP->detachBuffer(slot); }

    std::tuple<HStatus, HardwareBuffer, ::android::hardware::hidl_handle> detachNextBuffer() {
        HStatus outStatus;
        HardwareBuffer outBuffer;
        ::android::hardware::hidl_handle outFence;
        mHGBP->detachNextBuffer([&](HStatus status, const HardwareBuffer& buffer,
                                    const ::android::hardware::hidl_handle& fence) {
            outStatus = status;
            outBuffer = buffer;
            outFence = fence;
        });
        return {outStatus, outBuffer, outFence};
    }

    std::tuple<HStatus, int32_t, bool> attachBuffer(const HardwareBuffer& buffer,
                                                    uint32_t generationNumber) {
        HStatus outStatus;
        int32_t outSlot;
        bool outReleaseAllBuffers;
        mHGBP->attachBuffer(buffer, generationNumber,
                            [&](HStatus status, int32_t slot, bool releaseAllBuffers) {
                                outStatus = status;
                                outSlot = slot;
                                outReleaseAllBuffers = releaseAllBuffers;
                            });
        return {outStatus, outSlot, outReleaseAllBuffers};
    }

    std::tuple<HStatus, HGraphicBufferProducer::QueueBufferOutput> queueBuffer(
            int32_t slot, const HGraphicBufferProducer::QueueBufferInput& input) {
        HStatus outStatus;
        HGraphicBufferProducer::QueueBufferOutput outOutput;
        mHGBP->queueBuffer(slot, input,
                           [&](HStatus status,
                               const HGraphicBufferProducer::QueueBufferOutput& output) {
                               outStatus = status;
                               outOutput = output;
                           });
        return {outStatus, outOutput};
    }

    Return<HStatus> cancelBuffer(int32_t slot, const ::android::hardware::hidl_handle& fence) {
        return mHGBP->cancelBuffer(slot, fence);
    }

    std::tuple<int32_t, int32_t> query(int32_t what) {
        int32_t outValue;
        int32_t outResult;
        mHGBP->query(what, [&](int32_t result, int32_t value) {
            outValue = value;
            outResult = result;
        });
        return {outValue, outResult};
    }

    std::tuple<HStatus, HGraphicBufferProducer::QueueBufferOutput> connect(
            const sp<HProducerListener>& listener, HConnectionType api,
            bool producerControlledByApp) {
        HStatus outStatus;
        HGraphicBufferProducer::QueueBufferOutput outOutput;
        mHGBP->connect(listener, api, producerControlledByApp,
                       [&](HStatus status,
                           const HGraphicBufferProducer::QueueBufferOutput& output) {
                           outStatus = status;
                           outOutput = output;
                       });
        return {outStatus, outOutput};
    }

    Return<HStatus> disconnect(HConnectionType api) { return mHGBP->disconnect(api); }

    Return<HStatus> allocateBuffers(uint32_t width, uint32_t height, uint32_t format,
                                    uint64_t usage) {
        return mHGBP->allocateBuffers(width, height, format, usage);
    }

    Return<HStatus> allowAllocation(bool allow) { return mHGBP->allowAllocation(allow); }

    Return<HStatus> setGenerationNumber(uint32_t generationNumber) {
        return mHGBP->setGenerationNumber(generationNumber);
    }

    Return<HStatus> setDequeueTimeout(int64_t timeoutNs) {
        return mHGBP->setDequeueTimeout(timeoutNs);
    }

    Return<uint64_t> getUniqueId() { return mHGBP->getUniqueId(); }

    std::string getConsumerName() {
        std::string outName;
        mHGBP->getConsumerName(
                [&](const ::android::hardware::hidl_string& name) { outName = name; });
        return outName;
    }
};

class TestHProducerListener : public HProducerListener {
public:
    explicit TestHProducerListener() = default;

    ::android::hardware::Return<void> onBuffersReleased(uint32_t) override { return {}; }
};

TEST_F(Surface2HGraphicBufferProducerTest, Queue_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    EXPECT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{
            .width = 1,
            .height = 1,
    };
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    EXPECT_EQ(HStatus::OK, dequeueStatus);

    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    EXPECT_EQ(HStatus::OK, requestStatus);

    HGraphicBufferProducer::QueueBufferInput qInput{};
    qInput.timestamp = 12345;
    qInput.isAutoTimestamp = false;
    qInput.dataSpace = static_cast<int32_t>(HDataSpace::UNKNOWN);
    qInput.crop[0] = 0;
    qInput.crop[1] = 0;
    qInput.crop[2] = 1;
    qInput.crop[3] = 1;
    qInput.transform = 0;
    qInput.fence = ::android::hardware::hidl_handle(); // Empty fence
    auto [queueBufferStatus, queueBufferOutput] = queueBuffer(slot, qInput);
    EXPECT_EQ(HStatus::OK, queueBufferStatus);
    EXPECT_EQ(1u, queueBufferOutput.width);
    EXPECT_EQ(1u, queueBufferOutput.height);
    EXPECT_GE(queueBufferOutput.nextFrameNumber, 1u);

    BufferItem item;
    status_t ret = mConsumer->acquireBuffer(&item, 0);
    EXPECT_EQ(NO_ERROR, ret);
}

TEST_F(Surface2HGraphicBufferProducerTest, Connect_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();

    // Invalid API returns bad value
    auto [status, output] = connect(listener, static_cast<HConnectionType>(0xDEADBEEF), false);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, ConnectAgain_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Can't connect when there is already a producer connected
    auto [status, output] = connect(listener, HConnectionType::CPU, false);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnect_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HStatus status = disconnect(HConnectionType::CPU);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnect_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Must disconnect with the same API number used to connect
    HStatus status = disconnect(static_cast<HConnectionType>(
            static_cast<int32_t>(HConnectionType::CPU) + 1)); // Different API
    EXPECT_EQ(HStatus::BAD_VALUE, status);
    // API must not be out of range
    status = disconnect(static_cast<HConnectionType>(0xDEADBEEF));
    EXPECT_EQ(HStatus::BAD_VALUE, status);

    // Disconnecting twice should fail
    ASSERT_EQ(HStatus::OK, disconnect(HConnectionType::CPU));
    status = disconnect(HConnectionType::CPU);
    EXPECT_EQ(HStatus::NO_INIT, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_DequeueBuffer) {
    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [status, slot, output] = dequeueBuffer(input);
    EXPECT_EQ(HStatus::NO_INIT, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_DetachNextBuffer) {
    auto [status, buffer, fence] = detachNextBuffer();
    EXPECT_EQ(HStatus::NO_INIT, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_RequestBuffer) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    ASSERT_EQ(HStatus::OK, disconnect(HConnectionType::CPU));

    auto [status, buffer, generationNumber] = requestBuffer(slot);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_DetachBuffer) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    ASSERT_EQ(HStatus::OK, requestStatus);

    ASSERT_EQ(HStatus::OK, disconnect(HConnectionType::CPU));

    HStatus status = detachBuffer(slot);
    // According to the HGBP spec, this should return BAD_VALUE:
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_QueueBuffer) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    ASSERT_EQ(HStatus::OK, requestStatus);

    ASSERT_EQ(HStatus::OK, disconnect(HConnectionType::CPU));

    HGraphicBufferProducer::QueueBufferInput qInput{};
    auto [status, qOutput] = queueBuffer(slot, qInput);
    // According to the HGBP spec, no specific error code is defined for this case:
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_CancelBuffer) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    ASSERT_EQ(HStatus::OK, disconnect(HConnectionType::CPU));

    HStatus status = cancelBuffer(slot, ::android::hardware::hidl_handle());
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_SetMaxDequeuedBufferCount) {
    HStatus status = setMaxDequeuedBufferCount(2);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_SetAsyncMode) {
    HStatus status = setAsyncMode(true);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_AllocateBuffers) {
    HStatus status = allocateBuffers(100, 100, 1, kDefaultUsage);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_AllowAllocation) {
    HStatus status = allowAllocation(true);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_SetGenerationNumber) {
    HStatus status = setGenerationNumber(1);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Disconnected_SetDequeueTimeout) {
    HStatus status = setDequeueTimeout(1000);
    EXPECT_EQ(HStatus::OK, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, GetConsumerName) {
    mConsumer->setName(String8("name-for-test"));

    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    std::string name = getConsumerName();
    EXPECT_EQ("name-for-test", name);
}

TEST_F(Surface2HGraphicBufferProducerTest, Query_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    auto [value, result] = query(NATIVE_WINDOW_WIDTH);
    EXPECT_EQ(0, result);
    EXPECT_EQ(1u, static_cast<uint32_t>(value)); // Default width

    std::tie(value, result) = query(NATIVE_WINDOW_HEIGHT);
    EXPECT_EQ(0, result);
    EXPECT_EQ(1u, static_cast<uint32_t>(value)); // Default height

    std::tie(value, result) = query(NATIVE_WINDOW_FORMAT);
    EXPECT_EQ(0, result);
    EXPECT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, value); // Default format

    std::tie(value, result) = query(NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS);
    EXPECT_EQ(0, result);
    EXPECT_LE(0, value);
    EXPECT_GE(BufferQueueDefs::NUM_BUFFER_SLOTS, value);

    std::tie(value, result) = query(NATIVE_WINDOW_CONSUMER_USAGE_BITS);
    EXPECT_EQ(0, result);
    EXPECT_EQ(kDefaultUsage, static_cast<uint64_t>(value));
}

TEST_F(Surface2HGraphicBufferProducerTest, Query_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    auto [value, result] = query(-1); // Invalid what
    EXPECT_EQ(int32_t(HStatus::BAD_VALUE), result);

    std::tie(value, result) = query(0xDEADBEEF); // Invalid what
    EXPECT_EQ(int32_t(HStatus::BAD_VALUE), result);

    // NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER is not supported
    std::tie(value, result) = query(NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER);
    EXPECT_EQ(0, result);
}

TEST_F(Surface2HGraphicBufferProducerTest, Queue_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::QueueBufferInput qInput{};
    auto [status, qOutput] = queueBuffer(-1, qInput); // Invalid slot
    EXPECT_EQ(HStatus::BAD_VALUE, status);

    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer({});
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    // Queue without request
    std::tie(status, qOutput) = queueBuffer(slot, qInput);
    EXPECT_EQ(HStatus::OK, status);

    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    ASSERT_EQ(HStatus::OK, requestStatus);

    // Crop rect out of bounds
    qInput.crop[0] = 0;
    qInput.crop[1] = 0;
    qInput.crop[2] = 2;
    qInput.crop[3] = 2;
    std::tie(status, qOutput) = queueBuffer(slot, qInput);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, CancelBuffer_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer({});
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    HStatus status = cancelBuffer(slot, ::android::hardware::hidl_handle());
    EXPECT_EQ(HStatus::OK, status);

    // Second cancel should fail as the buffer is no longer dequeued
    // But Surface implementation allows it, so we expect OK.
    status = cancelBuffer(slot, ::android::hardware::hidl_handle());
    EXPECT_EQ(HStatus::OK, status);
}

// Ported from IGraphicBufferProducer_test.cpp & BufferQueue_test.cpp
// Configuration Tests

TEST_F(Surface2HGraphicBufferProducerTest, SetMaxDequeuedBufferCount_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    EXPECT_EQ(HStatus::OK, setMaxDequeuedBufferCount(2));
    EXPECT_EQ(HStatus::OK, setMaxDequeuedBufferCount(BufferQueueDefs::NUM_BUFFER_SLOTS));
}

TEST_F(Surface2HGraphicBufferProducerTest, SetMaxDequeuedBufferCount_Fails) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    EXPECT_EQ(HStatus::BAD_VALUE, setMaxDequeuedBufferCount(0));
    EXPECT_EQ(HStatus::BAD_VALUE, setMaxDequeuedBufferCount(BufferQueueDefs::NUM_BUFFER_SLOTS + 1));

    // Dequeue 2 buffers
    ASSERT_EQ(HStatus::OK, setMaxDequeuedBufferCount(2));
    for (int i = 0; i < 2; ++i) {
        auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer({});
        ASSERT_EQ(HStatus::OK, dequeueStatus);
    }
    // Cannot set max lower than the number currently dequeued
    EXPECT_EQ(HStatus::BAD_VALUE, setMaxDequeuedBufferCount(1));
}

TEST_F(Surface2HGraphicBufferProducerTest, SetAsyncMode_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    EXPECT_EQ(HStatus::OK, setAsyncMode(true));
    EXPECT_EQ(HStatus::OK, setAsyncMode(false));
}

TEST_F(Surface2HGraphicBufferProducerTest, DisallowAllocation) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, true);
    ASSERT_EQ(HStatus::OK, connectStatus);

    EXPECT_EQ(HStatus::OK, setDequeueTimeout(0));

    EXPECT_EQ(HStatus::OK, allowAllocation(false));
    HGraphicBufferProducer::DequeueBufferInput input{};
    input.width = 100;
    input.height = 100;
    input.format = HAL_PIXEL_FORMAT_RGBA_8888;
    input.usage = kDefaultUsage;
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    EXPECT_EQ(HStatus::TIMED_OUT, dequeueStatus);

    EXPECT_EQ(HStatus::OK, allowAllocation(true));
    std::tie(dequeueStatus, slot, dequeueOutput) = dequeueBuffer(input);
    EXPECT_EQ(HStatus::OK, dequeueStatus);
}

TEST_F(Surface2HGraphicBufferProducerTest, CanAttachWhileDisallowingAllocation) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, true);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    ASSERT_EQ(HStatus::OK, requestStatus);
    ASSERT_EQ(HStatus::OK, detachBuffer(slot));

    EXPECT_EQ(HStatus::OK, allowAllocation(false));

    auto [attachStatus, attachSlot, releaseAll] = attachBuffer(buffer, generationNumber);
    EXPECT_EQ(HStatus::OK, attachStatus);
}

TEST_F(Surface2HGraphicBufferProducerTest, Detach_BufferIsNotLeaked) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    {
        auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
        ASSERT_EQ(HStatus::OK, requestStatus);
    }

    auto [queueStatus, qOutput] = queueBuffer(slot, {});
    ASSERT_EQ(HStatus::OK, queueStatus);

    wp<GraphicBuffer> weakBuffer;
    {
        BufferItem item;
        ASSERT_EQ(OK, mConsumer->acquireBuffer(&item, 0));
        weakBuffer = item.mGraphicBuffer;
        ASSERT_EQ(OK, mConsumer->releaseBuffer(item, item.mFence));
    }

    ASSERT_NE(nullptr, weakBuffer.promote());

    auto [dequeueStatus2, slot2, dequeueOutput2] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus2);

    // Ensure the buffer is the same we got previously
    ASSERT_FALSE(dequeueOutput2.bufferNeedsReallocation || dequeueOutput2.releaseAllBuffers);
    ASSERT_EQ(slot, slot2);

    ASSERT_EQ(HStatus::OK, detachBuffer(slot));
    ASSERT_EQ(nullptr, weakBuffer.promote());
}

// TODO(b/478953869): Disabled until the underlying surface bug is fixed.
TEST_F(Surface2HGraphicBufferProducerTest, DISABLED_ConsumerDetach_BufferIsNotLeaked) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    {
        auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
        ASSERT_EQ(HStatus::OK, requestStatus);
    }

    HGraphicBufferProducer::QueueBufferInput qbi;
    auto [status, qOutput] = queueBuffer(slot, qbi);
    ASSERT_EQ(HStatus::OK, status);

    wp<GraphicBuffer> weakBuffer;
    {
        BufferItem item;
        ASSERT_EQ(OK, mConsumer->acquireBuffer(&item, 0));
        weakBuffer = item.mGraphicBuffer;
        ASSERT_EQ(OK, mConsumer->detachBuffer(item.mGraphicBuffer));
        ASSERT_NE(nullptr, weakBuffer.promote());
    }

    EXPECT_EQ(nullptr, weakBuffer.promote());
}

TEST_F(Surface2HGraphicBufferProducerTest, Dequeue_InvalidDimensions_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    input.width = 1;
    input.height = 0; // Invalid: one is zero, other is not
    input.format = HAL_PIXEL_FORMAT_RGBA_8888;
    input.usage = kDefaultUsage;
    auto [status, slot, output] = dequeueBuffer(input);
    EXPECT_EQ(HStatus::BAD_VALUE, status);

    input.width = 0;
    input.height = 1; // Invalid: one is zero, other is not
    std::tie(status, slot, output) = dequeueBuffer(input);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, Attach_BadGenerationNumber_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Set generation number to 10
    EXPECT_EQ(HStatus::OK, setGenerationNumber(10));

    // Dequeue a buffer (will have generation 10)
    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);
    auto [requestStatus, buffer, generationNumber] = requestBuffer(slot);
    ASSERT_EQ(HStatus::OK, requestStatus);
    EXPECT_EQ(10u, generationNumber);

    ASSERT_EQ(HStatus::OK, detachBuffer(slot));

    // Try to attach with wrong generation number (e.g., 5)
    auto [attachStatus, attachSlot, releaseAll] = attachBuffer(buffer, 5);
    EXPECT_EQ(HStatus::BAD_VALUE, attachStatus);
    // Try to attach with correct generation number (10)
    std::tie(attachStatus, attachSlot, releaseAll) = attachBuffer(buffer, 10);
    EXPECT_EQ(HStatus::OK, attachStatus);
}

TEST_F(Surface2HGraphicBufferProducerTest, Attach_TooManyDequeued_ReturnsError) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // First queue and dequeue, because BQs allow for unlimited dequeued right after a connect, then
    // enforce the limit.
    HGraphicBufferProducer::DequeueBufferInput input{};
    {
        auto [dequeueStatus1, slot1, dequeueOutput1] = dequeueBuffer(input);
        ASSERT_EQ(HStatus::OK, dequeueStatus1);
        auto [queueStatus1, qOutput1] = queueBuffer(slot1, {});
        ASSERT_EQ(HStatus::OK, queueStatus1);
    }

    // Set the max dequeued buffer count to 2 so we can dequeue 2 buffers and detach one of them.
    ASSERT_EQ(HStatus::OK, setMaxDequeuedBufferCount(2));

    auto [dequeueStatus1, slot1, dequeueOutput1] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus1);

    auto [dequeueStatus2, slot2, dequeueOutput2] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus2);
    auto [requestStatus, testBuffer, gen2] = requestBuffer(slot2);
    ASSERT_EQ(HStatus::OK, detachBuffer(slot2));

    // Now set max to 1. Slot1 is still dequeued,
    ASSERT_EQ(HStatus::OK, setMaxDequeuedBufferCount(1));

    auto [attachStatus, attachSlot, releaseAll] = attachBuffer(testBuffer, gen2);
    EXPECT_EQ(HStatus::INVALID_OPERATION, attachStatus);
}

TEST_F(Surface2HGraphicBufferProducerTest, RequestBuffer_InvalidSlot_ReturnsBadValue) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Request buffer with an invalid slot number
    auto [status, buffer, generationNumber] = requestBuffer(-1);
    EXPECT_EQ(HStatus::BAD_VALUE, status);

    // Request buffer with a slot that has not been dequeued
    std::tie(status, buffer, generationNumber) = requestBuffer(0);
    EXPECT_EQ(HStatus::BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, DequeueBuffer_TooManyDequeued_ReturnsInvalidOperation) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Dequeue and queue a buffer to prime the buffer queue
    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);
    auto [queueStatus, qOutput] = queueBuffer(slot, {});
    ASSERT_EQ(HStatus::OK, queueStatus);

    // Set max dequeued buffer count to 1
    ASSERT_EQ(HStatus::OK, setMaxDequeuedBufferCount(1));

    // Dequeue one buffer, which should succeed
    std::tie(dequeueStatus, slot, dequeueOutput) = dequeueBuffer({});
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    // Try to dequeue another buffer, which should fail
    auto [dequeueStatus2, slot2, dequeueOutput2] = dequeueBuffer({});
    EXPECT_EQ(HStatus::INVALID_OPERATION, dequeueStatus2);
}

TEST_F(Surface2HGraphicBufferProducerTest, DequeueBuffer_WouldBlock) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    // Connect in a mode where producer and consumer are controlled by the app
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, true);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // Dequeue and queue a buffer to prime the buffer queue
    HGraphicBufferProducer::DequeueBufferInput input{};
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);
    auto [queueStatus, qOutput] = queueBuffer(slot, {});
    ASSERT_EQ(HStatus::OK, queueStatus);

    // Set max dequeued buffer count to 1 and dequeue a buffer
    ASSERT_EQ(HStatus::OK, setMaxDequeuedBufferCount(1));
    std::tie(dequeueStatus, slot, dequeueOutput) = dequeueBuffer({});
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    // Dequeueing another buffer should return WOULD_BLOCK because this is a
    // non-blocking call and no buffers are available.
    auto [dequeueStatus2, slot2, dequeueOutput2] = dequeueBuffer({});
    EXPECT_EQ(HStatus::INVALID_OPERATION, dequeueStatus2);
}

TEST_F(Surface2HGraphicBufferProducerTest, Connect_NoConsumer_ReturnsNoInit) {
    sp<Surface> surface;
    sp<BufferItemConsumer> consumer;
    std::tie(consumer, surface) = BufferItemConsumer::create(kDefaultUsage);
    consumer->abandon();

    sp<Surface2HGraphicBufferProducer> hgbp = sp<Surface2HGraphicBufferProducer>::make(surface);
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    HStatus status;
    hgbp->connect(listener, HConnectionType::CPU, false,
                  [&](HStatus s, const HGraphicBufferProducer::QueueBufferOutput&) { status = s; });
    EXPECT_EQ(HStatus::NO_INIT, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, AllocateBuffers_ThenDequeue_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, true);
    ASSERT_EQ(HStatus::OK, connectStatus);

    EXPECT_EQ(HStatus::OK, setDequeueTimeout(0));

    // Allocate buffers
    EXPECT_EQ(HStatus::OK, allocateBuffers(100, 100, HAL_PIXEL_FORMAT_RGBA_8888, kDefaultUsage));

    // Disallow allocation and dequeue, which should succeed
    EXPECT_EQ(HStatus::OK, allowAllocation(false));
    HGraphicBufferProducer::DequeueBufferInput input{};
    input.width = 100;
    input.height = 100;
    input.format = HAL_PIXEL_FORMAT_RGBA_8888;
    input.usage = kDefaultUsage;
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    EXPECT_EQ(HStatus::OK, dequeueStatus);
    EXPECT_TRUE(dequeueOutput.bufferNeedsReallocation);
}

TEST_F(Surface2HGraphicBufferProducerTest, GetUniqueId_IsUnique) {
    uint64_t id1 = getUniqueId();
    EXPECT_NE(0u, id1);

    // Create a second BufferQueue and get its ID
    sp<Surface> surface;
    sp<BufferItemConsumer> consumer;
    std::tie(consumer, surface) = BufferItemConsumer::create(kDefaultUsage);
    sp<Surface2HGraphicBufferProducer> hgbp2 = sp<Surface2HGraphicBufferProducer>::make(surface);
    uint64_t id2 = hgbp2->getUniqueId();
    EXPECT_NE(0u, id2);

    EXPECT_NE(id1, id2);
}

TEST_F(Surface2HGraphicBufferProducerTest, DetachNextBuffer_NoFreeBuffer_ReturnsNoMemory) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    // No buffers are available to be detached, so this should fail.
    auto [status, buffer, fence] = detachNextBuffer();
    EXPECT_EQ(HStatus::NO_MEMORY, status);
}

// Native Method Tests

TEST_F(Surface2HGraphicBufferProducerTest, GetConsumerUsage_Succeeds) {
    uint64_t usage = 0;
    status_t status = mHGBP->getConsumerUsage(&usage);
    EXPECT_EQ(NO_ERROR, status);
    EXPECT_EQ(kDefaultUsage, usage);
}

TEST_F(Surface2HGraphicBufferProducerTest, GetConsumerUsage_InvalidArgs) {
    status_t status = mHGBP->getConsumerUsage(nullptr);
    EXPECT_EQ(BAD_VALUE, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, AttachGraphicBuffer_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;
    status_t status = mHGBP->attachGraphicBuffer(&slot, buffer);
    EXPECT_EQ(NO_ERROR, status);
    EXPECT_GE(slot, 0);
    EXPECT_LT(slot, BufferQueueDefs::NUM_BUFFER_SLOTS);
}

TEST_F(Surface2HGraphicBufferProducerTest, AttachGraphicBuffer_InvalidArgs) {
    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;

    EXPECT_EQ(BAD_VALUE, mHGBP->attachGraphicBuffer(nullptr, buffer));
    EXPECT_EQ(BAD_VALUE, mHGBP->attachGraphicBuffer(&slot, nullptr));
}

TEST_F(Surface2HGraphicBufferProducerTest, QueueGraphicBuffer_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;
    ASSERT_EQ(NO_ERROR, mHGBP->attachGraphicBuffer(&slot, buffer));

    SurfaceQueueBufferInput input;
    input.timestamp = 12345;
    input.isAutoTimestamp = 0;
    input.dataSpace = HAL_DATASPACE_UNKNOWN;
    input.crop = Rect(1, 1);
    input.transform = 0;
    input.fence = Fence::NO_FENCE;

    SurfaceQueueBufferOutput output;
    status_t status = mHGBP->queueGraphicBuffer(slot, input, &output);
    EXPECT_EQ(NO_ERROR, status);

    BufferItem item;
    EXPECT_EQ(NO_ERROR, mConsumer->acquireBuffer(&item, 0));
}

TEST_F(Surface2HGraphicBufferProducerTest, CancelBufferSimple_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;
    ASSERT_EQ(NO_ERROR, mHGBP->attachGraphicBuffer(&slot, buffer));

    status_t status = mHGBP->cancelBufferSimple(slot, Fence::NO_FENCE);
    EXPECT_EQ(NO_ERROR, status);
}

// Interaction Tests

TEST_F(Surface2HGraphicBufferProducerTest, NativeAttach_HidlQueue_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;
    ASSERT_EQ(NO_ERROR, mHGBP->attachGraphicBuffer(&slot, buffer));

    HGraphicBufferProducer::QueueBufferInput qInput{};
    qInput.timestamp = 12345;
    qInput.isAutoTimestamp = false;
    qInput.dataSpace = static_cast<int32_t>(HDataSpace::UNKNOWN);
    qInput.crop[0] = 0;
    qInput.crop[1] = 0;
    qInput.crop[2] = 1;
    qInput.crop[3] = 1;
    qInput.transform = 0;
    qInput.fence = ::android::hardware::hidl_handle();

    auto [queueBufferStatus, queueBufferOutput] = queueBuffer(slot, qInput);
    EXPECT_EQ(HStatus::OK, queueBufferStatus);

    BufferItem item;
    EXPECT_EQ(NO_ERROR, mConsumer->acquireBuffer(&item, 0));
}

TEST_F(Surface2HGraphicBufferProducerTest, HidlDequeue_NativeQueue_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    input.width = 1;
    input.height = 1;
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    SurfaceQueueBufferInput qInput;
    qInput.timestamp = 12345;
    qInput.isAutoTimestamp = 0;
    qInput.dataSpace = HAL_DATASPACE_UNKNOWN;
    qInput.crop = Rect(1, 1);
    qInput.transform = 0;
    qInput.fence = Fence::NO_FENCE;

    SurfaceQueueBufferOutput output;
    status_t status = mHGBP->queueGraphicBuffer(slot, qInput, &output);
    EXPECT_EQ(NO_ERROR, status);

    BufferItem item;
    EXPECT_EQ(NO_ERROR, mConsumer->acquireBuffer(&item, 0));
}

TEST_F(Surface2HGraphicBufferProducerTest, HidlDequeue_NativeCancel_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    HGraphicBufferProducer::DequeueBufferInput input{};
    input.width = 1;
    input.height = 1;
    auto [dequeueStatus, slot, dequeueOutput] = dequeueBuffer(input);
    ASSERT_EQ(HStatus::OK, dequeueStatus);

    status_t status = mHGBP->cancelBufferSimple(slot, Fence::NO_FENCE);
    EXPECT_EQ(NO_ERROR, status);
}

TEST_F(Surface2HGraphicBufferProducerTest, NativeAttach_HidlCancel_Succeeds) {
    sp<TestHProducerListener> listener = sp<TestHProducerListener>::make();
    auto [connectStatus, connectOutput] = connect(listener, HConnectionType::CPU, false);
    ASSERT_EQ(HStatus::OK, connectStatus);

    sp<GraphicBuffer> buffer =
            new GraphicBuffer(1, 1, HAL_PIXEL_FORMAT_RGBA_8888, 1, kDefaultUsage);
    int slot = -1;
    ASSERT_EQ(NO_ERROR, mHGBP->attachGraphicBuffer(&slot, buffer));

    HStatus status = cancelBuffer(slot, ::android::hardware::hidl_handle());
    EXPECT_EQ(HStatus::OK, status);
}

} // namespace android
