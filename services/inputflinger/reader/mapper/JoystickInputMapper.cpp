/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "../Macros.h"

#include "JoystickInputMapper.h"

#include <android-base/logging.h>
#include <ftl/enum.h>
#include <format>
#include <input/EvdevAbsCode.h>
#include <input/EvdevKeyCode.h>
#include <input/KeyCode.h>
#include <input/MotionEventAxis.h>

#include <map>
#include <sstream>

#include "EventHub.h"
#include "android/keycodes.h"

namespace android {

JoystickInputMapper::JoystickInputMapper(InputDeviceContext& deviceContext,
                                         const InputReaderConfiguration& readerConfig)
      : InputMapper(deviceContext, readerConfig) {}

JoystickInputMapper::~JoystickInputMapper() {}

uint32_t JoystickInputMapper::getSources() const {
    return AINPUT_SOURCE_JOYSTICK;
}

static bool isAxisEnabled(const int32_t axisId) {
    return MotionEventAxis(axisId) != MotionEventAxis::UNKNOWN;
}

static bool isAxisDisabled(const int32_t axisId) {
    return !isAxisEnabled(axisId);
}

void JoystickInputMapper::populateDeviceInfo(InputDeviceInfo& info) {
    InputMapper::populateDeviceInfo(info);

    for (const auto& [_, axis] : mAxes) {
        if (isAxisEnabled(axis.axisInfo.axis)) {
            addMotionRange(axis.axisInfo.axis, axis, info);
        }

        if (axis.axisInfo.mode == AxisInfo::MODE_SPLIT && isAxisEnabled(axis.axisInfo.highAxis)) {
            addMotionRange(axis.axisInfo.highAxis, axis, info);
        }
    }
}

void JoystickInputMapper::addMotionRange(int32_t axisId, const Axis& axis, InputDeviceInfo& info) {
    info.addMotionRange(axisId, AINPUT_SOURCE_JOYSTICK, axis.min, axis.max, axis.flat, axis.fuzz,
                        axis.resolution);
    /* In order to ease the transition for developers from using the old axes
     * to the newer, more semantically correct axes, we'll continue to register
     * the old axes as duplicates of their corresponding new ones.  */
    int32_t compatAxis = getCompatAxis(axisId);
    if (compatAxis >= 0) {
        info.addMotionRange(compatAxis, AINPUT_SOURCE_JOYSTICK, axis.min, axis.max, axis.flat,
                            axis.fuzz, axis.resolution);
    }
}

/* A mapping from axes the joystick actually has to the axes that should be
 * artificially created for compatibility purposes.
 * Returns -1 if no compatibility axis is needed. */
int32_t JoystickInputMapper::getCompatAxis(int32_t axis) {
    switch (axis) {
        case AMOTION_EVENT_AXIS_LTRIGGER:
            return AMOTION_EVENT_AXIS_BRAKE;
        case AMOTION_EVENT_AXIS_RTRIGGER:
            return AMOTION_EVENT_AXIS_GAS;
    }
    return -1;
}

void JoystickInputMapper::dump(std::string& dump) {
    dump += INDENT2 "Joystick Input Mapper:\n";

    dump += INDENT3 "Axes:\n";
    for (const auto& [rawAxis, axis] : mAxes) {
        dump += INDENT4;
        dump += MotionEvent::getLabelOrCode(axis.axisInfo.axis);
        if (axis.axisInfo.mode == AxisInfo::MODE_SPLIT) {
            dump += StringPrintf(" / %s (split at %d)",
                                 MotionEvent::getLabelOrCode(axis.axisInfo.highAxis).c_str(),
                                 axis.axisInfo.splitValue);
        } else if (axis.axisInfo.mode == AxisInfo::MODE_INVERT) {
            dump += " (invert)";
        }

        dump += StringPrintf(": min=%0.5f, max=%0.5f, flat=%0.5f, fuzz=%0.5f, resolution=%0.5f\n",
                             axis.min, axis.max, axis.flat, axis.fuzz, axis.resolution);
        dump += StringPrintf(INDENT4 "  scale=%0.5f, offset=%0.5f, "
                                     "highScale=%0.5f, highOffset=%0.5f\n",
                             axis.scale, axis.offset, axis.highScale, axis.highOffset);
        dump += StringPrintf(INDENT4 "  rawAxis=%d, rawMin=%d, rawMax=%d, "
                                     "rawFlat=%d, rawFuzz=%d, rawResolution=%d\n",
                             rawAxis, axis.rawAxisInfo.minValue, axis.rawAxisInfo.maxValue,
                             axis.rawAxisInfo.flat, axis.rawAxisInfo.fuzz,
                             axis.rawAxisInfo.resolution);
    }

    dump += INDENT3 "Key to Axis Remappings:\n";
    std::ostringstream ss;
    if (mEvdevKeyToEvdevAbs.empty()) {
        ss << INDENT4 "<empty>\n";
    } else {
        for (const auto& [evdevKey, axisData] : mEvdevKeyToEvdevAbs) {
            const auto& [evdevAbs, isHighAxis] = axisData;
            ss << INDENT4 << evdevKey << " -> " << evdevAbs
               << (isHighAxis ? " (high)" : "") << "\n";
        }
    }
    dump += ss.str();
}

std::list<NotifyArgs> JoystickInputMapper::reconfigure(nsecs_t when,
                                                       const InputReaderConfiguration& config,
                                                       ConfigurationChanges changes) {
    std::list<NotifyArgs> out = InputMapper::reconfigure(when, config, changes);
    bool refreshAxes = !changes.any();
    if (changes.test(InputReaderConfiguration::Change::AXIS_REMAPPING) &&
        !getDeviceContext().isVirtualDevice()) {
        std::unordered_map<int32_t, int32_t> axisRemapping;
        if (auto it = config.axisRemappingPerDevice.find(getDeviceId());
            it != config.axisRemappingPerDevice.end()) {
            axisRemapping = it->second;
        }

        std::map<KeyCode, MotionEventAxis> keyToAxisRemapping;
        if (auto it = config.keyToAxisRemappingPerDevice.find(getDeviceId());
            it != config.keyToAxisRemappingPerDevice.end()) {
            keyToAxisRemapping = it->second;
        }

        if (mAxisRemapping != axisRemapping) {
            mAxisRemapping = axisRemapping;
            getDeviceContext().setAxisRemapping(axisRemapping);
            refreshAxes = true;
        }

        if (mKeyToAxisRemapping != keyToAxisRemapping) {
            mKeyToAxisRemapping = keyToAxisRemapping;
            refreshAxes = true;
        }
    }

    if (refreshAxes) {
        bumpGeneration();
        mAxes.clear();
        // Collect all axes.
        for (int32_t abs = 0; abs <= ABS_MAX; abs++) {
            if (!(getAbsAxisUsage(abs, getDeviceContext().getDeviceClasses())
                          .test(InputDeviceClass::JOYSTICK))) {
                continue; // axis must be claimed by a different device
            }

            if (std::optional<RawAbsoluteAxisInfo> rawAxisInfo = getAbsoluteAxisInfo(abs);
                rawAxisInfo) {
                // Map axis.
                AxisInfo axisInfo;
                const bool explicitlyMapped = !getDeviceContext().mapAxis(abs, &axisInfo);

                if (!explicitlyMapped) {
                    // Axis is not explicitly mapped, will choose a generic axis later.
                    axisInfo.mode = AxisInfo::MODE_NORMAL;
                    axisInfo.axis = -1;
                } else if (isAxisDisabled(axisInfo.axis)) {
                    if (axisInfo.mode != AxisInfo::MODE_SPLIT ||
                        isAxisDisabled(axisInfo.highAxis)) {
                        continue;
                    }
                }

                mAxes.insert({abs, createAxis(axisInfo, rawAxisInfo.value(), explicitlyMapped)});
            }
        }

        addAxesMappedFromKeys();

        // If there are too many axes, start dropping them.
        // Prefer to keep explicitly mapped axes.
        if (mAxes.size() > PointerCoords::MAX_AXES) {
            ALOGI("Joystick '%s' has %zu axes but the framework only supports a maximum of %d.",
                  getDeviceName().c_str(), mAxes.size(), PointerCoords::MAX_AXES);
            pruneAxes(true);
            pruneAxes(false);
        }

        // Assign generic axis ids to remaining axes.
        int32_t nextGenericAxisId = AMOTION_EVENT_AXIS_GENERIC_1;
        for (auto it = mAxes.begin(); it != mAxes.end(); /*increment it inside loop*/) {
            Axis& axis = it->second;
            if (axis.axisInfo.axis < 0 && !axis.explicitlyMapped) {
                while (nextGenericAxisId <= AMOTION_EVENT_MAXIMUM_VALID_AXIS_VALUE &&
                       haveAxis(nextGenericAxisId)) {
                    nextGenericAxisId += 1;
                }

                if (nextGenericAxisId <= AMOTION_EVENT_MAXIMUM_VALID_AXIS_VALUE) {
                    axis.axisInfo.axis = nextGenericAxisId;
                    nextGenericAxisId += 1;
                } else {
                    ALOGI("Ignoring joystick '%s' axis %d because all of the generic axis ids "
                          "have already been assigned to other axes.",
                          getDeviceName().c_str(), it->first);
                    it = mAxes.erase(it);
                    continue;
                }
            }
            it++;
        }
    }

    // Remove axes if they were pruned above.
    std::erase_if(mEvdevKeyToEvdevAbs, [this](const auto& pair) {
        const auto& [evdevKey, _] = pair.second;
        return !mAxes.contains(ftl::to_underlying(evdevKey));
    });

    return out;
}

void JoystickInputMapper::addAxesMappedFromKeys() {
    mEvdevKeyToEvdevAbs.clear();

    if (mKeyToAxisRemapping.empty()) {
        return;
    }

    std::map<MotionEventAxis, std::pair<EvdevAbsCode, /*isHighAxis=*/bool>>
            motionEventAxisToEvdevAbs{};
    for (const auto& [abs, axis] : mAxes) {
        auto evdevAbs = EvdevAbsCode(abs);
        if (axis.axisInfo.mode == AxisInfo::MODE_SPLIT) {
            auto motionEventAxis = MotionEventAxis(axis.axisInfo.highAxis);
            motionEventAxisToEvdevAbs.insert({motionEventAxis, {evdevAbs, /*isHighAxis=*/true}});
        }
        auto motionEventAxis = MotionEventAxis(axis.axisInfo.axis);
        motionEventAxisToEvdevAbs.insert({motionEventAxis, {evdevAbs, /*isHighAxis=*/false}});
    }

    std::map<EvdevKeyCode, KeyCode> originalKeyMapping = getOriginalKeyMapping();

    // All "real" axes added so far are mapped from ABS_* values lower or equal to ABS_MAX.
    // Everything above ABS_MAX is guaranteed to not clash with the existing axes.
    int32_t newAbs = ABS_MAX + 1;

    for (const auto& [evdevKey, originalKeyCode] : originalKeyMapping) {
        auto keyToAxisIt = mKeyToAxisRemapping.find(originalKeyCode);
        if (keyToAxisIt == mKeyToAxisRemapping.end()) {
            // Skip the key code if it is not remapped to any axis.
            continue;
        }

        auto motionEventAxis = keyToAxisIt->second;

        auto evdevAbsIt = motionEventAxisToEvdevAbs.find(motionEventAxis);
        if (evdevAbsIt == motionEventAxisToEvdevAbs.end()) {
            // The axis to which the key is mapped does not exist, create a new axis in mAxes so
            // that it can store the value and be reported by populateDeviceInfo(). Use the next
            // available value above ABS_MAX as the key in the map, so that it does not clash with
            // any existing axes. This way it also easy to tell which axes were created from key to
            // axis remappings in a bug report.
            AxisInfo axisInfo{};
            axisInfo.axis = ftl::to_underlying(motionEventAxis);
            RawAbsoluteAxisInfo rawAxisInfo{.minValue = 0,
                                            .maxValue = 1,
                                            .flat = 0,
                                            .fuzz = 0,
                                            .resolution = 1};
            newAbs++;
            mAxes.insert({newAbs, createAxis(axisInfo, rawAxisInfo, /*explicitlyMapped=*/true)});
            mEvdevKeyToEvdevAbs.insert(
                    {evdevKey, {static_cast<EvdevAbsCode>(newAbs), /*isHighAxis=*/false}});
        } else {
            // The axis is already mapped from a "real" evdev axis, just point to it.
            const auto& [evdevAbs, isHighAxis] = evdevAbsIt->second;
            mEvdevKeyToEvdevAbs.insert({evdevKey, {evdevAbs, isHighAxis}});
        }
    }
}

std::map<EvdevKeyCode, KeyCode> JoystickInputMapper::getOriginalKeyMapping() const {
    std::map<EvdevKeyCode, KeyCode> originalKeyMapping;
    // Iteration over all possible evdev key codes is not ideal, however it's the most reliable way
    // of finding which evdev key codes are mapped to a given Android key code, as the mapKey()
    // method is relatively complex and finding the inverse mapping efficiently is not trivial.
    for (int32_t evdevKey = 0; evdevKey <= KEY_MAX; evdevKey++) {
        if (!getDeviceContext().hasScanCode(evdevKey)) {
            continue;
        }

        std::optional<MappedKey> mappedKey =
                getDeviceContext().mapKey(evdevKey, /*usageCode=*/0, /*metaState=*/0);
        if (!mappedKey) {
            LOG(FATAL) << "failed to map evdevKey=" << evdevKey;
            continue;
        }

        originalKeyMapping.insert(
                {static_cast<EvdevKeyCode>(evdevKey), mappedKey->originalKeyCode});
    }

    return originalKeyMapping;
}

JoystickInputMapper::Axis JoystickInputMapper::createAxis(const AxisInfo& axisInfo,
                                                          const RawAbsoluteAxisInfo& rawAxisInfo,
                                                          bool explicitlyMapped) {
    // Apply flat override.
    int32_t rawFlat = axisInfo.flatOverride < 0 ? rawAxisInfo.flat : axisInfo.flatOverride;

    float scale = std::numeric_limits<float>::signaling_NaN();
    float highScale = std::numeric_limits<float>::signaling_NaN();
    float highOffset = 0;
    float offset = 0;
    float min = 0;
    // Calculate scaling factors and limits.
    if (axisInfo.mode == AxisInfo::MODE_SPLIT) {
        scale = 1.0f / (axisInfo.splitValue - rawAxisInfo.minValue);
        highScale = 1.0f / (rawAxisInfo.maxValue - axisInfo.splitValue);
    } else if (isCenteredAxis(axisInfo.axis)) {
        scale = 2.0f / (rawAxisInfo.maxValue - rawAxisInfo.minValue);
        offset = avg(rawAxisInfo.minValue, rawAxisInfo.maxValue) * -scale;
        highOffset = offset;
        highScale = scale;
        min = -1.0f;
    } else if (isAnalogTrigger(axisInfo.axis)) {
        scale = 1.0f / (rawAxisInfo.maxValue - rawAxisInfo.minValue);
        offset = -rawAxisInfo.minValue * scale;
        highScale = scale;
        highOffset = offset;
    } else {
        // generic axes
        scale = 1.0f / (rawAxisInfo.maxValue - rawAxisInfo.minValue);
        highScale = scale;
    }

    constexpr float max = 1.0;
    const float flat = rawFlat * scale;
    const float fuzz = rawAxisInfo.fuzz * scale;
    const float resolution = rawAxisInfo.resolution * scale;

    // To eliminate noise while the joystick is at rest, filter out small variations
    // in axis values up front.
    const float filter = fuzz ? fuzz : flat * 0.25f;
    return Axis(rawAxisInfo, axisInfo, explicitlyMapped, scale, offset, highScale, highOffset, min,
                max, flat, fuzz, resolution, filter);
}

bool JoystickInputMapper::haveAxis(int32_t axisId) {
    for (const std::pair<const int32_t, Axis>& pair : mAxes) {
        const Axis& axis = pair.second;
        if (axis.axisInfo.axis == axisId ||
            (axis.axisInfo.mode == AxisInfo::MODE_SPLIT && axis.axisInfo.highAxis == axisId)) {
            return true;
        }
    }
    return false;
}

void JoystickInputMapper::pruneAxes(bool ignoreExplicitlyMappedAxes) {
    while (mAxes.size() > PointerCoords::MAX_AXES) {
        auto it = mAxes.begin();
        if (ignoreExplicitlyMappedAxes && it->second.explicitlyMapped) {
            continue;
        }
        ALOGI("Discarding joystick '%s' axis %d because there are too many axes.",
              getDeviceName().c_str(), it->first);
        mAxes.erase(it);
    }
}

bool JoystickInputMapper::isCenteredAxis(int32_t axis) {
    switch (axis) {
        case AMOTION_EVENT_AXIS_X:
        case AMOTION_EVENT_AXIS_Y:
        case AMOTION_EVENT_AXIS_Z:
        case AMOTION_EVENT_AXIS_RX:
        case AMOTION_EVENT_AXIS_RY:
        case AMOTION_EVENT_AXIS_RZ:
        case AMOTION_EVENT_AXIS_HAT_X:
        case AMOTION_EVENT_AXIS_HAT_Y:
        case AMOTION_EVENT_AXIS_ORIENTATION:
        case AMOTION_EVENT_AXIS_RUDDER:
        case AMOTION_EVENT_AXIS_WHEEL:
            return true;
        default:
            return false;
    }
}

bool JoystickInputMapper::isAnalogTrigger(int32_t axis) {
    switch (axis) {
        case AMOTION_EVENT_AXIS_LTRIGGER:
        case AMOTION_EVENT_AXIS_RTRIGGER:
        case AMOTION_EVENT_AXIS_GAS:
        case AMOTION_EVENT_AXIS_BRAKE:
        case AMOTION_EVENT_AXIS_THROTTLE:
            return true;
        default:
            return false;
    }
}

std::list<NotifyArgs> JoystickInputMapper::reset(nsecs_t when) {
    // Recenter all axes.
    for (std::pair<const int32_t, Axis>& pair : mAxes) {
        Axis& axis = pair.second;
        axis.resetValue();
    }

    return InputMapper::reset(when);
}

std::list<NotifyArgs> JoystickInputMapper::process(const RawEvent& rawEvent) {
    std::list<NotifyArgs> out;
    switch (rawEvent.type) {
        case EV_KEY: {
            auto evdevKey = static_cast<EvdevKeyCode>(rawEvent.code);
            auto keyIt = mEvdevKeyToEvdevAbs.find(evdevKey);
            if (keyIt == mEvdevKeyToEvdevAbs.end()) {
                // Key is not mapped to any axis, no need to process it.
                break;
            }

            const auto [evdevAbs, isHighAxis] = keyIt->second;

            auto absIt = mAxes.find(ftl::to_underlying(evdevAbs));

            if (absIt == mAxes.end()) {
                LOG(FATAL) << "axis " << evdevAbs << " not found for key " << evdevKey;
                return out;
            }

            Axis& axis = absIt->second;

            auto newValue = rawEvent.value == 0 ? 0 : 1;
            if (isHighAxis) {
                axis.highNewValue = newValue;
            } else {
                axis.newValue = newValue;
            }

            axis.lastUpdateTime = rawEvent.when;

            break;
        }
        case EV_ABS: {
            auto it = mAxes.find(rawEvent.code);
            if (it != mAxes.end()) {
                Axis& axis = it->second;
                float newValue, highNewValue;
                switch (axis.axisInfo.mode) {
                    case AxisInfo::MODE_INVERT:
                        newValue = (axis.rawAxisInfo.maxValue - rawEvent.value) * axis.scale +
                                axis.offset;
                        highNewValue = 0.0f;
                        break;
                    case AxisInfo::MODE_SPLIT:
                        if (rawEvent.value < axis.axisInfo.splitValue) {
                            newValue = (axis.axisInfo.splitValue - rawEvent.value) * axis.scale +
                                    axis.offset;
                            highNewValue = 0.0f;
                        } else if (rawEvent.value > axis.axisInfo.splitValue) {
                            newValue = 0.0f;
                            highNewValue =
                                    (rawEvent.value - axis.axisInfo.splitValue) * axis.highScale +
                                    axis.highOffset;
                        } else {
                            newValue = 0.0f;
                            highNewValue = 0.0f;
                        }

                        if (isAxisDisabled(axis.axisInfo.axis)) {
                            newValue = 0.0f;
                        }
                        if (isAxisDisabled(axis.axisInfo.highAxis)) {
                            highNewValue = 0.0f;
                        }
                        break;
                    default:
                        newValue = rawEvent.value * axis.scale + axis.offset;
                        highNewValue = 0.0f;
                        break;
                }
                axis.newValue = newValue;
                axis.highNewValue = highNewValue;
                axis.lastUpdateTime = rawEvent.when;
            }
            break;
        }

        case EV_SYN:
            switch (rawEvent.code) {
                case SYN_REPORT:
                    out += sync(rawEvent.when, rawEvent.readTime, /*force=*/false);
                    break;
            }
            break;
    }
    return out;
}

std::list<NotifyArgs> JoystickInputMapper::sync(nsecs_t when, nsecs_t readTime, bool force) {
    std::list<NotifyArgs> out;
    if (!filterAxes(force)) {
        return out;
    }

    int32_t metaState = getContext()->getGlobalMetaState();
    int32_t buttonState = 0;

    PointerProperties pointerProperties;
    pointerProperties.clear();
    pointerProperties.id = 0;
    pointerProperties.toolType = ToolType::UNKNOWN;

    // A map from the Android axis ID to the most recent value and update time.
    // This is used to resolve conflicts if multiple raw axes are mapped to the same
    // Android axis, ensuring the most recent event takes precedence.
    std::unordered_map<int32_t, std::pair<float, nsecs_t>> latestAxisValues;

    for (std::pair<const int32_t, Axis>& pair : mAxes) {
        const Axis& axis = pair.second;

        // Update the entry for the primary axis if this one is more recent.
        auto it = latestAxisValues.find(axis.axisInfo.axis);
        if (it == latestAxisValues.end() || axis.lastUpdateTime > it->second.second) {
            latestAxisValues[axis.axisInfo.axis] = {axis.currentValue, axis.lastUpdateTime};
        }

        // Update the entry for the high axis (for split mode) if this one is more recent.
        if (axis.axisInfo.mode == AxisInfo::MODE_SPLIT) {
            it = latestAxisValues.find(axis.axisInfo.highAxis);
            if (it == latestAxisValues.end() || axis.lastUpdateTime > it->second.second) {
                latestAxisValues[axis.axisInfo.highAxis] = {axis.highCurrentValue,
                                                            axis.lastUpdateTime};
            }
        }
    }

    PointerCoords pointerCoords;
    pointerCoords.clear();

    // Populate pointerCoords with the definitive, most recent values.
    for (const auto& [axisId, valuePair] : latestAxisValues) {
        setPointerCoordsAxisValue(&pointerCoords, axisId, valuePair.first);
    }

    // Moving a joystick axis should not wake the device because joysticks can
    // be fairly noisy even when not in use.  On the other hand, pushing a gamepad
    // button will likely wake the device.
    // TODO: Use the input device configuration to control this behavior more finely.
    uint32_t policyFlags = 0;
    ui::LogicalDisplayId displayId = ui::LogicalDisplayId::INVALID;
    if (getDeviceContext().getAssociatedViewport()) {
        displayId = getDeviceContext().getAssociatedViewport()->displayId;
    }

    out.push_back(NotifyMotionArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                   AINPUT_SOURCE_JOYSTICK, displayId, policyFlags,
                                   AMOTION_EVENT_ACTION_MOVE, 0, 0, metaState, buttonState,
                                   MotionClassification::NONE, 1, &pointerProperties,
                                   &pointerCoords, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                   AMOTION_EVENT_INVALID_CURSOR_POSITION, 0, /*videoFrames=*/{}));
    return out;
}

