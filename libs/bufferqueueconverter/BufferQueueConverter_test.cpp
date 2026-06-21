#include <bufferqueueconverter/BufferQueueConverter.h>

#include <gtest/gtest.h>
#include <gui/bufferqueue/2.0/Surface2HGraphicBufferProducer.h>

#include <gui/BufferItemConsumer.h>
#include <gui/IProducerListener.h>
#include <gui/Surface.h>

using android::hardware::graphics::bufferqueue::V2_0::utils::Surface2HGraphicBufferProducer;

namespace android {

TEST(BufferQueueConverterTest, TestGetNativeWindowFromHGBP) {
    // Test that a null HGraphicBufferProducer results in a null Surface.
    sp<HGraphicBufferProducer> hgbp = nullptr;
    sp<ANativeWindow> nativeWindow = getNativeWindowFromHGBP(hgbp);
    EXPECT_EQ(nativeWindow, nullptr);
}

TEST(BufferQueueConverterTest, TestGetNativeWindowFromHGBP_ValidProducer) {
    // Create a valid BufferQueue and get the producer.
    auto [consumer, surface] = BufferItemConsumer::create(GRALLOC_USAGE_SW_READ_NEVER);
    sp<HGraphicBufferProducer> hgbp = sp<Surface2HGraphicBufferProducer>::make(surface);

    // Test that a valid HGraphicBufferProducer results in a valid ANativeWindow.
    sp<ANativeWindow> nativeWindow = getNativeWindowFromHGBP(hgbp);
    EXPECT_NE(nativeWindow, nullptr);
}

TEST(BufferQueueConverterTest, TestLegacySurfaceHolderPath) {
    // Create a valid BufferQueue and get the producer.
    auto [consumer, surface] = BufferItemConsumer::create(GRALLOC_USAGE_SW_READ_NEVER);
    sp<HGraphicBufferProducer> hgbp = sp<Surface2HGraphicBufferProducer>::make(surface);

    // Test legacy path
    SurfaceHolderUniquePtr surfaceHolder = getSurfaceFromHGBP(hgbp);
    ASSERT_NE(surfaceHolder, nullptr);

    ANativeWindow* nativeWindow = getNativeWindow(surfaceHolder.get());
    EXPECT_NE(nativeWindow, nullptr);
}

}  // namespace android
