/*
 * Copyright 2026 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "AidlComposerHalTest"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <aidl/android/hardware/graphics/composer3/BnComposer.h>
#include <aidl/android/hardware/graphics/composer3/BnComposerClient.h>
#include "DisplayHardware/AidlComposerHal.h"

namespace android {
namespace Hwc2 {

namespace composer3 = aidl::android::hardware::graphics::composer3;
namespace common = aidl::android::hardware::graphics::common;

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

class MockComposer : public composer3::BnComposer {
public:
    MOCK_METHOD(ndk::ScopedAStatus, getCapabilities, (std::vector<composer3::Capability>*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, createClient, (std::shared_ptr<composer3::IComposerClient>*),
                (override));
};

class MockComposerClient : public composer3::BnComposerClient {
public:
    MOCK_METHOD(ndk::ScopedAStatus, createLayer, (int64_t, int32_t, int64_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, createVirtualDisplay,
                (int32_t, int32_t, common::PixelFormat, int32_t, composer3::VirtualDisplay*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, destroyLayer, (int64_t, int64_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, destroyVirtualDisplay, (int64_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, executeCommands,
                (const std::vector<composer3::DisplayCommand>&,
                 std::vector<composer3::CommandResultPayload>*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getActiveConfig, (int64_t, int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getColorModes, (int64_t, std::vector<composer3::ColorMode>*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDataspaceSaturationMatrix,
                (common::Dataspace, std::vector<float>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayAttribute,
                (int64_t, int32_t, composer3::DisplayAttribute, int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayCapabilities,
                (int64_t, std::vector<composer3::DisplayCapability>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayConfigs, (int64_t, std::vector<int32_t>*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayConnectionType,
                (int64_t, composer3::DisplayConnectionType*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayIdentificationData,
                (int64_t, composer3::DisplayIdentification*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayName, (int64_t, std::string*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayVsyncPeriod, (int64_t, int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayedContentSample,
                (int64_t, int64_t, int64_t, composer3::DisplayContentSample*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayedContentSamplingAttributes,
                (int64_t, composer3::DisplayContentSamplingAttributes*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayPhysicalOrientation, (int64_t, common::Transform*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getHdrCapabilities, (int64_t, composer3::HdrCapabilities*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getMaxVirtualDisplayCount, (int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getPerFrameMetadataKeys,
                (int64_t, std::vector<composer3::PerFrameMetadataKey>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getReadbackBufferAttributes,
                (int64_t, composer3::ReadbackBufferAttributes*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getReadbackBufferFence, (int64_t, ::ndk::ScopedFileDescriptor*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getRenderIntents,
                (int64_t, composer3::ColorMode, std::vector<composer3::RenderIntent>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getSupportedContentTypes,
                (int64_t, std::vector<composer3::ContentType>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayDecorationSupport,
                (int64_t, std::optional<common::DisplayDecorationSupport>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getOverlaySupport, (composer3::OverlayProperties*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getHdrConversionCapabilities,
                (std::vector<common::HdrConversionCapability>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setHdrConversionStrategy,
                (const common::HdrConversionStrategy&, common::Hdr*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setRefreshRateChangedCallbackDebugEnabled, (int64_t, bool),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayConfigurations,
                (int64_t, int32_t, std::vector<composer3::DisplayConfiguration>*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, notifyExpectedPresent,
                (int64_t, const composer3::ClockMonotonicTimestamp&, int32_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getMaxLayerPictureProfiles, (int64_t, int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, startHdcpNegotiation,
                (int64_t, const aidl::android::hardware::drm::HdcpLevels&), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getLuts,
                (int64_t, const std::vector<composer3::Buffer>&, std::vector<composer3::Luts>*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, getDisplayKnownVsyncSample, (int64_t, composer3::VsyncSample*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, registerCallback,
                (const std::shared_ptr<composer3::IComposerCallback>&), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setActiveConfig, (int64_t, int32_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setActiveConfigWithConstraints,
                (int64_t, int32_t, const composer3::VsyncPeriodChangeConstraints&,
                 composer3::VsyncPeriodChangeTimeline*),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, setBootDisplayConfig, (int64_t, int32_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, clearBootDisplayConfig, (int64_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, getPreferredBootDisplayConfig, (int64_t, int32_t*), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setAutoLowLatencyMode, (int64_t, bool), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setClientTargetSlotCount, (int64_t, int32_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setColorMode,
                (int64_t, composer3::ColorMode, composer3::RenderIntent), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setContentType, (int64_t, composer3::ContentType), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setDisplayedContentSamplingEnabled,
                (int64_t, bool, composer3::FormatColorComponent, int64_t), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setPowerMode, (int64_t, composer3::PowerMode), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setReadbackBuffer,
                (int64_t, const ::aidl::android::hardware::common::NativeHandle&,
                 const ::ndk::ScopedFileDescriptor&),
                (override));
    MOCK_METHOD(ndk::ScopedAStatus, setVsyncEnabled, (int64_t, bool), (override));
    MOCK_METHOD(ndk::ScopedAStatus, setIdleTimerEnabled, (int64_t, int32_t), (override));
};

class AidlComposerHalTest : public testing::Test {
public:
    void SetUp() override {
        mMockComposer = ndk::SharedRefBase::make<NiceMock<MockComposer>>();
        mMockComposerClient = ndk::SharedRefBase::make<NiceMock<MockComposerClient>>();

        // When createClient() is called, it will always return mMockComposerClient.
        ON_CALL(*mMockComposer, createClient(_))
                .WillByDefault([&](std::shared_ptr<composer3::IComposerClient>* client) {
                    *client = mMockComposerClient;
                    return ndk::ScopedAStatus::ok();
                });

        // ndk::ScopedAStatus is not default constructible to a safe state for isOk(),
        // so we must provide a default value for NiceMock.
        // It is move-only, so we use SetFactory.
        ::testing::DefaultValue<ndk::ScopedAStatus>::SetFactory(
                [] { return ndk::ScopedAStatus::ok(); });
    }

    void TearDown() override { ::testing::DefaultValue<ndk::ScopedAStatus>::Clear(); }

    void init(bool lifecycleBatchCommandSupported) {
        std::vector<composer3::Capability> capabilities;
        if (lifecycleBatchCommandSupported) {
            capabilities.push_back(composer3::Capability::LAYER_LIFECYCLE_BATCH_COMMAND);
        }

        EXPECT_CALL(*mMockComposer, getCapabilities(_))
                .WillOnce(DoAll(SetArgPointee<0>(capabilities), Return(ndk::ScopedAStatus::ok())));

        mAidlComposer = std::make_unique<AidlComposer>(mMockComposer);
    }

    std::shared_ptr<MockComposer> mMockComposer;
    std::shared_ptr<MockComposerClient> mMockComposerClient;
    std::unique_ptr<AidlComposer> mAidlComposer;
};

TEST_F(AidlComposerHalTest, destroyLayer_NoBatchSupport) {
    init(false);

    const Display display = static_cast<Display>(1);
    const Layer layer = static_cast<Layer>(123);
    const int64_t displayId = 1;
    const int64_t layerId = 123;

    // We need to add the display so writer exists
    mAidlComposer->onHotplugConnect(display);

    EXPECT_CALL(*mMockComposerClient, destroyLayer(displayId, layerId))
            .WillOnce(Return(ndk::ScopedAStatus::ok()));

    auto error = mAidlComposer->destroyLayer(display, layer);
    EXPECT_EQ(error, Error::NONE);
}

TEST_F(AidlComposerHalTest, destroyLayer_BatchSupport) {
    init(true);

    const Display display = static_cast<Display>(1);
    const Layer layer = static_cast<Layer>(123);
    const int64_t displayId = 1;
    const int64_t layerId = 123;

    // We need to add the display so writer exists
    mAidlComposer->onHotplugConnect(display);

    // destroyLayer should NOT be called directly
    EXPECT_CALL(*mMockComposerClient, destroyLayer(_, _)).Times(0);

    auto error = mAidlComposer->destroyLayer(display, layer);
    EXPECT_EQ(error, Error::NONE);

    // Now execute commands and verify the batch command
    EXPECT_CALL(*mMockComposerClient, executeCommands(_, _))
            .WillOnce([&](const std::vector<composer3::DisplayCommand>& commands,
                          std::vector<composer3::CommandResultPayload>*) {
                if (commands.empty())
                    return ndk::ScopedAStatus::fromServiceSpecificError(
                            static_cast<int32_t>(Error::BAD_PARAMETER));
                const auto& cmd = commands[0];
                if (cmd.display != displayId)
                    return ndk::ScopedAStatus::fromServiceSpecificError(
                            static_cast<int32_t>(Error::BAD_DISPLAY));

                bool foundDestroy = false;
                for (const auto& layerCmd : cmd.layers) {
                    if (layerCmd.layer == layerId &&
                        layerCmd.layerLifecycleBatchCommandType ==
                                composer3::LayerLifecycleBatchCommandType::DESTROY) {
                        foundDestroy = true;
                        break;
                    }
                }

                if (foundDestroy) return ndk::ScopedAStatus::ok();
                return ndk::ScopedAStatus::fromServiceSpecificError(
                        static_cast<int32_t>(Error::BAD_LAYER));
            });

    error = mAidlComposer->executeCommands(display);
    EXPECT_EQ(error, Error::NONE);
}

TEST_F(AidlComposerHalTest, getHdrCapabilities_ReturnsUnsupported) {
    init(false);
    const Display display = static_cast<Display>(1);
    const int64_t displayId = 1;

    EXPECT_CALL(*mMockComposerClient, getHdrCapabilities(displayId, _))
            .WillOnce(Return(ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Error::UNSUPPORTED))));

    std::vector<Hdr> types;
    float maxLuminance = 0.0f;
    float maxAverageLuminance = 0.0f;
    float minLuminance = 0.0f;

    // Verifies that getHdrCapabilities correctly handles the UNSUPPORTED error
    // returned by the HAL via handleStatus.
    auto error = mAidlComposer->getHdrCapabilities(display, &types, &maxLuminance,
                                                   &maxAverageLuminance, &minLuminance);
    EXPECT_EQ(error, Error::UNSUPPORTED);
}

TEST_F(AidlComposerHalTest, getPreferredBootDisplayConfig_ReturnsUnsupported) {
    init(false);
    const Display display = static_cast<Display>(1);
    const int64_t displayId = 1;

    EXPECT_CALL(*mMockComposerClient, getPreferredBootDisplayConfig(displayId, _))
            .WillOnce(Return(ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Error::UNSUPPORTED))));

    Config config;

    // Verifies that getPreferredBootDisplayConfig correctly handles the UNSUPPORTED error
    // returned by the HAL via handleStatus.
    auto error = mAidlComposer->getPreferredBootDisplayConfig(display, &config);
    EXPECT_EQ(error, Error::UNSUPPORTED);
}

TEST_F(AidlComposerHalTest, getHdrConversionCapabilities_ReturnsUnsupported) {
    init(false);

    EXPECT_CALL(*mMockComposerClient, getHdrConversionCapabilities(_))
            .WillOnce(Return(ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Error::UNSUPPORTED))));

    std::vector<common::HdrConversionCapability> capabilities;

    // Verifies that getHdrConversionCapabilities correctly handles the UNSUPPORTED error
    // returned by the HAL via handleStatus.
    auto error = mAidlComposer->getHdrConversionCapabilities(&capabilities);
    EXPECT_EQ(error, Error::UNSUPPORTED);
}

TEST_F(AidlComposerHalTest, getMaxLayerPictureProfiles_ReturnsUnsupported) {
    init(false);
    const Display display = static_cast<Display>(1);
    const int64_t displayId = 1;

    EXPECT_CALL(*mMockComposerClient, getMaxLayerPictureProfiles(displayId, _))
            .WillOnce(Return(ndk::ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(Error::UNSUPPORTED))));

    int32_t maxProfiles = 0;

    // Verifies that getMaxLayerPictureProfiles correctly handles the UNSUPPORTED error
    // returned by the HAL via handleStatus.
    auto error = mAidlComposer->getMaxLayerPictureProfiles(display, &maxProfiles);
    EXPECT_EQ(error, Error::UNSUPPORTED);
}

} // namespace Hwc2
} // namespace android
