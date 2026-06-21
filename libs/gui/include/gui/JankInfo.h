/*
 * Copyright 2020 The Android Open Source Project
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

namespace android {

// Jank type tracked by SurfaceFlinger(SF) for Perfetto tracing and telemetry.
enum JankType {
    // No Jank
    None = 0x0,
    // Jank that occurs in the layers below SurfaceFlinger
    DisplayHAL = 0x1,
    // SF took too long on the CPU; deadline missed during HWC
    SurfaceFlingerCpuDeadlineMissed = 0x2,
    // SF took too long on the GPU; deadline missed during GPU composition
    SurfaceFlingerGpuDeadlineMissed = 0x4,
    // Either App or GPU took too long on the frame
    AppDeadlineMissed = 0x8,
    // Vsync predictions have drifted beyond the threshold from the actual HWVsync
    PredictionError = 0x10,
    // Janks caused due to the time SF was scheduled to work on the frame
    // Example: SF woke up too early and latched a buffer resulting in an early present
    SurfaceFlingerScheduling = 0x20,
    // A buffer is said to be stuffed if it was expected to be presented on a vsync but was
    // presented later because the previous buffer was presented in its expected vsync. This
    // usually happens if there is an unexpectedly long frame causing the rest of the buffers
    // to enter a stuffed state.
    BufferStuffing = 0x40,
    // Jank due to unknown reasons.
    Unknown = 0x80,
    // SF is said to be stuffed if the previous frame ran longer than expected resulting in the case
    // where the previous frame was presented in the current frame's expected vsync. This pushes the
    // current frame to the next vsync. The behavior is similar to BufferStuffing.
    SurfaceFlingerStuffing = 0x100,
    // Frame was dropped, as a newer frame was ready and replaced this frame.
    Dropped = 0x200,
    // Frame was not presented on time, but it is not causing a percivable jank as it is not
    // part of an animation (e.g. a cursor blinking).
    NonAnimating = 0x400,
    // Frame vsync time was modified by the app.
    AppResyncedJitter = 0x800,
    // Display is not on (off or doze).
    DisplayNotOn = 0x1000,
    // Display mode change is in progress.
    DisplayModeChangeInProgress = 0x2000,
    // Display power mode change is in progress.
    DisplayPowerModeChangeInProgress = 0x4000,
};

// IMPORTANT: update this whenever a new value is added to JankType.
constexpr int kJankTypeAll = JankType::None | JankType::DisplayHAL |
        JankType::SurfaceFlingerCpuDeadlineMissed | JankType::SurfaceFlingerGpuDeadlineMissed |
        JankType::AppDeadlineMissed | JankType::PredictionError |
        JankType::SurfaceFlingerScheduling | JankType::BufferStuffing | JankType::Unknown |
        JankType::SurfaceFlingerStuffing | JankType::Dropped | JankType::NonAnimating |
        JankType::AppResyncedJitter | JankType::DisplayNotOn |
        JankType::DisplayModeChangeInProgress | JankType::DisplayPowerModeChangeInProgress;

// Jank severity type tracked by SurfaceFlinger(SF) for Perfetto tracing and telemetry.
enum class JankSeverityType {
    // Unknown: not enough information to classify the severity of a jank
    Unknown = 0,
    // None: no jank
    None = 1,
    // Partial: jank caused by missing the deadline by less than the app's frame interval
    Partial = 2,
    // Full: jank caused by missing the deadline by more than the app's frame interval
    Full = 3,
};

} // namespace android
