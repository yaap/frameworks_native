/*
 * Copyright 2021 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/Surface.h>
#include <ui/GraphicBuffer.h>

#include "DisplayHardware/FramebufferSurface.h"
#include "DisplayHardware/LegacyFramebufferSurface.h"
#include "common/FlagManager.h"
#include "compositionengine/DisplaySurface.h"
#include "mock/DisplayHardware/MockHWComposer.h"

namespace android {

using namespace testing;

constexpr PhysicalDisplayId kDisplayId = PhysicalDisplayId::fromPort(123);
constexpr ui::Size kResolution(640, 480);
constexpr ui::Size kMaxSize(1920, 1080);

class FramebufferSurfaceTest : public testing::Test {
public:
    ui::Size limitSize(const ui::Size& size, const ui::Size maxSize) {
        if (FlagManager::getInstance().wb_framebuffersurface2()) {
            return FramebufferSurface::limitSizeInternal(size, maxSize);
        } else {
            return LegacyFramebufferSurface::limitSizeInternal(size, maxSize);
        }
    }

    virtual void SetUp() override {
        if (FlagManager::getInstance().wb_framebuffersurface2()) {
            const auto frameBufferSurface =
                    sp<FramebufferSurface>::make(mMockHwc, kDisplayId, kResolution, kMaxSize);
            mDisplaySurface = frameBufferSurface;
            mRenderSurface = frameBufferSurface->getSurface();
        } else {
            const auto frameBufferSurface =
                    sp<LegacyFramebufferSurface>::make(mMockHwc, kDisplayId, kResolution, kMaxSize);
            mDisplaySurface = frameBufferSurface;
            mRenderSurface = frameBufferSurface->getSurface();
        }
    }

protected:
    StrictMock<mock::HWComposer> mMockHwc;

    sp<Surface> mRenderSurface;
    sp<compositionengine::DisplaySurface> mDisplaySurface;
};

TEST_F(FramebufferSurfaceTest, limitSize) {
    EXPECT_EQ(ui::Size(1920, 1080), limitSize({3840, 2160}, kMaxSize));
    EXPECT_EQ(ui::Size(1920, 1080), limitSize({1920, 1080}, kMaxSize));
    EXPECT_EQ(ui::Size(1920, 1012), limitSize({4096, 2160}, kMaxSize));
    EXPECT_EQ(ui::Size(1080, 1080), limitSize({3840, 3840}, kMaxSize));
    EXPECT_EQ(ui::Size(1280, 720), limitSize({1280, 720}, kMaxSize));
}

TEST_F(FramebufferSurfaceTest, TestTwoFrames) {
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    mRenderSurface->connect(NATIVE_WINDOW_API_CPU, listener);

    uint32_t slotA, slotB;
    sp<GraphicBuffer> clientBufferA, clientBufferB;
    // Set up the mMockHwc to expect two calls to setClientTarget
    EXPECT_CALL(mMockHwc, setClientTarget(static_cast<HalDisplayId>(kDisplayId), _, _, _, _, _))
            .WillOnce(DoAll(SaveArg<1>(&slotA), SaveArg<3>(&clientBufferA), Return(OK)))
            .WillOnce(DoAll(SaveArg<1>(&slotB), SaveArg<3>(&clientBufferB), Return(OK)));

    EXPECT_CALL(mMockHwc, getPresentFence(static_cast<HalDisplayId>(kDisplayId)))
            .WillOnce(Return(Fence::NO_FENCE));

    sp<GraphicBuffer> bufferA;
    sp<Fence> fenceA;
    mDisplaySurface->beginFrame(/*mustRecompose*/ true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&bufferA, &fenceA));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(bufferA, fenceA));
    mDisplaySurface->advanceFrame(/*hdrSdrRatio*/ 1.0);
    mDisplaySurface->onFrameCommitted();

    sp<GraphicBuffer> bufferB;
    sp<Fence> fenceB;
    mDisplaySurface->beginFrame(/*mustRecompose*/ true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&bufferB, &fenceB));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(bufferB, fenceB));
    mDisplaySurface->advanceFrame(/*hdrSdrRatio*/ 1.0);
    mDisplaySurface->onFrameCommitted();

    EXPECT_NE(slotA, slotB);

    // These are technically not necessary, I think, but are useful for understanding the update
    // logic between a legacy and more modern version of FrameBufferSurface.
    EXPECT_NE(bufferA, bufferB);
    EXPECT_EQ(clientBufferA, bufferA);
    EXPECT_EQ(clientBufferB, bufferB);
}

