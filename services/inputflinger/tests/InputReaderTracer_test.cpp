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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/WindowInfo.h>
#include <gui/WindowInfosUpdate.h>
#include <utils/StrongPointer.h>

#include <memory>
#include <vector>

#include "FakeInputTracingBackend.h"
#include "InputReaderTracer.h"
#include "InputTracingBackendInterface.h"

using ::testing::Field;

namespace android {

class TestInputReaderTracer : public InputReaderTracer {
public:
    TestInputReaderTracer(std::shared_ptr<input_trace::InputTracingBackendInterface> backend,
                          sp<gui::WindowInfosListener>& windowInfoListener,
                          const std::vector<gui::WindowInfo>& initialWindowInfos)
          : InputReaderTracer(
                    backend,
                    [&windowInfoListener,
                     &initialWindowInfos](const sp<android::gui::WindowInfosListener>& listener) {
                        windowInfoListener = listener;
                        return initialWindowInfos;
                    },
                    [&windowInfoListener](const sp<android::gui::WindowInfosListener>& listener) {
                        windowInfoListener = nullptr;
                    }) {}
};

class InputReaderTracerTest : public testing::Test {
protected:
    std::shared_ptr<input_trace::VerifyingTrace> mTrace;
    std::shared_ptr<input_trace::FakeInputTracingBackend> mBackend;
    sp<gui::WindowInfosListener> mRegisteredWindowInfoListener;
    gui::WindowInfo mInitialWindow;
    std::vector<gui::WindowInfo> mInjectedInitialWindowInfos;
    std::unique_ptr<TestInputReaderTracer> mTracer;

    void SetUp() override {
        mTrace = std::make_shared<input_trace::VerifyingTrace>();
        mBackend = std::make_shared<input_trace::FakeInputTracingBackend>(mTrace);
        // Add a window initially, to check that the code under test isn't just looking for the
        // presence of any windows at all, without checking for them being secure.
        mInitialWindow.name = "Initial window";
        mInjectedInitialWindowInfos.push_back(mInitialWindow);
    }

    void createTracer() {
        mTracer = std::make_unique<TestInputReaderTracer>(mBackend, mRegisteredWindowInfoListener,
                                                          mInjectedInitialWindowInfos);
    }
};

TEST_F(InputReaderTracerTest, InitialSecureWindowVisible) {
    gui::WindowInfo secureWindow;
    secureWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    mInjectedInitialWindowInfos.push_back(secureWindow);

    createTracer();

    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               true));
    mTrace->verifyExpectedEventsTraced();
}

TEST_F(InputReaderTracerTest, SecureWindowAppearsAfterInitialization) {
    createTracer();
    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               false));

    gui::WindowInfo secureWindow;
    secureWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    mRegisteredWindowInfoListener->onWindowInfosChanged({{mInitialWindow, secureWindow}, {}, 0, 0});

    mTracer->traceRawEvent({.when = 456});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(456,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               true));
    mTrace->verifyExpectedEventsTraced();
}

TEST_F(InputReaderTracerTest, IgnoresInvisibleOrNoInputChannelSecureWindows) {
    createTracer();

    gui::WindowInfo secureInvisibleWindow;
    secureInvisibleWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    secureInvisibleWindow.inputConfig |= gui::WindowInfo::InputConfig::NOT_VISIBLE;

    gui::WindowInfo secureNoInputChannelWindow;
    secureNoInputChannelWindow.inputConfig |= gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY;
    secureNoInputChannelWindow.inputConfig |= gui::WindowInfo::InputConfig::NO_INPUT_CHANNEL;

    mRegisteredWindowInfoListener->onWindowInfosChanged(
            {{mInitialWindow, secureInvisibleWindow, secureNoInputChannelWindow}, {}, 0, 0});

    mTracer->traceRawEvent({.when = 123});

    mTrace->expectAllRawEventsAtTimestampToMatchMetadata(123,
                                                         Field(&input_trace::TracedEventMetadata::
                                                                       isSecure,
                                                               false));
    mTrace->verifyExpectedEventsTraced();
}

} // namespace android