void JoystickInputMapper::setPointerCoordsAxisValue(PointerCoords* pointerCoords, int32_t axis,
                                                    float value) {
    pointerCoords->setAxisValue(axis, value);
    /* In order to ease the transition for developers from using the old axes
     * to the newer, more semantically correct axes, we'll continue to produce
     * values for the old axes as mirrors of the value of their corresponding
     * new axes. */
    int32_t compatAxis = getCompatAxis(axis);
    if (compatAxis >= 0) {
        pointerCoords->setAxisValue(compatAxis, value);
    }
}

bool JoystickInputMapper::filterAxes(bool force) {
    bool atLeastOneSignificantChange = force;
    for (std::pair<const int32_t, Axis>& pair : mAxes) {
        Axis& axis = pair.second;
        if (force ||
            hasValueChangedSignificantly(axis.filter, axis.newValue, axis.currentValue, axis.min,
                                         axis.max)) {
            axis.currentValue = axis.newValue;
            atLeastOneSignificantChange = true;
        }
        if (axis.axisInfo.mode == AxisInfo::MODE_SPLIT) {
            if (force ||
                hasValueChangedSignificantly(axis.filter, axis.highNewValue, axis.highCurrentValue,
                                             axis.min, axis.max)) {
                axis.highCurrentValue = axis.highNewValue;
                atLeastOneSignificantChange = true;
            }
        }
    }
    return atLeastOneSignificantChange;
}

bool JoystickInputMapper::hasValueChangedSignificantly(float filter, float newValue,
                                                       float currentValue, float min, float max) {
    if (newValue != currentValue) {
        // Filter out small changes in value unless the value is converging on the axis
        // bounds or center point.  This is intended to reduce the amount of information
        // sent to applications by particularly noisy joysticks (such as PS3).
        if (fabs(newValue - currentValue) > filter ||
            hasMovedNearerToValueWithinFilteredRange(filter, newValue, currentValue, min) ||
            hasMovedNearerToValueWithinFilteredRange(filter, newValue, currentValue, max) ||
            hasMovedNearerToValueWithinFilteredRange(filter, newValue, currentValue, 0)) {
            return true;
        }
    }
    return false;
}

bool JoystickInputMapper::hasMovedNearerToValueWithinFilteredRange(float filter, float newValue,
                                                                   float currentValue,
                                                                   float thresholdValue) {
    float newDistance = fabs(newValue - thresholdValue);
    if (newDistance < filter) {
        float oldDistance = fabs(currentValue - thresholdValue);
        if (newDistance < oldDistance) {
            return true;
        }
    }
    return false;
}

} // namespace android