TEST_F(FramebufferSurfaceTest, NoBufferNoHwcUpdate) {
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    mRenderSurface->connect(NATIVE_WINDOW_API_CPU, listener);

    // We don't expect any calls to HWC because no buffers are queued.
    // mMockHwc is a strict mock, so any unexpected calls will fail.

    mDisplaySurface->beginFrame(/*mustRecompose*/ true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    // No buffer is queued.
    mDisplaySurface->advanceFrame(/*hdrSdrRatio*/ 1.0);
    mDisplaySurface->onFrameCommitted();
}

TEST_F(FramebufferSurfaceTest, NoBuffersRenderedLeavesHwcUntouched) {
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    mRenderSurface->connect(NATIVE_WINDOW_API_CPU, listener);

    sp<GraphicBuffer> clientTargetBuffer;
    // Expect one call to HWC for the first frame.
    EXPECT_CALL(mMockHwc, setClientTarget(static_cast<HalDisplayId>(kDisplayId), _, _, _, _, _))
            .WillOnce(DoAll(SaveArg<3>(&clientTargetBuffer), Return(OK)));

    // First frame, with a buffer.
    mDisplaySurface->beginFrame(/*mustRecompose*/ true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(buffer, fence));
    mDisplaySurface->advanceFrame(/*hdrSdrRatio*/ 1.0);
    mDisplaySurface->onFrameCommitted();

    // Second frame, without a buffer.
    mDisplaySurface->beginFrame(/*mustRecompose*/ true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    // No buffer is queued.
    mDisplaySurface->advanceFrame(/*hdrSdrRatio*/ 1.0);
    mDisplaySurface->onFrameCommitted();

    EXPECT_EQ(clientTargetBuffer, buffer);
}

TEST_F(FramebufferSurfaceTest, TestUsageChangeReusesHwcSlot) {
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    mRenderSurface->connect(NATIVE_WINDOW_API_CPU, listener);

    uint32_t slot1, slot2, slot3;
    sp<GraphicBuffer> hwcBuffer1, hwcBuffer2, hwcBuffer3;

    // We need to mock getPresentFence for onFrameCommitted
    EXPECT_CALL(mMockHwc, getPresentFence(static_cast<HalDisplayId>(kDisplayId)))
            .WillRepeatedly(Return(Fence::NO_FENCE));

    // First frame
    EXPECT_CALL(mMockHwc, setClientTarget(static_cast<HalDisplayId>(kDisplayId), _, _, _, _, _))
            .WillOnce(DoAll(SaveArg<1>(&slot1), SaveArg<3>(&hwcBuffer1), Return(OK)));

    mDisplaySurface->beginFrame(true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    sp<GraphicBuffer> buffer1;
    sp<Fence> fence1;
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&buffer1, &fence1));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(buffer1, fence1));
    mDisplaySurface->advanceFrame(1.0);
    mDisplaySurface->onFrameCommitted();

    // Second frame - this will allow the first frame's buffer to be released on commit.
    EXPECT_CALL(mMockHwc, setClientTarget(static_cast<HalDisplayId>(kDisplayId), _, _, _, _, _))
            .WillOnce(DoAll(SaveArg<1>(&slot2), SaveArg<3>(&hwcBuffer2), Return(OK)));

    mDisplaySurface->beginFrame(true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    sp<GraphicBuffer> buffer2;
    sp<Fence> fence2;
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&buffer2, &fence2));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(buffer2, fence2));
    mDisplaySurface->advanceFrame(1.0);
    mDisplaySurface->onFrameCommitted(); // This releases buffer1!

    // Change usage
    uint64_t newUsage = GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER |
            GRALLOC_USAGE_SW_WRITE_OFTEN;
    native_window_set_usage(mRenderSurface.get(), newUsage);

    // Third frame - should trigger reallocation and reuse buffer1's HWC slot.
    EXPECT_CALL(mMockHwc, setClientTarget(static_cast<HalDisplayId>(kDisplayId), _, _, _, _, _))
            .WillOnce(DoAll(SaveArg<1>(&slot3), SaveArg<3>(&hwcBuffer3), Return(OK)));

    mDisplaySurface->beginFrame(true);
    mDisplaySurface->prepareFrame(compositionengine::DisplaySurface::CompositionType::Gpu);
    sp<GraphicBuffer> buffer3;
    sp<Fence> fence3;
    ASSERT_EQ(OK, mRenderSurface->dequeueBuffer(&buffer3, &fence3));
    ASSERT_EQ(OK, mRenderSurface->queueBuffer(buffer3, fence3));

    mDisplaySurface->advanceFrame(1.0);

    // Check that buffer3 is indeed different from buffer2 (reallocated)
    // It might also be different from buffer1 if BufferQueue didn't reuse the slot.
    ASSERT_NE(buffer2->getId(), buffer3->getId());

    // The HWC slot for buffer3 should be the same as slot1 (recently freed).
    EXPECT_EQ(slot1, slot3);
    EXPECT_NE(slot2, slot3);

    // And it should be set to a buffer (non-null hwcBuffer)
    EXPECT_NE(nullptr, hwcBuffer3);
    EXPECT_EQ(buffer3, hwcBuffer3);

    mDisplaySurface->onFrameCommitted();
}

} // namespace android
