/*
 * Copyright 2024 The Android Open Source Project
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
#define LOG_TAG "LibSurfaceFlingerUnittests"

#include "Display/DisplayModeController.h"
#include "Display/DisplaySnapshot.h"
#include "DisplayHardware/HWComposer.h"
#include "DisplayHardware/Hal.h"
#include "DisplayIdentificationTestHelpers.h"
#include "FpsOps.h"
#include "mock/DisplayHardware/MockComposer.h"
#include "mock/DisplayHardware/MockDisplayMode.h"
#include "mock/MockFrameRateMode.h"

#include <com_android_graphics_surfaceflinger_flags.h>
#include <common/test/FlagUtils.h>
#include <ftl/fake_guard.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ui/ScreenPartStatus.h>

#define EXPECT_DISPLAY_MODE_REQUEST(expected, request)                              \
    EXPECT_FRAME_RATE_MODE(expected.mode.modePtr, expected.mode.fps, request.mode); \
    EXPECT_EQ(expected.emitEvent, request.emitEvent)

#define EXPECT_DISPLAY_MODE_REQUEST_OPT(expected, requestOpt) \
    ASSERT_TRUE(requestOpt);                                  \
    EXPECT_DISPLAY_MODE_REQUEST(expected, (*requestOpt))

namespace android::display {
namespace {

namespace flags = com::android::graphics::surfaceflinger::flags;
namespace hal = android::hardware::graphics::composer::hal;

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

enum class ModeSetHal { DisplayCommand, SetActiveConfig };

class DisplayModeControllerTest : public testing::Test {
public:
    using Action = DisplayModeController::DesiredModeAction;

    void SetUp() override {
        mDmc.setHwComposer(mComposer.get());
        mDmc.setActiveModeListener(
                [this](PhysicalDisplayId displayId, Fps vsyncRate, Fps renderFps) {
                    mActiveModeListener.Call(displayId, vsyncRate, renderFps);
                });

        constexpr uint8_t kPort = 111;
        EXPECT_CALL(*mComposerHal, getDisplayIdentificationData(kHwcDisplayId, _, _, _))
                .WillOnce(DoAll(SetArgPointee<1>(kPort), SetArgPointee<2>(getInternalEdid()),
                                Return(hal::Error::NONE)));

        EXPECT_CALL(*mComposerHal, getDisplayConnectionType(kHwcDisplayId, _))
                .WillOnce(DoAll(SetArgPointee<1>(
                                        hal::IComposerClient::DisplayConnectionType::INTERNAL),
                                Return(hal::V2_4::Error::NONE)));

        EXPECT_CALL(*mComposerHal, setClientTargetSlotCount(kHwcDisplayId));
        EXPECT_CALL(*mComposerHal,
                    setVsyncEnabled(kHwcDisplayId, hal::IComposerClient::Vsync::DISABLE));
        EXPECT_CALL(*mComposerHal, onHotplugConnect(kHwcDisplayId));

        const auto infoOpt =
                mComposer->onHotplug(kHwcDisplayId, HWComposer::HotplugEvent::Connected);
        ASSERT_TRUE(infoOpt);

        mDisplayId = infoOpt->id;
        mDisplaySnapshotOpt.emplace(mDisplayId, infoOpt->port,
                                    android::ScreenPartStatus::UNSUPPORTED,
                                    ui::DisplayConnectionType::Internal,
                                    makeModes(kMode60, kMode90, kMode120), ui::ColorModes{},
                                    std::nullopt);

        ftl::FakeGuard guard(kMainThreadContext);
        mDmc.registerDisplay(*mDisplaySnapshotOpt, kModeId60,
                             scheduler::RefreshRateSelector::Config{});
    }

protected:
    PhysicalDisplayId initiateSecondaryDisplay() {
        constexpr uint8_t kPort = 112;
        EXPECT_CALL(*mComposerHal, getDisplayIdentificationData(kSecondaryHwcDisplayId, _, _, _))
                .WillOnce(DoAll(SetArgPointee<1>(kPort), SetArgPointee<2>(getInternalEdid()),
                                Return(hal::Error::NONE)));

        EXPECT_CALL(*mComposerHal, getDisplayConnectionType(kSecondaryHwcDisplayId, _))
                .WillOnce(DoAll(SetArgPointee<1>(
                                        hal::IComposerClient::DisplayConnectionType::EXTERNAL),
                                Return(hal::V2_4::Error::NONE)));

        EXPECT_CALL(*mComposerHal, setClientTargetSlotCount(kSecondaryHwcDisplayId));
        EXPECT_CALL(*mComposerHal,
                    setVsyncEnabled(kSecondaryHwcDisplayId, hal::IComposerClient::Vsync::DISABLE));
        EXPECT_CALL(*mComposerHal, onHotplugConnect(kSecondaryHwcDisplayId));

        const auto infoOpt =
                mComposer->onHotplug(kSecondaryHwcDisplayId, HWComposer::HotplugEvent::Connected);

        auto displayId = infoOpt->id;
        mSecondaryDisplaySnapshotOpt.emplace(displayId, infoOpt->port,
                                             android::ScreenPartStatus::UNSUPPORTED,
                                             ui::DisplayConnectionType::External,
                                             makeModes(kMode60, kMode90, kMode120),
                                             ui::ColorModes{}, std::nullopt);

        ftl::FakeGuard guard(kMainThreadContext);
        mDmc.registerDisplay(*mSecondaryDisplaySnapshotOpt, kModeId60,
                             scheduler::RefreshRateSelector::Config{});
        return displayId;
    }

    hal::VsyncPeriodChangeConstraints expectModeSet(
            const DisplayModeRequest& request, hal::VsyncPeriodChangeTimeline& timeline,
            bool subsequent = false, ModeSetHal modeSetHal = ModeSetHal::SetActiveConfig,
            hal::Error halError = hal::Error::NONE) {
        EXPECT_CALL(*mComposerHal, isDisplayCommandModesetSupported())
                .WillOnce(Return(modeSetHal == ModeSetHal::DisplayCommand));

        const hal::VsyncPeriodChangeConstraints constraints{
                .desiredTimeNanos = systemTime(),
                .seamlessRequired = false,
        };

        const hal::HWConfigId hwcModeId = request.mode.modePtr->getHwcId();

        if (modeSetHal != ModeSetHal::DisplayCommand) {
            if (!subsequent) {
                EXPECT_CALL(*mComposerHal, getDisplayConnectionType(kHwcDisplayId, _))
                        .WillOnce(DoAll(SetArgPointee<1>(hal::IComposerClient::
                                                                 DisplayConnectionType::INTERNAL),
                                        Return(hal::V2_4::Error::NONE)));
            }
            EXPECT_CALL(*mComposerHal,
                        isSupported(Hwc2::Composer::OptionalFeature::RefreshRateSwitching))
                    .WillOnce(Return(true));
            EXPECT_CALL(*mComposerHal,
                        setActiveConfigWithConstraints(kHwcDisplayId, hwcModeId, constraints, _))
                    .WillOnce(DoAll(SetArgPointee<3>(timeline), Return(halError)));
        } else {
            EXPECT_CALL(*mComposerHal, setDisplayMode(kHwcDisplayId, hwcModeId, false))
                    .WillOnce(Return(halError));
        }

        return constraints;
    }

    void finalizeModeSet(PhysicalDisplayId displayId) {
        ftl::FakeGuard guard(kMainThreadContext);
        EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getPendingMode(displayId));

        EXPECT_CALL(mActiveModeListener, Call(displayId, 90_Hz, 90_Hz)).Times(1);

        auto modeChange = mDmc.finalizeModeChange(displayId);

        auto* changePtr = std::get_if<DisplayModeController::RefreshRateChange>(&modeChange);
        ASSERT_TRUE(changePtr);
        EXPECT_DISPLAY_MODE_REQUEST(kDesiredMode90, changePtr->activeMode);
    }

    static constexpr hal::HWDisplayId kHwcDisplayId = 1234;
    static constexpr hal::HWDisplayId kSecondaryHwcDisplayId = 5678;

    Hwc2::mock::Composer* mComposerHal = new testing::StrictMock<Hwc2::mock::Composer>();
    const std::unique_ptr<HWComposer> mComposer{
            std::make_unique<impl::HWComposer>(std::unique_ptr<Hwc2::Composer>(mComposerHal))};

    testing::MockFunction<void(PhysicalDisplayId, Fps, Fps)> mActiveModeListener;

    DisplayModeController mDmc;

    PhysicalDisplayId mDisplayId;
    std::optional<DisplaySnapshot> mDisplaySnapshotOpt;
    std::optional<DisplaySnapshot> mSecondaryDisplaySnapshotOpt;

    static constexpr DisplayModeId kModeId60{0};
    static constexpr DisplayModeId kModeId90{1};
    static constexpr DisplayModeId kModeId120{2};
    static constexpr DisplayModeId kModeId90_4K{3};

    static inline const ftl::NonNull<DisplayModePtr> kMode60 =
            ftl::as_non_null(mock::createDisplayMode(kModeId60, 60_Hz));
    static inline const ftl::NonNull<DisplayModePtr> kMode90 =
            ftl::as_non_null(mock::createDisplayMode(kModeId90, 90_Hz));
    static inline const ftl::NonNull<DisplayModePtr> kMode120 =
            ftl::as_non_null(mock::createDisplayMode(kModeId120, 120_Hz));
    static inline const ftl::NonNull<DisplayModePtr> kMode90_4K =
            ftl::as_non_null(mock::createDisplayMode(kModeId90_4K, 90_Hz, 1, mock::kResolution4K));

    static inline const DisplayModeRequest kDesiredMode30{{30_Hz, kMode60}, .emitEvent = false};
    static inline const DisplayModeRequest kDesiredMode60{{60_Hz, kMode60}, .emitEvent = true};
    static inline const DisplayModeRequest kDesiredMode90{{90_Hz, kMode90}, .emitEvent = false};
    static inline const DisplayModeRequest kDesiredMode120{{120_Hz, kMode120}, .emitEvent = true};
    static inline const DisplayModeRequest kDesiredMode90_4K{{90_Hz, kMode90_4K},
                                                             .emitEvent = true};
};

TEST_F(DisplayModeControllerTest, setDesiredModeToActiveMode) {
    EXPECT_CALL(mActiveModeListener, Call(_, _, _)).Times(0);

    EXPECT_EQ(Action::None, mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode60)));
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, setDesiredMode) {
    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    EXPECT_EQ(Action::MergeDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode120)));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode120, mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, clearDesiredMode) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, false);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_TRUE(mDmc.getDesiredMode(mDisplayId));

    mDmc.clearDesiredMode(mDisplayId);
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, takeDesiredModeIfMatches) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::synced_resolution_switch, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(2);

    // Change refresh rate.
    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_EQ(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    EXPECT_EQ(kDesiredMode90, mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p));

    // Change resolution.
    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90_4K)));
    EXPECT_EQ(kDesiredMode90_4K, mDmc.getDesiredMode(mDisplayId));

    // The desired mode must match the expected resolution.
    EXPECT_FALSE(mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p));

    EXPECT_EQ(kDesiredMode90_4K, mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution4K));
}

TEST_F(DisplayModeControllerTest, initiateModeChangeLegacy) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, false);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));
    auto modeRequest = kDesiredMode90;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints = expectModeSet(modeRequest, timeline);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getPendingMode(mDisplayId));

    mDmc.clearDesiredMode(mDisplayId);
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, rejectModeChangeLegacy) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, false);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));
    auto modeRequest = kDesiredMode90;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints = expectModeSet(modeRequest, timeline, false,
                                           ModeSetHal::SetActiveConfig, hal::Error::CONFIG_FAILED);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Rejected,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));

    EXPECT_FALSE(mDmc.isModeSetPending(mDisplayId));

    // Caller is responsible for clearing on failure.
    mDmc.clearDesiredMode(mDisplayId);
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, initiateModeChange) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints = expectModeSet(modeRequest, timeline);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    finalizeModeSet(mDisplayId);
}

TEST_F(DisplayModeControllerTest, rejectModeChange) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints = expectModeSet(modeRequest, timeline, false,
                                           ModeSetHal::SetActiveConfig, hal::Error::CONFIG_FAILED);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Rejected,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));

    EXPECT_FALSE(mDmc.getPendingMode(mDisplayId));
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, initiateModeChangeDisplayCommand) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::display_command_modeset, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints =
            expectModeSet(modeRequest, timeline, false, ModeSetHal::DisplayCommand);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    finalizeModeSet(mDisplayId);
}

TEST_F(DisplayModeControllerTest, initiateModeChangeMultiDisplayCommand)
REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::display_command_modeset, true);
    SET_FLAG_FOR_TEST(flags::modeset_multi_display, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    std::vector<std::pair<hal::HWDisplayId, hal::HWConfigId>> displayModes(
            {{kHwcDisplayId, modeRequest.mode.modePtr->getHwcId()}});

    EXPECT_CALL(*mComposerHal, setDisplayModes(displayModes, false))
            .WillOnce(Return(hal::Error::NONE));

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(ftl::init::map(mDisplayId, std::move(modeRequest))));
    finalizeModeSet(mDisplayId);
}

TEST_F(DisplayModeControllerTest, initiateMultiModeChangeMultiDisplayCommand)
REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::display_command_modeset, true);
    SET_FLAG_FOR_TEST(flags::modeset_multi_display, true);

    auto secondaryDisplayId = initiateSecondaryDisplay();

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);
    EXPECT_CALL(mActiveModeListener, Call(secondaryDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(secondaryDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(secondaryDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;
    const auto secondaryDesiredModeOpt =
            mDmc.takeDesiredModeIfMatches(secondaryDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, secondaryDesiredModeOpt);
    auto secondaryModeRequest = *secondaryDesiredModeOpt;

    std::vector<std::pair<hal::HWDisplayId, hal::HWConfigId>> displayModes(
            {{kHwcDisplayId, modeRequest.mode.modePtr->getHwcId()},
             {kSecondaryHwcDisplayId, secondaryModeRequest.mode.modePtr->getHwcId()}});

    EXPECT_CALL(*mComposerHal, setDisplayModes(displayModes, false))
            .WillOnce(Return(hal::Error::NONE));

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(
                      ftl::init::map(mDisplayId,
                                     std::move(modeRequest))(secondaryDisplayId,
                                                             std::move(secondaryModeRequest))));
    finalizeModeSet(mDisplayId);
    finalizeModeSet(secondaryDisplayId);

    mDmc.unregisterDisplay(secondaryDisplayId);
}

TEST_F(DisplayModeControllerTest, rejectMultiModeChange) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::display_command_modeset, true);
    SET_FLAG_FOR_TEST(flags::modeset_multi_display, true);

    auto secondaryDisplayId = initiateSecondaryDisplay();

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);
    EXPECT_CALL(mActiveModeListener, Call(secondaryDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(secondaryDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(secondaryDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;
    const auto secondaryDesiredModeOpt =
            mDmc.takeDesiredModeIfMatches(secondaryDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, secondaryDesiredModeOpt);
    auto secondaryModeRequest = *secondaryDesiredModeOpt;

    std::vector<std::pair<hal::HWDisplayId, hal::HWConfigId>> displayModes(
            {{kHwcDisplayId, modeRequest.mode.modePtr->getHwcId()},
             {kSecondaryHwcDisplayId, secondaryModeRequest.mode.modePtr->getHwcId()}});

    EXPECT_CALL(*mComposerHal, setDisplayModes(displayModes, false))
            .WillOnce(Return(hal::Error::CONFIG_FAILED));

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Rejected,
              mDmc.initiateModeChange(
                      ftl::init::map(mDisplayId,
                                     std::move(modeRequest))(secondaryDisplayId,
                                                             std::move(secondaryModeRequest))));
    EXPECT_FALSE(mDmc.getPendingMode(mDisplayId));
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
    EXPECT_FALSE(mDmc.getPendingMode(secondaryDisplayId));
    EXPECT_FALSE(mDmc.getDesiredMode(secondaryDisplayId));
    mDmc.unregisterDisplay(secondaryDisplayId);
}

TEST_F(DisplayModeControllerTest, rejectModeChangeDisplayCommand) REQUIRES(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, true);
    SET_FLAG_FOR_TEST(flags::display_command_modeset, true);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    hal::VsyncPeriodChangeTimeline timeline;
    const auto constraints = expectModeSet(modeRequest, timeline, false, ModeSetHal::DisplayCommand,
                                           hal::Error::CONFIG_FAILED);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Rejected,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));

    EXPECT_FALSE(mDmc.getPendingMode(mDisplayId));
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, initiateRenderRateSwitch) {
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 30_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateRenderRateSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode30)));
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, initiateDisplayModeSwitchLegacy)
FTL_FAKE_GUARD(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, false);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));
    auto modeRequest = kDesiredMode90;

    hal::VsyncPeriodChangeTimeline timeline;
    auto constraints = expectModeSet(modeRequest, timeline);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getPendingMode(mDisplayId));

    EXPECT_EQ(Action::MergeDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode120)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getPendingMode(mDisplayId));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode120, mDmc.getDesiredMode(mDisplayId));
    modeRequest = kDesiredMode120;

    constexpr bool kSubsequent = true;
    constraints = expectModeSet(modeRequest, timeline, kSubsequent);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode120, mDmc.getPendingMode(mDisplayId));

    mDmc.clearDesiredMode(mDisplayId);
    EXPECT_FALSE(mDmc.getDesiredMode(mDisplayId));
}

TEST_F(DisplayModeControllerTest, initiateDisplayModeSwitch) FTL_FAKE_GUARD(kMainThreadContext) {
    SET_FLAG_FOR_TEST(flags::modeset_state_machine, false);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 60_Hz, 60_Hz)).Times(1);

    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode90)));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getDesiredMode(mDisplayId));

    const auto desiredModeOpt = mDmc.takeDesiredModeIfMatches(mDisplayId, mock::kResolution1080p);
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, desiredModeOpt);
    auto modeRequest = *desiredModeOpt;

    hal::VsyncPeriodChangeTimeline timeline;
    auto constraints = expectModeSet(modeRequest, timeline);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));
    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode90, mDmc.getPendingMode(mDisplayId));

    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 90_Hz, 90_Hz)).Times(1);

    auto modeChange = mDmc.finalizeModeChange(mDisplayId);
    auto* changePtr = std::get_if<DisplayModeController::RefreshRateChange>(&modeChange);
    ASSERT_TRUE(changePtr);
    EXPECT_DISPLAY_MODE_REQUEST(kDesiredMode90, changePtr->activeMode);

    // Called because setDesiredMode resets the render rate to the active refresh rate.
    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 90_Hz, 90_Hz)).Times(1);

    // The latest request should override the desired mode.
    EXPECT_EQ(Action::InitiateDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode60)));
    EXPECT_EQ(Action::MergeDisplayModeSwitch,
              mDmc.setDesiredMode(mDisplayId, DisplayModeRequest(kDesiredMode120)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode120, mDmc.getDesiredMode(mDisplayId));

    // The mode change should not yet be pending.
    EXPECT_FALSE(mDmc.getPendingMode(mDisplayId));
    EXPECT_TRUE(std::holds_alternative<DisplayModeController::NoModeChange>(
            mDmc.finalizeModeChange(mDisplayId)));

    EXPECT_DISPLAY_MODE_REQUEST_OPT(kDesiredMode120, mDmc.getDesiredMode(mDisplayId));
    modeRequest = kDesiredMode120;

    constexpr bool kSubsequent = true;
    constraints = expectModeSet(modeRequest, timeline, kSubsequent);

    EXPECT_EQ(DisplayModeController::ModeChangeResult::Changed,
              mDmc.initiateModeChange(mDisplayId, std::move(modeRequest), constraints, timeline));

    EXPECT_CALL(mActiveModeListener, Call(mDisplayId, 120_Hz, 120_Hz)).Times(1);

    modeChange = mDmc.finalizeModeChange(mDisplayId);
    changePtr = std::get_if<DisplayModeController::RefreshRateChange>(&modeChange);
    ASSERT_TRUE(changePtr);
    EXPECT_DISPLAY_MODE_REQUEST(kDesiredMode120, changePtr->activeMode);
}

} // namespace
} // namespace android::display
