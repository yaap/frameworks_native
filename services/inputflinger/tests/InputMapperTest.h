/*
 * Copyright 2022 The Android Open Source Project
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

#include <list>
#include <memory>

#include <InputDevice.h>
#include <InputMapper.h>
#include <android-base/result.h>
#include <ftl/flags.h>
#include <gmock/gmock.h>
#include <utils/StrongPointer.h>

#include "FakeEventHub.h"
#include "FakeInputReaderPolicy.h"
#include "InputReaderBase.h"
#include "InstrumentedInputReader.h"
#include "InterfaceMocks.h"
#include "NotifyArgs.h"
#include "TestConstants.h"
#include "TestInputListener.h"
#include "TestInputQueue.h"
#include "input/Input.h"
#include "input/InputVerifier.h"
#include "input/PropertyMap.h"

namespace android {

class InputMapperUnitTest : public testing::Test {
protected:
    static constexpr RawDeviceId EVENTHUB_ID = 1;
    static constexpr DeviceId DEVICE_ID = END_RESERVED_ID + 1000;
    virtual void SetUp() override { SetUp(/*bus=*/0, /*isExternal=*/false); }
    virtual void SetUp(int bus, bool isExternal);

    void setupAxis(int axis, bool valid, int32_t min, int32_t max, int32_t resolution,
                   int32_t flat = 0, int32_t fuzz = 0);

    void expectScanCodes(bool present, std::set<int> scanCodes);

    void setScanCodeState(KeyState state, std::set<int> scanCodes);

    void setKeyCodeState(KeyState state, std::set<int> keyCodes);

    void setSwitchState(int32_t state, std::set<int32_t> switchCodes);

    void process(int32_t type, int32_t code, int32_t value);
    void process(nsecs_t when, int32_t type, int32_t code, int32_t value);
    virtual void process(nsecs_t when, nsecs_t readTime, int32_t type, int32_t code, int32_t value);

    virtual void processArgs(const std::list<NotifyArgs>& args);

    InputDeviceIdentifier mIdentifier;
    MockEventHubInterface mMockEventHub;
    sp<FakeInputReaderPolicy> mFakePolicy;
    MockInputReaderContext mMockInputReaderContext;
    std::unique_ptr<MockInputDevice> mDevice;
    TestInputQueue mFakeListener;

    std::unique_ptr<InputDeviceContext> mDeviceContext;
    InputReaderConfiguration mReaderConfiguration;
    // The mapper should be created by the subclasses.
    std::unique_ptr<InputMapper> mMapper;
    PropertyMap mPropertyMap;
};

/**
 * A variant of InputMapperUnitTest that also runs NotifyMotionArgs produced by the mapper through
 * an InputVerifier.
 *
 * When using this class, all args produced by the mapper need to be run through the verifier, so
 * that it gets a full view of the input stream. This means that wrapper functions such as
 * reconfigureMapper must be used instead of calling functions on the mapper directly.
 */
class VerifyingInputMapperUnitTest : public InputMapperUnitTest {
protected:
    VerifyingInputMapperUnitTest();

    using InputMapperUnitTest::process;
    virtual void process(nsecs_t when, nsecs_t readTime, int32_t type, int32_t code,
                         int32_t value) override;
    virtual void processArgs(const std::list<NotifyArgs>& args) override;

    void reconfigureMapper(nsecs_t when, const InputReaderConfiguration& config,
                           ConfigurationChanges changes);

    void resetMapper(nsecs_t when);

private:
    void processMotionArgs(const std::list<NotifyArgs>& args);

    InputVerifier mVerifier;
};

/**
 * Deprecated - use InputMapperUnitTest instead.
 */
class InputMapperTest : public testing::Test {
protected:
    static const char* DEVICE_NAME;
    static const char* DEVICE_LOCATION;
    static constexpr DeviceId DEVICE_ID = END_RESERVED_ID + 1000;
    static constexpr int32_t DEVICE_GENERATION = 2;
    static constexpr int32_t DEVICE_CONTROLLER_NUMBER = 0;
    static const ftl::Flags<InputDeviceClass> DEVICE_CLASSES;
    static constexpr RawDeviceId EVENTHUB_ID = 1;

    std::shared_ptr<FakeEventHub> mFakeEventHub;
    sp<FakeInputReaderPolicy> mFakePolicy;
    std::unique_ptr<TestInputListener> mFakeListener;
    std::unique_ptr<InstrumentedInputReader> mReader;
    std::shared_ptr<InputDevice> mDevice;

    virtual void SetUp(ftl::Flags<InputDeviceClass> classes, int bus = 0);
    void SetUp() override;
    void TearDown() override;

    void addConfigurationProperty(const char* key, const char* value);
    std::list<NotifyArgs> configureDevice(ConfigurationChanges changes);
    std::shared_ptr<InputDevice> newDevice(int32_t deviceId, const std::string& name,
                                           const std::string& location, int32_t eventHubId,
                                           ftl::Flags<InputDeviceClass> classes, int bus = 0);

    template <class T, typename... Args>
    T& constructAndAddMapper(Args... args) {
        // ensure a device entry exists for this eventHubId
        mDevice->addEmptyEventHubDevice(EVENTHUB_ID);

        auto& mapper =
                mDevice->constructAndAddMapper<T>(EVENTHUB_ID,
                                                  mFakePolicy->getReaderConfiguration(), args...);
        configureDevice(/*changes=*/{});
        return mapper;
    }

    void setDisplayInfoAndReconfigure(ui::LogicalDisplayId displayId, int32_t width, int32_t height,
                                      ui::Rotation orientation, const std::string& uniqueId,
                                      std::optional<uint8_t> physicalPort,
                                      ViewportType viewportType);
    void clearViewports();
    std::list<NotifyArgs> process(InputMapper& mapper, nsecs_t when, nsecs_t readTime, int32_t type,
                                  int32_t code, int32_t value);
    void resetMapper(InputMapper& mapper, nsecs_t when);

    std::list<NotifyArgs> handleTimeout(InputMapper& mapper, nsecs_t when);
};

void assertMotionRange(const InputDeviceInfo& info, int32_t axis, uint32_t source, float min,
                       float max, float flat, float fuzz);

void assertPointerCoords(const PointerCoords& coords, float x, float y, float pressure, float size,
                         float touchMajor, float touchMinor, float toolMajor, float toolMinor,
                         float orientation, float distance, float scaledAxisEpsilon = 1.f);

} // namespace android
