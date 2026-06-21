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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "FrameTimeline.h"

#include <android-base/stringprintf.h>
#include <common/FlagManager.h>
#include <common/trace.h>
#include <utils/Log.h>

#include <chrono>
#include <cinttypes>
#include <numeric>
#include <unordered_set>

#include "Jank/JankTracker.h"

namespace android::scheduler {

using base::StringAppendF;
using FrameTimelineEvent = perfetto::protos::pbzero::FrameTimelineEvent;
using FrameTimelineDataSource = impl::FrameTimeline::FrameTimelineDataSource;

namespace {

template <typename Period>
void dumpTable(std::string& result, TimelineItem predictions, TimelineItem actuals,
               const std::string& indent, PredictionState predictionState, nsecs_t baseTime) {
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "\t\t");
    StringAppendF(&result, "    Start time\t\t|");
    StringAppendF(&result, "    End time\t\t|");
    StringAppendF(&result, "    Present time\n");
    if (predictionState == PredictionState::Valid) {
        // Dump the Predictions only if they are valid
        StringAppendF(&result, "%s", indent.c_str());
        StringAppendF(&result, "Expected\t|");
        std::chrono::nanoseconds startTime(predictions.startTime - baseTime);
        std::chrono::nanoseconds endTime(predictions.endTime - baseTime);
        std::chrono::nanoseconds presentTime(predictions.presentTime - baseTime);
        StringAppendF(&result, "\t%10.2f\t|\t%10.2f\t|\t%10.2f\n",
                      std::chrono::duration<double, Period>(startTime).count(),
                      std::chrono::duration<double, Period>(endTime).count(),
                      std::chrono::duration<double, Period>(presentTime).count());
    }
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Actual  \t|");

    if (actuals.startTime == 0) {
        StringAppendF(&result, "\t\tN/A\t|");
    } else {
        std::chrono::nanoseconds startTime(std::max<nsecs_t>(0, actuals.startTime - baseTime));
        StringAppendF(&result, "\t%10.2f\t|",
                      std::chrono::duration<double, Period>(startTime).count());
    }
    if (actuals.endTime <= 0) {
        // Animation leashes can send the endTime as -1
        StringAppendF(&result, "\t\tN/A\t|");
    } else {
        std::chrono::nanoseconds endTime(actuals.endTime - baseTime);
        StringAppendF(&result, "\t%10.2f\t|",
                      std::chrono::duration<double, Period>(endTime).count());
    }
    if (actuals.presentTime == 0) {
        StringAppendF(&result, "\t\tN/A\n");
    } else {
        std::chrono::nanoseconds presentTime(std::max<nsecs_t>(0, actuals.presentTime - baseTime));
        StringAppendF(&result, "\t%10.2f\n",
                      std::chrono::duration<double, Period>(presentTime).count());
    }

    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "----------------------");
    StringAppendF(&result, "----------------------");
    StringAppendF(&result, "----------------------");
    StringAppendF(&result, "----------------------\n");
}

std::string toString(PredictionState predictionState) {
    switch (predictionState) {
        case PredictionState::Valid:
            return "Valid";
        case PredictionState::Expired:
            return "Expired";
        case PredictionState::None:
            return "None";
    }
}

std::string jankTypeBitmaskToString(int32_t jankType) {
    if (jankType == JankType::None) {
        return "None";
    }

    std::vector<std::string> janks;
    if (jankType & JankType::DisplayHAL) {
        janks.emplace_back("Display HAL");
        jankType &= ~JankType::DisplayHAL;
    }
    if (jankType & JankType::SurfaceFlingerCpuDeadlineMissed) {
        janks.emplace_back("SurfaceFlinger deadline missed (while in HWC)");
        jankType &= ~JankType::SurfaceFlingerCpuDeadlineMissed;
    }
    if (jankType & JankType::SurfaceFlingerGpuDeadlineMissed) {
        janks.emplace_back("SurfaceFlinger deadline missed (while in GPU comp)");
        jankType &= ~JankType::SurfaceFlingerGpuDeadlineMissed;
    }
    if (jankType & JankType::AppDeadlineMissed) {
        janks.emplace_back("App Deadline Missed");
        jankType &= ~JankType::AppDeadlineMissed;
    }
    if (jankType & JankType::AppResyncedJitter) {
        janks.emplace_back("App Resynced Jitter");
        jankType &= ~JankType::AppResyncedJitter;
    }
    if (jankType & JankType::PredictionError) {
        janks.emplace_back("Prediction Error");
        jankType &= ~JankType::PredictionError;
    }
    if (jankType & JankType::SurfaceFlingerScheduling) {
        janks.emplace_back("SurfaceFlinger Scheduling");
        jankType &= ~JankType::SurfaceFlingerScheduling;
    }
    if (jankType & JankType::BufferStuffing) {
        janks.emplace_back("Buffer Stuffing");
        jankType &= ~JankType::BufferStuffing;
    }
    if (jankType & JankType::Unknown) {
        janks.emplace_back("Unknown jank");
        jankType &= ~JankType::Unknown;
    }
    if (jankType & JankType::SurfaceFlingerStuffing) {
        janks.emplace_back("SurfaceFlinger Stuffing");
        jankType &= ~JankType::SurfaceFlingerStuffing;
    }
    if (jankType & JankType::Dropped) {
        janks.emplace_back("Dropped Frame");
        jankType &= ~JankType::Dropped;
    }
    if (jankType & JankType::NonAnimating) {
        janks.emplace_back("Non Animating");
        jankType &= ~JankType::NonAnimating;
    }

    if (jankType & JankType::DisplayNotOn) {
        janks.emplace_back("Display not ON");
        jankType &= ~JankType::DisplayNotOn;
    }

    if (jankType & JankType::DisplayModeChangeInProgress) {
        janks.emplace_back("ModeChange in progress");
        jankType &= ~JankType::DisplayModeChangeInProgress;
    }

    if (jankType & JankType::DisplayPowerModeChangeInProgress) {
        janks.emplace_back("PowerModeChange in progress");
        jankType &= ~JankType::DisplayPowerModeChangeInProgress;
    }

    // jankType should be 0 if all types of jank were checked for.
    LOG_ALWAYS_FATAL_IF(jankType != 0, "Unrecognized jank type value 0x%x", jankType);
    return std::accumulate(janks.begin(), janks.end(), std::string(),
                           [](const std::string& l, const std::string& r) {
                               return l.empty() ? r : l + ", " + r;
                           });
}

std::string toString(FramePresentMetadata presentMetadata) {
    switch (presentMetadata) {
        case FramePresentMetadata::OnTimePresent:
            return "On Time Present";
        case FramePresentMetadata::LatePresent:
            return "Late Present";
        case FramePresentMetadata::EarlyPresent:
            return "Early Present";
        case FramePresentMetadata::UnknownPresent:
            return "Unknown Present";
    }
}

std::string toString(FrameReadyMetadata finishMetadata) {
    switch (finishMetadata) {
        case FrameReadyMetadata::OnTimeFinish:
            return "On Time Finish";
        case FrameReadyMetadata::LateFinish:
            return "Late Finish";
        case FrameReadyMetadata::UnknownFinish:
            return "Unknown Finish";
    }
}

std::string toString(FrameStartMetadata startMetadata) {
    switch (startMetadata) {
        case FrameStartMetadata::OnTimeStart:
            return "On Time Start";
        case FrameStartMetadata::LateStart:
            return "Late Start";
        case FrameStartMetadata::EarlyStart:
            return "Early Start";
        case FrameStartMetadata::UnknownStart:
            return "Unknown Start";
    }
}

std::string toString(SurfaceFrame::PresentState presentState) {
    using PresentState = SurfaceFrame::PresentState;
    switch (presentState) {
        case PresentState::Presented:
            return "Presented";
        case PresentState::Dropped:
            return "Dropped";
        case PresentState::Unknown:
            return "Unknown";
    }
}

FrameTimelineEvent::PresentType toProto(FramePresentMetadata presentMetadata) {
    switch (presentMetadata) {
        case FramePresentMetadata::EarlyPresent:
            return FrameTimelineEvent::PRESENT_EARLY;
        case FramePresentMetadata::LatePresent:
            return FrameTimelineEvent::PRESENT_LATE;
        case FramePresentMetadata::OnTimePresent:
            return FrameTimelineEvent::PRESENT_ON_TIME;
        case FramePresentMetadata::UnknownPresent:
            return FrameTimelineEvent::PRESENT_UNSPECIFIED;
    }
}

FrameTimelineEvent::PredictionType toProto(PredictionState predictionState) {
    switch (predictionState) {
        case PredictionState::Valid:
            return FrameTimelineEvent::PREDICTION_VALID;
        case PredictionState::Expired:
            return FrameTimelineEvent::PREDICTION_EXPIRED;
        case PredictionState::None:
            return FrameTimelineEvent::PREDICTION_UNKNOWN;
    }
}

int32_t jankTypeBitmaskToProto(int32_t jankType) {
    if (jankType == JankType::None) {
        return FrameTimelineEvent::JANK_NONE;
    }

    int32_t protoJank = 0;
    if (jankType & JankType::DisplayHAL) {
        protoJank |= FrameTimelineEvent::JANK_DISPLAY_HAL;
        jankType &= ~JankType::DisplayHAL;
    }
    if (jankType & JankType::SurfaceFlingerCpuDeadlineMissed) {
        protoJank |= FrameTimelineEvent::JANK_SF_CPU_DEADLINE_MISSED;
        jankType &= ~JankType::SurfaceFlingerCpuDeadlineMissed;
    }
    if (jankType & JankType::SurfaceFlingerGpuDeadlineMissed) {
        protoJank |= FrameTimelineEvent::JANK_SF_GPU_DEADLINE_MISSED;
        jankType &= ~JankType::SurfaceFlingerGpuDeadlineMissed;
    }
    if (jankType & JankType::AppDeadlineMissed) {
        protoJank |= FrameTimelineEvent::JANK_APP_DEADLINE_MISSED;
        jankType &= ~JankType::AppDeadlineMissed;
    }
    if (jankType & JankType::AppResyncedJitter) {
        protoJank |= FrameTimelineEvent::JANK_APP_RESYNCED_JITTER;
        jankType &= ~JankType::AppResyncedJitter;
    }
    if (jankType & JankType::PredictionError) {
        protoJank |= FrameTimelineEvent::JANK_PREDICTION_ERROR;
        jankType &= ~JankType::PredictionError;
    }
    if (jankType & JankType::SurfaceFlingerScheduling) {
        protoJank |= FrameTimelineEvent::JANK_SF_SCHEDULING;
        jankType &= ~JankType::SurfaceFlingerScheduling;
    }
    if (jankType & JankType::BufferStuffing) {
        protoJank |= FrameTimelineEvent::JANK_BUFFER_STUFFING;
        jankType &= ~JankType::BufferStuffing;
    }
    if (jankType & JankType::Unknown) {
        protoJank |= FrameTimelineEvent::JANK_UNKNOWN;
        jankType &= ~JankType::Unknown;
    }
    if (jankType & JankType::SurfaceFlingerStuffing) {
        protoJank |= FrameTimelineEvent::JANK_SF_STUFFING;
        jankType &= ~JankType::SurfaceFlingerStuffing;
    }
    if (jankType & JankType::Dropped) {
        // Jank dropped does not append to other janks, it fully overrides.
        protoJank |= FrameTimelineEvent::JANK_DROPPED;
        jankType &= ~JankType::Dropped;
    }
    if (jankType & JankType::NonAnimating) {
        protoJank |= FrameTimelineEvent::JANK_NON_ANIMATING;
        jankType &= ~JankType::NonAnimating;
    }
    if (jankType & JankType::DisplayNotOn) {
        protoJank |= FrameTimelineEvent::JANK_DISPLAY_NOT_ON;
        jankType &= ~JankType::DisplayNotOn;
    }
    if (jankType & JankType::DisplayModeChangeInProgress) {
        protoJank |= FrameTimelineEvent::JANK_DISPLAY_MODE_CHANGE_IN_PROGRESS;
        jankType &= ~JankType::DisplayModeChangeInProgress;
    }

    if (jankType & JankType::DisplayPowerModeChangeInProgress) {
        protoJank |= FrameTimelineEvent::JANK_DISPLAY_POWER_MODE_CHANGE_IN_PROGRESS;
        jankType &= ~JankType::DisplayPowerModeChangeInProgress;
    }

    // jankType should be 0 if all types of jank were checked for.
    LOG_ALWAYS_FATAL_IF(jankType != 0, "Unrecognized jank type value 0x%x", jankType);
    return protoJank;
}

FrameTimelineEvent::JankSeverityType toProto(JankSeverityType jankSeverityType) {
    switch (jankSeverityType) {
        case JankSeverityType::Unknown:
            return FrameTimelineEvent::SEVERITY_UNKNOWN;
        case JankSeverityType::None:
            return FrameTimelineEvent::SEVERITY_NONE;
        case JankSeverityType::Partial:
            return FrameTimelineEvent::SEVERITY_PARTIAL;
        case JankSeverityType::Full:
            return FrameTimelineEvent::SEVERITY_FULL;
    }
}

// Returns the smallest timestamp from the set of predictions and actuals.
nsecs_t getMinTime(PredictionState predictionState, TimelineItem predictions,
                   TimelineItem actuals) {
    nsecs_t minTime = std::numeric_limits<nsecs_t>::max();
    if (predictionState == PredictionState::Valid) {
        // Checking start time for predictions is enough because start time is always lesser than
        // endTime and presentTime.
        minTime = std::min(minTime, predictions.startTime);
    }

    // Need to check startTime, endTime and presentTime for actuals because some frames might not
    // have them set.
    if (actuals.startTime != 0) {
        minTime = std::min(minTime, actuals.startTime);
    }
    if (actuals.endTime != 0) {
        minTime = std::min(minTime, actuals.endTime);
    }
    if (actuals.presentTime != 0) {
        minTime = std::min(minTime, actuals.presentTime);
    }
    return minTime;
}

bool shouldTraceForDataSource(const FrameTimelineDataSource::TraceContext& ctx, nsecs_t timestamp) {
    if (auto ds = ctx.GetDataSourceLocked(); ds && ds->getStartTime() > timestamp) {
        return false;
    }

    return true;
}

nsecs_t calculateDisplayPresentJitter(nsecs_t presentDelay, Fps refreshRate) {
    if (refreshRate.getPeriodNsecs() > 0) {
        return std::abs(presentDelay) % refreshRate.getPeriodNsecs();
    }
    return 0;
}

bool delayMatchVsyncCadence(nsecs_t presentDelay, Fps refreshRate, nsecs_t presentThreshold) {
    // For very high present delays, vsync cadence doesn't matter much. Just mark it as on cadence
    // to avoid prediction errors.
    if (presentDelay > impl::FrameTimeline::kThresholdFpsForAnimation.getPeriodNsecs()) {
        return true;
    }

    const nsecs_t deltaToVsync = calculateDisplayPresentJitter(presentDelay, refreshRate);
    return deltaToVsync < presentThreshold ||
            deltaToVsync >= refreshRate.getPeriodNsecs() - presentThreshold;
}

// Formula explained in go/refined-jank-metric
std::pair<float, JankSeverityType> calculateJankSeverity(int32_t jankType,
                                                         nsecs_t expectedPresentDelta,
                                                         nsecs_t actualPresentDelta, Fps interval) {
    if (expectedPresentDelta <= 0) return {0.0f, JankSeverityType::Unknown};

    const int32_t jankBitmask = JankType::DisplayHAL | JankType::SurfaceFlingerCpuDeadlineMissed |
            JankType::SurfaceFlingerGpuDeadlineMissed | JankType::AppDeadlineMissed |
            JankType::PredictionError | JankType::SurfaceFlingerScheduling | JankType::Unknown |
            JankType::Dropped | JankType::AppResyncedJitter;
    const int32_t nonJankBitmask = JankType::BufferStuffing | JankType::SurfaceFlingerStuffing |
            JankType::NonAnimating | JankType::DisplayNotOn |
            JankType::DisplayModeChangeInProgress | JankType::DisplayPowerModeChangeInProgress;
    static_assert((kJankTypeAll & ~(jankBitmask | nonJankBitmask)) == 0);

    if ((jankType & jankBitmask) == 0) { // Not Janky
        return {0.0f, JankSeverityType::None};
    }

    if (jankType == JankType::Dropped) {
        return {0.0f, JankSeverityType::Unknown};
    }

    const auto absDelay = std::abs(expectedPresentDelta - actualPresentDelta);
    const float ratio = static_cast<float>(absDelay + interval.getPeriodNsecs()) /
            static_cast<float>(interval.getPeriodNsecs());
    const float w_s = std::log2(ratio);
    const float w_f = std::sqrt(static_cast<float>(interval.getPeriodNsecs()) /
                                static_cast<float>((120_Hz).getPeriodNsecs()));
    const float score = w_s * w_f;

    JankSeverityType type;
    if (score == 0) {
        type = JankSeverityType::None;
    } else if (score < 0.9f) {
        type = JankSeverityType::Partial;
    } else {
        type = JankSeverityType::Full;
    }

    return {score, type};
}

} // namespace

int64_t TraceCookieCounter::getCookieForTracing() {
    return ++mTraceCookie;
}

SurfaceFrame::SurfaceFrame(const FrameTimelineInfo& frameTimelineInfo, pid_t ownerPid,
                           uid_t ownerUid, int32_t layerId, std::string layerName,
                           std::string debugName, PredictionState predictionState,
                           scheduler::TimelineItem&& predictions,
                           std::shared_ptr<TimeStats> timeStats,
                           JankClassificationThresholds thresholds,
                           TraceCookieCounter* traceCookieCounter, bool isBuffer, GameMode gameMode,
                           int32_t systemContentPriority)
      : mToken(frameTimelineInfo.vsyncId),
        mInputEventId(frameTimelineInfo.inputEventId),
        mVsyncResyncedJitter(frameTimelineInfo.vsyncResyncedJitterNanos),
        mDequeueBufferDuration(frameTimelineInfo.dequeueBufferDurationNanos),
        mOwnerPid(ownerPid),
        mOwnerUid(ownerUid),
        mLayerName(std::move(layerName)),
        mDebugName(std::move(debugName)),
        mLayerId(layerId),
        mPresentState(PresentState::Unknown),
        mPredictionState(predictionState),
        mPredictions(predictions),
        mActuals({0, 0, 0}),
        mTimeStats(timeStats),
        mJankClassificationThresholds(thresholds),
        mTraceCookieCounter(*traceCookieCounter),
        mIsBuffer(isBuffer),
        mGameMode(gameMode),
        mSystemContentPriority(systemContentPriority) {}

void SurfaceFrame::setActualStartTime(nsecs_t actualStartTime) {
    std::scoped_lock lock(mMutex);
    mActuals.startTime = actualStartTime;
}

void SurfaceFrame::setActualQueueTime(nsecs_t actualQueueTime) {
    std::scoped_lock lock(mMutex);
    mActualQueueTime = actualQueueTime;
}

void SurfaceFrame::setAcquireFenceTime(nsecs_t acquireFenceTime) {
    std::scoped_lock lock(mMutex);
    if (CC_UNLIKELY(acquireFenceTime == Fence::SIGNAL_TIME_PENDING)) {
        mActuals.endTime = mActualQueueTime;
    } else {
        mActuals.endTime = std::max(acquireFenceTime, mActualQueueTime);
    }
}

void SurfaceFrame::setDesiredPresentTime(nsecs_t desiredPresentTime) {
    std::scoped_lock lock(mMutex);
    mActuals.desiredPresentTime = desiredPresentTime;
}

void SurfaceFrame::setDropTime(nsecs_t dropTime) {
    std::scoped_lock lock(mMutex);
    mDropTime = dropTime;
}

void SurfaceFrame::setPresentState(PresentState presentState, LastFrameTimestamps times) {
    std::scoped_lock lock(mMutex);
    LOG_ALWAYS_FATAL_IF(mPresentState != PresentState::Unknown,
                        "setPresentState called on a SurfaceFrame from Layer - %s, that has a "
                        "PresentState - %s set already.",
                        mDebugName.c_str(), toString(mPresentState).c_str());
    mPresentState = presentState;
    mLastFrameTimestamps = times;
}

void SurfaceFrame::setRenderRate(Fps renderRate) {
    std::lock_guard<std::mutex> lock(mMutex);
    mRenderRate = renderRate;
}

Fps SurfaceFrame::getRenderRate() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mRenderRate ? *mRenderRate : mDisplayFrameRenderRate;
}

void SurfaceFrame::setGpuComposition() {
    std::scoped_lock lock(mMutex);
    mGpuComposition = true;
}

void SurfaceFrame::setPreviousSurfaceFrame(const std::weak_ptr<SurfaceFrame>& prev) {
    std::scoped_lock lock(mMutex);
    mPreviousSurfaceFrame = prev;
}

SurfaceFrame::PreviousFrameData SurfaceFrame::previousFrameDataLocked() const {
    auto currentPrev = mPreviousSurfaceFrame.lock();

    for (int i = 0; i < kMaxPreviousFrames; i++) {
        if (!currentPrev || currentPrev->mPredictionState != PredictionState::Valid) {
            return PreviousFrameData::unknown();
        }

        if (currentPrev->mToken > mToken) {
            // this can happen when a RenderThread animation is running and the UI thread is late.
            return PreviousFrameData::outOfOrder();
        }

        std::scoped_lock lock(currentPrev->mMutex);
        if (currentPrev->mPresentState == PresentState::Presented) {
            return PreviousFrameData::create(currentPrev->mPredictions, currentPrev->mActuals,
                                             currentPrev->mVsyncResyncedJitter);
        }
        currentPrev = currentPrev->mPreviousSurfaceFrame.lock();
    }

    return PreviousFrameData::tooFarBack();
}

// TODO(b/316171339): migrate from perfetto side
bool SurfaceFrame::isSelfJanky() const {
    int32_t jankType = getJankType().value_or(JankType::None);

    if (jankType == JankType::None) {
        return false;
    }

    int32_t jankBitmask =
            JankType::AppDeadlineMissed | JankType::Unknown | JankType::AppResyncedJitter;
    if (jankType & jankBitmask) {
        return true;
    }

    return false;
}

std::optional<int32_t> SurfaceFrame::getJankType() const {
    std::scoped_lock lock(mMutex);
    if (mPresentState == PresentState::Dropped) {
        return JankType::Dropped;
    }
    if (mActuals.presentTime == 0) {
        // Frame hasn't been presented yet.
        return std::nullopt;
    }
    return mJankType.value();
}

std::optional<JankSeverityType> SurfaceFrame::getJankSeverityType() const {
    std::scoped_lock lock(mMutex);
    if (mActuals.presentTime == 0) {
        // Frame hasn't been presented yet.
        return std::nullopt;
    }
    return mJankSeverityTypeLegacy;
}

std::optional<float> SurfaceFrame::getJankSeverityScore() const {
    std::scoped_lock lock(mMutex);
    if (mActuals.presentTime == 0) {
        // Frame hasn't been presented yet.
        return std::nullopt;
    }
    return mJankScore;
}

nsecs_t SurfaceFrame::getBaseTime() const {
    std::scoped_lock lock(mMutex);
    return getMinTime(mPredictionState, mPredictions, mActuals);
}

TimelineItem SurfaceFrame::getActuals() const {
    std::scoped_lock lock(mMutex);
    return mActuals;
}

PredictionState SurfaceFrame::getPredictionState() const {
    std::scoped_lock lock(mMutex);
    return mPredictionState;
}

SurfaceFrame::PresentState SurfaceFrame::getPresentState() const {
    std::scoped_lock lock(mMutex);
    return mPresentState;
}

FramePresentMetadata SurfaceFrame::getFramePresentMetadata() const {
    std::scoped_lock lock(mMutex);
    return mFramePresentMetadata.value();
}

FrameReadyMetadata SurfaceFrame::getFrameReadyMetadata() const {
    std::scoped_lock lock(mMutex);
    return mFrameReadyMetadata.value();
}

nsecs_t SurfaceFrame::getDropTime() const {
    std::scoped_lock lock(mMutex);
    return mDropTime;
}

void SurfaceFrame::promoteToBuffer() {
    std::scoped_lock lock(mMutex);
    LOG_ALWAYS_FATAL_IF(mIsBuffer == true,
                        "Trying to promote an already promoted BufferSurfaceFrame from layer %s "
                        "with token %" PRId64 "",
                        mDebugName.c_str(), mToken);
    mIsBuffer = true;
}

bool SurfaceFrame::getIsBuffer() const {
    std::scoped_lock lock(mMutex);
    return mIsBuffer;
}

template <typename Period>
void SurfaceFrame::dump(std::string& result, const std::string& indent, nsecs_t baseTime) const {
    std::scoped_lock lock(mMutex);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Layer - %s", mDebugName.c_str());
    if (mJankType.value() != JankType::None) {
        // Easily identify a janky Surface Frame in the dump
        StringAppendF(&result, " [*] ");
    }
    StringAppendF(&result, "\n");
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Token: %" PRId64 "\n", mToken);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Is Buffer?: %d\n", mIsBuffer);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Owner Pid : %d\n", mOwnerPid);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Scheduled rendering rate: %d fps\n",
                  mRenderRate ? mRenderRate->getIntValue() : 0);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Layer ID : %d\n", mLayerId);
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Present State : %s\n", toString(mPresentState).c_str());
    StringAppendF(&result, "%s", indent.c_str());
    if (mPresentState == PresentState::Dropped) {
        std::chrono::nanoseconds dropTime(mDropTime - baseTime);
        StringAppendF(&result, "Drop time : %10f\n",
                      std::chrono::duration<double, Period>(dropTime).count());
        StringAppendF(&result, "%s", indent.c_str());
    }
    StringAppendF(&result, "Prediction State : %s\n", toString(mPredictionState).c_str());
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Jank Type : %s\n", jankTypeBitmaskToString(mJankType.value()).c_str());
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Present Metadata : %s\n",
                  toString(mFramePresentMetadata.value()).c_str());
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Finish Metadata: %s\n", toString(mFrameReadyMetadata.value()).c_str());
    std::chrono::nanoseconds latchTime(
            std::max(static_cast<int64_t>(0), mLastFrameTimestamps.latchTime - baseTime));
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Last latch time: %10f\n",
                  std::chrono::duration<double, Period>(latchTime).count());
    std::chrono::nanoseconds latchExpectedPresentTime(
            std::max(static_cast<int64_t>(0), mLastFrameTimestamps.expectedPresentTime - baseTime));
    StringAppendF(&result, "%s", indent.c_str());
    StringAppendF(&result, "Last expected present time: %10f\n",
                  std::chrono::duration<double, Period>(latchTime).count());
    if (mPredictionState == PredictionState::Valid) {
        nsecs_t presentDelta = mActuals.presentTime - mPredictions.presentTime;
        std::chrono::nanoseconds presentDeltaNs(std::abs(presentDelta));
        StringAppendF(&result, "%s", indent.c_str());
        StringAppendF(&result, "Present delta: %10f\n",
                      std::chrono::duration<double, Period>(presentDeltaNs).count());
    }
    dumpTable<Period>(result, mPredictions, mActuals, indent, mPredictionState, baseTime);
}

std::string SurfaceFrame::miniDump() const {
    std::scoped_lock lock(mMutex);
    std::string result;
    StringAppendF(&result, "Layer - %s\n", mDebugName.c_str());
    StringAppendF(&result, "Token: %" PRId64 "\n", mToken);
    StringAppendF(&result, "Is Buffer?: %d\n", mIsBuffer);
    StringAppendF(&result, "Present State : %s\n", toString(mPresentState).c_str());
    StringAppendF(&result, "Prediction State : %s\n", toString(mPredictionState).c_str());
    StringAppendF(&result, "Jank Type : %s\n", jankTypeBitmaskToString(mJankType.value()).c_str());
    StringAppendF(&result, "Present Metadata : %s\n",
                  toString(mFramePresentMetadata.value()).c_str());
    StringAppendF(&result, "Finish Metadata: %s\n", toString(mFrameReadyMetadata.value()).c_str());
    StringAppendF(&result, "Present time: %" PRId64 "", mActuals.presentTime);
    return result;
}

void SurfaceFrame::classifyJankLegacyLocked(int32_t displayFrameJankType, const Fps& refreshRate,
                                            Fps displayFrameRenderRate, nsecs_t* outDeadlineDelta,
                                            nsecs_t* outPresentDelay) {
    const nsecs_t presentDelay = mActuals.presentTime - mPredictions.presentTime;

    if (outPresentDelay) {
        *outPresentDelay = presentDelay;
    }
    const nsecs_t deadlineDelta = mActuals.endTime - mPredictions.endTime;
    if (outDeadlineDelta) {
        *outDeadlineDelta = deadlineDelta;
    }

    if (deadlineDelta > mJankClassificationThresholds.deadlineThreshold) {
        mFrameReadyMetadata.legacy() = FrameReadyMetadata::LateFinish;
    } else {
        mFrameReadyMetadata.legacy() = FrameReadyMetadata::OnTimeFinish;
    }

    const nsecs_t presentThreshold = mJankClassificationThresholds.presentThreshold;
    if (std::abs(presentDelay) > presentThreshold) {
        mFramePresentMetadata.legacy() = presentDelay > 0 ? FramePresentMetadata::LatePresent
                                                          : FramePresentMetadata::EarlyPresent;
        // Jank that is missing by less than the render rate period is classified as partial jank,
        // otherwise it is a full jank.
        mJankSeverityTypeLegacy = std::abs(presentDelay) < displayFrameRenderRate.getPeriodNsecs()
                ? JankSeverityType::Partial
                : JankSeverityType::Full;
    } else {
        mFramePresentMetadata.legacy() = FramePresentMetadata::OnTimePresent;
    }

    if (mFramePresentMetadata.legacy() == FramePresentMetadata::OnTimePresent) {
        // Frames presented on time are not janky.
        mJankType.legacy() = JankType::None;
    } else if (mFramePresentMetadata.legacy() == FramePresentMetadata::EarlyPresent) {
        if (mFrameReadyMetadata.legacy() == FrameReadyMetadata::OnTimeFinish) {
            // Finish on time, Present early
            if (delayMatchVsyncCadence(presentDelay, refreshRate, presentThreshold)) {
                // Delta factor of vsync
                mJankType.legacy() = JankType::SurfaceFlingerScheduling;
            } else {
                // Delta not a factor of vsync
                mJankType.legacy() = JankType::PredictionError;
            }
        } else if (mFrameReadyMetadata.legacy() == FrameReadyMetadata::LateFinish) {
            // Finish late, Present early
            mJankType.legacy() = JankType::Unknown;
        }
    } else { // FramePresentMetadata::LatePresent
        const bool readyBeforePreviousLatch = mLastFrameTimestamps.latchTime != 0 &&
                mPredictions.endTime <= mLastFrameTimestamps.latchTime;
        const bool dueLastFrame = (mLastFrameTimestamps.expectedPresentTime != 0 &&
                                   mPredictions.presentTime - presentThreshold <
                                           mLastFrameTimestamps.expectedPresentTime);

        if (readyBeforePreviousLatch && dueLastFrame) {
            // Buffer Stuffing.
            mJankType.legacy() |= JankType::BufferStuffing;
            // In a stuffed state, the frame could be stuck on a dequeue wait for quite some time.
            // Because of this dequeue wait, it can be hard to tell if a frame was genuinely late.
            // We try to do this by moving the deadline. Since the queue could be stuffed by more
            // than one buffer, we take the last latch time as reference and give one vsync
            // worth of time for the frame to be ready.
            nsecs_t adjustedDeadline =
                    mLastFrameTimestamps.latchTime + displayFrameRenderRate.getPeriodNsecs();
            if (adjustedDeadline > mActuals.endTime) {
                mFrameReadyMetadata.legacy() = FrameReadyMetadata::OnTimeFinish;
            } else {
                mFrameReadyMetadata.legacy() = FrameReadyMetadata::LateFinish;
            }

            if (outPresentDelay) {
                nsecs_t adjustedPresent = mLastFrameTimestamps.expectedPresentTime +
                        displayFrameRenderRate.getPeriodNsecs();
                *outPresentDelay = mActuals.presentTime - adjustedPresent;
            }
        }
        if (mFrameReadyMetadata.legacy() == FrameReadyMetadata::OnTimeFinish) {
            // Finish on time, Present late
            if (displayFrameJankType != JankType::None) {
                // Propagate displayFrame's jank if it exists
                mJankType.legacy() |= displayFrameJankType;
            } else {
                if (!(mJankType.legacy() & JankType::BufferStuffing)) {
                    // In a stuffed state, if the app finishes on time and there is no display frame
                    // jank, only buffer stuffing is the root cause of the jank.
                    if (delayMatchVsyncCadence(presentDelay, refreshRate, presentThreshold)) {
                        // Delta factor of vsync
                        mJankType.legacy() |= JankType::SurfaceFlingerScheduling;
                    } else {
                        // Delta not a factor of vsync
                        mJankType.legacy() |= JankType::PredictionError;
                    }
                }
            }
        } else if (mFrameReadyMetadata.legacy() == FrameReadyMetadata::LateFinish) {
            // Finish late, Present late
            mJankType.legacy() |= JankType::AppDeadlineMissed;
            // Propagate DisplayFrame's jankType if it is janky
            mJankType.legacy() |= displayFrameJankType;
        }
    }
}

void SurfaceFrame::classifyJankLocked(int32_t displayFrameJankTypeLegacy,
                                      int32_t displayFrameJankTypeExperimental,
                                      const Fps& refreshRate, Fps displayFrameRenderRate,
                                      nsecs_t* outDeadlineDelta, nsecs_t* outPresentDelay) {
    if (mActuals.presentTime == Fence::SIGNAL_TIME_INVALID) {
        // Cannot do any classification for invalid present time.
        mJankType.legacy() = JankType::Unknown;
        mJankType.experimental() = mJankType.legacy();
        if (displayFrameJankTypeExperimental & JankType::DisplayNotOn) {
            mJankType.experimental() = JankType::DisplayNotOn;
        }
        mJankSeverityTypeLegacy = JankSeverityType::Unknown;
        if (outDeadlineDelta) {
            *outDeadlineDelta = -1;
        }
        if (outPresentDelay) {
            *outPresentDelay = 0;
        }
        return;
    }

    if (mPredictionState == PredictionState::Expired) {
        // We classify prediction expired as AppDeadlineMissed as the
        // TokenManager::kMaxTokens we store is large enough to account for a
        // reasonable app, so prediction expire would mean a huge scheduling delay.
        mJankType.legacy() = mPresentState != PresentState::Presented ? JankType::Dropped
                                                                      : JankType::AppDeadlineMissed;
        mJankType.experimental() = mJankType.legacy();
        if (displayFrameJankTypeExperimental & JankType::DisplayNotOn) {
            mJankType.experimental() = JankType::DisplayNotOn;
        }
        mJankSeverityTypeLegacy = JankSeverityType::Unknown;
        if (outDeadlineDelta) {
            *outDeadlineDelta = -1;
        }
        if (outPresentDelay) {
            *outPresentDelay = 0;
        }
        return;
    }

    if (mPredictionState == PredictionState::None) {
        // Cannot do jank classification on frames that don't have a token.
        return;
    }

    classifyJankLegacyLocked(displayFrameJankTypeLegacy, refreshRate, displayFrameRenderRate,
                             outDeadlineDelta, outPresentDelay);

    const auto previousFrameData = previousFrameDataLocked();

    mPresentDelay = mActuals.presentTime - mPredictions.presentTime;
    if (outPresentDelay) {
        *outPresentDelay = mPresentDelay;
    }

    mExpectedPresentDelta = (mRenderRate ? *mRenderRate : mDisplayFrameRenderRate).getPeriodNsecs();
    mActualPresentDelta = mExpectedPresentDelta + mPresentDelay;

    const nsecs_t deadlineDelta = mActuals.endTime - mPredictions.endTime;
    if (outDeadlineDelta) {
        *outDeadlineDelta = deadlineDelta;
    }

    // Subtract the time spent in dequeueBuffer to avoid counting it in the app budget
    if (deadlineDelta - mDequeueBufferDuration > mJankClassificationThresholds.deadlineThreshold) {
        mFrameReadyMetadata.experimental() = FrameReadyMetadata::LateFinish;
    } else {
        mFrameReadyMetadata.experimental() = FrameReadyMetadata::OnTimeFinish;
    }

    const nsecs_t presentThreshold = mJankClassificationThresholds.presentThreshold;

    if (std::abs(mPresentDelay) <= presentThreshold) {
        mFramePresentMetadata.experimental() = FramePresentMetadata::OnTimePresent;
    } else {
        if (mPresentDelay > impl::FrameTimeline::kThresholdFpsForAnimation.getPeriodNsecs()) {
            // The frame is significantly delayed, so we have to mark it as late.
            mFramePresentMetadata.experimental() = FramePresentMetadata::LatePresent;
        } else {
            switch (previousFrameData.status) {
                case PreviousFrameData::Status::Unknown:
                    // We can't do any classification if the previous frame is unknown
                    mFramePresentMetadata.experimental() = FramePresentMetadata::UnknownPresent;
                    break;
                case PreviousFrameData::Status::FrameHistoryTooLong:
                    // We can't do any classification if the history is too long
                    mFramePresentMetadata.experimental() = FramePresentMetadata::UnknownPresent;
                    mJankDebugMetadata = -1.0f;
                    break;
                case PreviousFrameData::Status::OutOfOrder:
                    // This can happen if the frame is significantly delayed on the UI thread,
                    // and RT is updating meanwhile.
                    mFramePresentMetadata.experimental() = FramePresentMetadata::LatePresent;
                    break;
                case PreviousFrameData::Status::Valid: {
                    const auto thisExpectedPresentTime =
                            mPredictions.presentTime + mVsyncResyncedJitter;
                    const auto prevExpectedPresentTime = previousFrameData.predictions.presentTime +
                            previousFrameData.vsyncResyncedJitter;

                    mActualPresentDelta =
                            mActuals.presentTime - previousFrameData.actuals.presentTime;
                    mExpectedPresentDelta = thisExpectedPresentTime - prevExpectedPresentTime;
                    const nsecs_t presentationConsistencyDelay =
                            mActualPresentDelta - mExpectedPresentDelta;
                    const float deltaFrameRatio = mExpectedPresentDelta == 0
                            ? 0
                            : abs(static_cast<float>(presentationConsistencyDelay) /
                                  static_cast<float>(mExpectedPresentDelta));
                    const bool smooth =
                            mExpectedPresentDelta < impl::FrameTimeline::kThresholdFpsForAnimation
                                                            .getPeriodNsecs() &&
                            deltaFrameRatio <= impl::FrameTimeline::kDeltaFramesRatioThreshold;
                    if (smooth) {
                        mFramePresentMetadata.experimental() = FramePresentMetadata::OnTimePresent;
                        mJankDebugMetadata = deltaFrameRatio;
                    } else {
                        const auto delayRatio = static_cast<float>(std::abs(mPresentDelay)) /
                                static_cast<float>(mExpectedPresentDelta);
                        if (delayRatio > impl::FrameTimeline::kDeltaFramesRatioThreshold) {
                            mFramePresentMetadata.experimental() =
                                    (presentationConsistencyDelay > 0)
                                    ? FramePresentMetadata::LatePresent
                                    : FramePresentMetadata::EarlyPresent;
                        } else {
                            // frame is delayed, it doesn't match the frame pacing but it also
                            // pretty far from the previous frame. In that case, mark it as non
                            // animation.
                            mFramePresentMetadata.experimental() =
                                    FramePresentMetadata::UnknownPresent;
                        }
                        mJankDebugMetadata = delayRatio;
                    }
                }
            }
        }
    }

    if (displayFrameJankTypeExperimental & JankType::DisplayNotOn) {
        mJankType.experimental() = JankType::DisplayNotOn;
        return;
    }

    if (displayFrameJankTypeExperimental & JankType::DisplayPowerModeChangeInProgress) {
        mJankType.experimental() = JankType::DisplayPowerModeChangeInProgress;
        return;
    }

    if (FlagManager::getInstance().use_content_priority_for_jank_classification() &&
        mSystemContentPriority < 0) {
        mJankType.experimental() = JankType::NonAnimating;
        mJankDebugMetadata = static_cast<float>(mSystemContentPriority);
        return;
    }

    if (mFramePresentMetadata.experimental() == FramePresentMetadata::OnTimePresent) {
        // Frames presented on time are not janky, but might be buffer stuffed.
        if (std::abs(mPresentDelay) <= presentThreshold) {
            mJankType.experimental() = JankType::None;
        } else {
            mJankType.experimental() = JankType::BufferStuffing;
            // fixup the app deadline based on the present delay.
            if (mPresentDelay > 0) {
                const nsecs_t adjustedDeadline = mPredictions.endTime + mPresentDelay;
                if (adjustedDeadline > mActuals.endTime) {
                    mFrameReadyMetadata.experimental() = FrameReadyMetadata::OnTimeFinish;
                }
            }
        }
    } else if (mFramePresentMetadata.experimental() == FramePresentMetadata::EarlyPresent) {
        //  Finish on time, Present early
        if (delayMatchVsyncCadence(mPresentDelay, refreshRate, presentThreshold)) {
            // Delta factor of vsync
            mJankType.experimental() = JankType::SurfaceFlingerScheduling;
        } else {
            // Delta not a factor of vsync
            mJankType.experimental() = JankType::PredictionError;
        }
    } else if (mFramePresentMetadata.experimental() == FramePresentMetadata::LatePresent) {
        if (mFrameReadyMetadata.experimental() == FrameReadyMetadata::OnTimeFinish) {
            // Finish on time, Present late
            if (displayFrameJankTypeExperimental != JankType::None) {
                // Propagate displayFrame's jank if it exists
                mJankType.experimental() |= displayFrameJankTypeExperimental;
            } else {
                if (!(mJankType.experimental() & JankType::BufferStuffing)) {
                    // In a stuffed state, if the app finishes on time and there is no display frame
                    // jank, only buffer stuffing is the root cause of the jank.
                    if (delayMatchVsyncCadence(mPresentDelay, refreshRate, presentThreshold)) {
                        // Delta factor of vsync
                        mJankType.experimental() |= JankType::SurfaceFlingerScheduling;
                    } else {
                        // Delta not a factor of vsync
                        mJankType.experimental() |= JankType::PredictionError;
                    }
                }
            }
        } else if (mFrameReadyMetadata.experimental() == FrameReadyMetadata::LateFinish) {
            // Finish late, Present late
            mJankType.experimental() |= JankType::AppDeadlineMissed;
            // Propagate DisplayFrame's jankType if it is janky
            mJankType.experimental() |= displayFrameJankTypeExperimental;
        }
    } else { // mFramePresentMetadata.experimental() == FramePresentMetadata::UnknownPresent
        mJankType.experimental() |= JankType::NonAnimating;
    }

    if (mPresentState != PresentState::Presented) {
        mJankType.legacy() = JankType::Dropped;
        mJankType.experimental() = mJankType.legacy();
        // Since frame was not presented, lets drop any present value
        mActuals.presentTime = 0;
        mJankSeverityTypeLegacy = JankSeverityType::Unknown;
    }

    if (mVsyncResyncedJitter > 0) {
        // the app adjusted the vsync time due to a delay on the main thread - mark is as
        // AppResyncedJitter
        mJankType.experimental() |= JankType::AppResyncedJitter;
    }
}

void SurfaceFrame::onPresent(nsecs_t presentTime, int32_t displayFrameJankTypeLegacy,
                             int32_t displayFrameJankTypeExperimental, Fps refreshRate,
                             Fps displayFrameRenderRate, nsecs_t displayDeadlineDelta,
                             nsecs_t displayPresentJitter) {
    std::scoped_lock lock(mMutex);

    mDisplayFrameRenderRate = displayFrameRenderRate;
    mActuals.presentTime = presentTime;
    nsecs_t deadlineDelta = 0;
    nsecs_t presentDelay = 0;

    classifyJankLocked(displayFrameJankTypeLegacy, displayFrameJankTypeExperimental, refreshRate,
                       displayFrameRenderRate, &deadlineDelta, &presentDelay);
    const auto [score, severity] =
            calculateJankSeverity(mJankType.value(), mExpectedPresentDelta, mActualPresentDelta,
                                  mRenderRate ? *mRenderRate : mDisplayFrameRenderRate);
    mJankSeverity = severity;
    mJankScore = score;

    if (mPredictionState != PredictionState::None) {
        // Only update janky frames if the app used vsync predictions
        mTimeStats->incrementJankyFrames({refreshRate, mRenderRate, mOwnerUid, mLayerName,
                                          mGameMode, mJankType.value(), displayDeadlineDelta,
                                          displayPresentJitter, deadlineDelta});

        gui::JankData jd;
        jd.frameVsyncId = mToken;
        jd.jankTypeLegacy = mJankType.legacy();
        jd.jankTypeExperimental = mJankType.experimental();
        jd.frameIntervalNs =
                (mRenderRate ? *mRenderRate : mDisplayFrameRenderRate).getPeriodNsecs();
        jd.presentDelayNs = presentDelay;
        jd.jankScore = mJankScore;

        if (mPredictionState == PredictionState::Valid) {
            jd.scheduledAppFrameTimeNs = mPredictions.endTime - mPredictions.startTime;

            // Using expected start, rather than actual, to measure the entire frame time. That is
            // if the application starts the frame later than scheduled, include that delay in the
            // frame time, as it usually means main thread being busy with non-rendering work.
            if (mPresentState == PresentState::Dropped) {
                jd.actualAppFrameTimeNs = mDropTime - mPredictions.startTime;
            } else {
                jd.actualAppFrameTimeNs = mActuals.endTime - mPredictions.startTime;
            }
        } else {
            jd.scheduledAppFrameTimeNs = 0;
            jd.actualAppFrameTimeNs = 0;
        }

        JankTracker::onJankData(mLayerId, jd);
    }
}

void SurfaceFrame::onCommitNotComposited(Fps refreshRate, Fps displayFrameRenderRate) {
    std::scoped_lock lock(mMutex);

    mDisplayFrameRenderRate = displayFrameRenderRate;
    mActuals.presentTime = mPredictions.presentTime;
    classifyJankLocked(JankType::None, JankType::None, refreshRate, displayFrameRenderRate, nullptr,
                       nullptr);
}

void SurfaceFrame::tracePredictions(int64_t displayFrameToken, nsecs_t monoBootOffset,
                                    bool filterFramesBeforeTraceStarts) const {
    int64_t expectedTimelineCookie = mTraceCookieCounter.getCookieForTracing();
    bool traced = false;

    // Expected timeline start
    FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
        const auto timestamp = mPredictions.startTime;
        if (filterFramesBeforeTraceStarts && !shouldTraceForDataSource(ctx, timestamp)) {
            // Do not trace packets started before tracing starts.
            return;
        }
        traced = true;

        std::scoped_lock lock(mMutex);
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
        if (monoBootOffset > 0 &&
            FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
            monoBootOffset =
                    systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
        }
        packet->set_timestamp(static_cast<uint64_t>(timestamp + monoBootOffset));

        auto* event = packet->set_frame_timeline_event();
        auto* expectedSurfaceFrameStartEvent = event->set_expected_surface_frame_start();

        expectedSurfaceFrameStartEvent->set_cookie(expectedTimelineCookie);

        expectedSurfaceFrameStartEvent->set_token(mToken);
        expectedSurfaceFrameStartEvent->set_display_frame_token(displayFrameToken);

        expectedSurfaceFrameStartEvent->set_pid(mOwnerPid);
        expectedSurfaceFrameStartEvent->set_layer_name(mDebugName);
    });

    if (traced) {
        // Expected timeline end
        FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
            std::scoped_lock lock(mMutex);
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
            if (monoBootOffset > 0 &&
                FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                monoBootOffset =
                        systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
            }
            packet->set_timestamp(static_cast<uint64_t>(mPredictions.endTime + monoBootOffset));

            auto* event = packet->set_frame_timeline_event();
            auto* expectedSurfaceFrameEndEvent = event->set_frame_end();

            expectedSurfaceFrameEndEvent->set_cookie(expectedTimelineCookie);
        });
    }
}

void SurfaceFrame::traceActuals(int64_t displayFrameToken, nsecs_t monoBootOffset,
                                bool filterFramesBeforeTraceStarts) const {
    int64_t actualTimelineCookie = mTraceCookieCounter.getCookieForTracing();
    bool traced = false;

    // Actual timeline start
    FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
        const auto timestamp = [&]() {
            std::scoped_lock lock(mMutex);
            // Actual start time is not yet available, so use expected start instead
            if (mPredictionState == PredictionState::Expired) {
                // If prediction is expired, we can't use the predicted start time. Instead, just
                // use a start time a little earlier than the end time so that we have some info
                // about this frame in the trace.
                nsecs_t endTime =
                        (mPresentState == PresentState::Dropped ? mDropTime : mActuals.endTime);
                return endTime - kPredictionExpiredStartTimeDelta;
            }

            return mActuals.startTime == 0 ? mPredictions.startTime : mActuals.startTime;
        }();

        if (filterFramesBeforeTraceStarts && !shouldTraceForDataSource(ctx, timestamp)) {
            // Do not trace packets started before tracing starts.
            return;
        }
        traced = true;

        std::scoped_lock lock(mMutex);
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
        if (monoBootOffset > 0 &&
            FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
            monoBootOffset =
                    systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
        }
        packet->set_timestamp(static_cast<uint64_t>(timestamp + monoBootOffset));

        auto* event = packet->set_frame_timeline_event();
        auto* actualSurfaceFrameStartEvent = event->set_actual_surface_frame_start();

        actualSurfaceFrameStartEvent->set_cookie(actualTimelineCookie);

        actualSurfaceFrameStartEvent->set_token(mToken);
        actualSurfaceFrameStartEvent->set_display_frame_token(displayFrameToken);

        actualSurfaceFrameStartEvent->set_pid(mOwnerPid);
        actualSurfaceFrameStartEvent->set_layer_name(mDebugName);

        if (mPresentState == PresentState::Dropped) {
            actualSurfaceFrameStartEvent->set_present_type(FrameTimelineEvent::PRESENT_DROPPED);
            actualSurfaceFrameStartEvent->set_present_type_experimental(
                    FrameTimelineEvent::PRESENT_DROPPED);
        } else if (mPresentState == PresentState::Unknown) {
            actualSurfaceFrameStartEvent->set_present_type(FrameTimelineEvent::PRESENT_UNSPECIFIED);
            actualSurfaceFrameStartEvent->set_present_type_experimental(
                    FrameTimelineEvent::PRESENT_UNSPECIFIED);
        } else {
            actualSurfaceFrameStartEvent->set_present_type(toProto(mFramePresentMetadata.value()));
            actualSurfaceFrameStartEvent->set_present_type_experimental(
                    toProto(mFramePresentMetadata.altValue()));
        }
        actualSurfaceFrameStartEvent->set_on_time_finish(mFrameReadyMetadata.value() ==
                                                         FrameReadyMetadata::OnTimeFinish);
        actualSurfaceFrameStartEvent->set_gpu_composition(mGpuComposition);
        actualSurfaceFrameStartEvent->set_jank_type(jankTypeBitmaskToProto(mJankType.value()));
        actualSurfaceFrameStartEvent->set_prediction_type(toProto(mPredictionState));
        actualSurfaceFrameStartEvent->set_is_buffer(mIsBuffer);
        actualSurfaceFrameStartEvent->set_present_delay_millis(mPresentDelay / 1e6f);
        actualSurfaceFrameStartEvent->set_jank_type_experimental(
                jankTypeBitmaskToProto(mJankType.altValue()));
        actualSurfaceFrameStartEvent->set_jank_debug_metadata(mJankDebugMetadata);
        actualSurfaceFrameStartEvent->set_vsync_resynced_jitter_millis(mVsyncResyncedJitter / 1e6f);
        actualSurfaceFrameStartEvent->set_jank_severity_score(mJankScore);
        actualSurfaceFrameStartEvent->set_jank_severity_type(toProto(mJankSeverity));
    });

    if (traced) {
        // Actual timeline end
        FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
            std::scoped_lock lock(mMutex);
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
            if (monoBootOffset > 0 &&
                FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                monoBootOffset =
                        systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
            }
            if (mPresentState == PresentState::Dropped) {
                packet->set_timestamp(static_cast<uint64_t>(mDropTime + monoBootOffset));
            } else {
                packet->set_timestamp(static_cast<uint64_t>(mActuals.endTime + monoBootOffset));
            }

            auto* event = packet->set_frame_timeline_event();
            auto* actualSurfaceFrameEndEvent = event->set_frame_end();

            actualSurfaceFrameEndEvent->set_cookie(actualTimelineCookie);
        });
    }
}

/**
 * TODO(b/178637512): add inputEventId to the perfetto trace.
 */
void SurfaceFrame::trace(int64_t displayFrameToken, nsecs_t monoBootOffset,
                         bool filterFramesBeforeTraceStarts) const {
    if (mToken == FrameTimelineInfo::INVALID_VSYNC_ID ||
        displayFrameToken == FrameTimelineInfo::INVALID_VSYNC_ID) {
        // No packets can be traced with a missing token.
        return;
    }
    if (getPredictionState() != PredictionState::Expired) {
        // Expired predictions have zeroed timestamps. This cannot be used in any meaningful way in
        // a trace.
        tracePredictions(displayFrameToken, monoBootOffset, filterFramesBeforeTraceStarts);
    }
    traceActuals(displayFrameToken, monoBootOffset, filterFramesBeforeTraceStarts);
}

namespace impl {

int64_t TokenManager::generateTokenForPredictions(TimelineItem&& predictions) {
    SFTRACE_CALL();
    std::scoped_lock lock(mMutex);
    const int64_t assignedToken = mCurrentToken++;
    if (assignedToken < static_cast<int64_t>(kMaxTokens)) {
        // Append to the back until max capacity is reached.
        mPredictions.push_back({assignedToken, predictions});
    } else {
        // Overwrite the oldest entry.
        size_t insertIndex = static_cast<size_t>(assignedToken) % kMaxTokens;
        mPredictions[insertIndex] = {assignedToken, predictions};
    }

    return assignedToken;
}

std::optional<TimelineItem> TokenManager::getPredictionsForToken(int64_t token) const {
    std::scoped_lock lock(mMutex);
    // Start searching from the most recent tokens.
    // mCurrentToken is the next token to be assigned. If mCurrentToken is 0,
    // it means no tokens have been assigned yet, so we return early.
    if (mCurrentToken == 0) {
        return {};
    }

    const size_t startIndex = (static_cast<size_t>(mCurrentToken) - 1) % kMaxTokens;
    const size_t numElements = std::min(static_cast<size_t>(mCurrentToken), kMaxTokens);
    for (size_t i = 0; i < numElements; ++i) {
        const size_t index = (startIndex + (kMaxTokens - i)) % kMaxTokens;
        const auto& [assignedToken, predictions] = mPredictions[index];
        if (assignedToken == token) {
            return predictions;
        }
    }
    return {};
}

FrameTimeline::FrameTimeline(std::shared_ptr<TimeStats> timeStats, pid_t surfaceFlingerPid,
                             JankClassificationThresholds thresholds, bool useBootTimeClock,
                             bool filterFramesBeforeTraceStarts)
      : mUseBootTimeClock(useBootTimeClock),
        mFilterFramesBeforeTraceStarts(filterFramesBeforeTraceStarts),
        mMaxDisplayFrames(kDefaultMaxDisplayFrames),
        mTimeStats(std::move(timeStats)),
        mSurfaceFlingerPid(surfaceFlingerPid),
        mJankClassificationThresholds(thresholds) {
    mCurrentDisplayFrame =
            std::make_shared<DisplayFrame>(mTimeStats, thresholds, &mTraceCookieCounter);
}

void FrameTimeline::onBootFinished() {
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;
    perfetto::Tracing::Initialize(args);
    registerDataSource();
}

void FrameTimeline::registerDataSource() {
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name(kFrameTimelineDataSource);
    FrameTimelineDataSource::Register(dsd);
}

std::shared_ptr<SurfaceFrame> FrameTimeline::createSurfaceFrameForToken(
        const FrameTimelineInfo& frameTimelineInfo, pid_t ownerPid, uid_t ownerUid, int32_t layerId,
        std::string layerName, std::string debugName, bool isBuffer, GameMode gameMode,
        int32_t systemContentPriority) {
    SFTRACE_CALL();
    if (frameTimelineInfo.vsyncId == FrameTimelineInfo::INVALID_VSYNC_ID) {
        return std::make_shared<SurfaceFrame>(frameTimelineInfo, ownerPid, ownerUid, layerId,
                                              std::move(layerName), std::move(debugName),
                                              PredictionState::None, TimelineItem(), mTimeStats,
                                              mJankClassificationThresholds, &mTraceCookieCounter,
                                              isBuffer, gameMode, systemContentPriority);
    }
    std::optional<TimelineItem> predictions =
            mTokenManager.getPredictionsForToken(frameTimelineInfo.vsyncId);
    if (predictions) {
        return std::make_shared<SurfaceFrame>(frameTimelineInfo, ownerPid, ownerUid, layerId,
                                              std::move(layerName), std::move(debugName),
                                              PredictionState::Valid, std::move(*predictions),
                                              mTimeStats, mJankClassificationThresholds,
                                              &mTraceCookieCounter, isBuffer, gameMode,
                                              systemContentPriority);
    }
    return std::make_shared<SurfaceFrame>(frameTimelineInfo, ownerPid, ownerUid, layerId,
                                          std::move(layerName), std::move(debugName),
                                          PredictionState::Expired, TimelineItem(), mTimeStats,
                                          mJankClassificationThresholds, &mTraceCookieCounter,
                                          isBuffer, gameMode, systemContentPriority);
}

FrameTimeline::DisplayFrame::DisplayFrame(std::shared_ptr<TimeStats> timeStats,
                                          JankClassificationThresholds thresholds,
                                          TraceCookieCounter* traceCookieCounter)
      : mSurfaceFlingerPredictions(TimelineItem()),
        mSurfaceFlingerActuals(TimelineItem()),
        mTimeStats(timeStats),
        mJankClassificationThresholds(thresholds),
        mTraceCookieCounter(*traceCookieCounter) {
    mSurfaceFrames.reserve(kNumSurfaceFramesInitial);
}

void FrameTimeline::addSurfaceFrame(std::shared_ptr<SurfaceFrame> surfaceFrame) {
    SFTRACE_CALL();
    std::scoped_lock lock(mMutex);

    if (const auto it = mPreviousSurfaceFrames.find(surfaceFrame->getLayerId());
        it != mPreviousSurfaceFrames.end()) {
        surfaceFrame->setPreviousSurfaceFrame(it->second);
    }

    mPreviousSurfaceFrames[surfaceFrame->getLayerId()] = surfaceFrame;

    mCurrentDisplayFrame->addSurfaceFrame(surfaceFrame);
}

void FrameTimeline::setSfWakeUp(int64_t token, nsecs_t wakeUpTime, Fps refreshRate, Fps renderRate,
                                FrameTimelineDisplayState displayState) {
    SFTRACE_CALL();
    std::scoped_lock lock(mMutex);
    mCurrentDisplayFrame->onSfWakeUp(token, refreshRate, renderRate,
                                     mTokenManager.getPredictionsForToken(token), wakeUpTime,
                                     displayState);
}

void FrameTimeline::setSfPresent(nsecs_t sfPresentTime,
                                 const std::shared_ptr<FenceTime>& presentFence,
                                 const std::shared_ptr<FenceTime>& gpuFence) {
    SFTRACE_CALL();
    std::scoped_lock lock(mMutex);
    mCurrentDisplayFrame->setActualEndTime(sfPresentTime);
    mCurrentDisplayFrame->setGpuFence(gpuFence);
    mPendingPresentFences.emplace_back(std::make_pair(presentFence, mCurrentDisplayFrame));
    flushPendingPresentFences();
    finalizeCurrentDisplayFrame();
}

void FrameTimeline::onCommitNotComposited() {
    SFTRACE_CALL();
    std::scoped_lock lock(mMutex);
    mCurrentDisplayFrame->onCommitNotComposited();
    mCurrentDisplayFrame.reset();
    mCurrentDisplayFrame = std::make_shared<DisplayFrame>(mTimeStats, mJankClassificationThresholds,
                                                          &mTraceCookieCounter);
}

void FrameTimeline::DisplayFrame::addSurfaceFrame(std::shared_ptr<SurfaceFrame> surfaceFrame) {
    mSurfaceFrames.push_back(surfaceFrame);
}

void FrameTimeline::DisplayFrame::onSfWakeUp(int64_t token, Fps refreshRate, Fps renderRate,
                                             std::optional<TimelineItem> predictions,
                                             nsecs_t wakeUpTime,
                                             FrameTimelineDisplayState displayState) {
    mToken = token;
    mRefreshRate = refreshRate;
    mRenderRate = renderRate;
    if (!predictions) {
        mPredictionState = PredictionState::Expired;
    } else {
        mPredictionState = PredictionState::Valid;
        mSurfaceFlingerPredictions = *predictions;
    }
    mSurfaceFlingerActuals.startTime = wakeUpTime;
    mDisplayState = displayState;
}

void FrameTimeline::DisplayFrame::setPredictions(PredictionState predictionState,
                                                 TimelineItem predictions) {
    mPredictionState = predictionState;
    mSurfaceFlingerPredictions = predictions;
}

void FrameTimeline::DisplayFrame::setActualStartTime(nsecs_t actualStartTime) {
    mSurfaceFlingerActuals.startTime = actualStartTime;
}

void FrameTimeline::DisplayFrame::setActualEndTime(nsecs_t actualEndTime) {
    mSurfaceFlingerActuals.endTime = actualEndTime;
}

void FrameTimeline::DisplayFrame::setGpuFence(const std::shared_ptr<FenceTime>& gpuFence) {
    mGpuFence = gpuFence;
}

void FrameTimeline::DisplayFrame::classifyJankLegacy(nsecs_t presentDelay,
                                                     nsecs_t previousPresentTime) {
    const nsecs_t presentThreshold = mJankClassificationThresholds.presentThreshold;

    if (std::abs(presentDelay) > presentThreshold) {
        mFramePresentMetadata.legacy() = presentDelay > 0 ? FramePresentMetadata::LatePresent
                                                          : FramePresentMetadata::EarlyPresent;
        // Jank that is missing by less than the render rate period is classified as partial jank,
        // otherwise it is a full jank.
        mJankSeverityTypeLegacy = std::abs(presentDelay) < mRenderRate.getPeriodNsecs()
                ? JankSeverityType::Partial
                : JankSeverityType::Full;
    } else {
        mFramePresentMetadata.legacy() = FramePresentMetadata::OnTimePresent;
    }

    if (mFramePresentMetadata.legacy() != FramePresentMetadata::OnTimePresent) {
        // Do jank classification only if present is not on time
        if (mFramePresentMetadata.legacy() == FramePresentMetadata::EarlyPresent) {
            if (mFrameReadyMetadata == FrameReadyMetadata::OnTimeFinish) {
                // Finish on time, Present early
                if (delayMatchVsyncCadence(presentDelay, mRefreshRate, presentThreshold)) {
                    // Delta is a factor of vsync if its within the presentTheshold on either side
                    // of the vsyncPeriod. Example: 0-2ms and 9-11ms are both within the threshold
                    // of the vsyncPeriod if the threshold was 2ms and the vsyncPeriod was 11ms.
                    mJankType.legacy() = JankType::SurfaceFlingerScheduling;
                } else {
                    // Delta is not a factor of vsync,
                    mJankType.legacy() = JankType::PredictionError;
                }
            } else if (mFrameReadyMetadata == FrameReadyMetadata::LateFinish) {
                // Finish late, Present early
                mJankType.legacy() = JankType::SurfaceFlingerScheduling;
            } else {
                // Finish time unknown
                mJankType.legacy() = JankType::Unknown;
            }
        } else if (mFramePresentMetadata.legacy() == FramePresentMetadata::LatePresent) {
            if (std::abs(mSurfaceFlingerPredictions.presentTime - previousPresentTime) <=
                        presentThreshold ||
                previousPresentTime > mSurfaceFlingerPredictions.presentTime) {
                // The previous frame was either presented in the current frame's expected vsync or
                // it was presented even later than the current frame's expected vsync.
                mJankType.legacy() = JankType::SurfaceFlingerStuffing;
            }
            if (mFrameReadyMetadata == FrameReadyMetadata::OnTimeFinish &&
                !(mJankType.legacy() & JankType::SurfaceFlingerStuffing)) {
                // Finish on time, Present late
                if (delayMatchVsyncCadence(presentDelay, mRefreshRate, presentThreshold)) {
                    // Delta is a factor of vsync if its within the presentTheshold on either side
                    // of the vsyncPeriod. Example: 0-2ms and 9-11ms are both within the threshold
                    // of the vsyncPeriod if the threshold was 2ms and the vsyncPeriod was 11ms.
                    mJankType.legacy() = JankType::DisplayHAL;
                } else {
                    // Delta is not a factor of vsync
                    mJankType.legacy() = JankType::PredictionError;
                }
            } else if (mFrameReadyMetadata == FrameReadyMetadata::LateFinish) {
                if (!(mJankType.legacy() & JankType::SurfaceFlingerStuffing) ||
                    mSurfaceFlingerActuals.presentTime - previousPresentTime >
                            mRefreshRate.getPeriodNsecs() + presentThreshold) {
                    // Classify CPU vs GPU if SF wasn't stuffed or if SF was stuffed but this frame
                    // was presented more than a vsync late.
                    if (mGpuFence != FenceTime::NO_FENCE) {
                        // If SF was in GPU composition, classify it as GPU deadline missed.
                        mJankType.legacy() = JankType::SurfaceFlingerGpuDeadlineMissed;
                    } else {
                        mJankType.legacy() = JankType::SurfaceFlingerCpuDeadlineMissed;
                    }
                }
            } else {
                // Finish time unknown
                mJankType.legacy() = JankType::Unknown;
            }
        } else {
            // Present unknown
            mJankType.legacy() = JankType::Unknown;
        }
    }
}

void FrameTimeline::DisplayFrame::classifyJank(nsecs_t& deadlineDelta,
                                               nsecs_t& displayPresentJitter,
                                               nsecs_t previousPredictedPresentTime,
                                               nsecs_t previousActualPresentTime) {
    const bool presentTimeValid =
            mSurfaceFlingerActuals.presentTime >= mSurfaceFlingerActuals.startTime;
    if (mPredictionState == PredictionState::Expired || !presentTimeValid) {
        // Cannot do jank classification with expired predictions or invalid signal times. Set the
        // deltas to 0 as both negative and positive deltas are used as real values.
        mJankType.legacy() = JankType::Unknown;
        mJankSeverityTypeLegacy = JankSeverityType::Unknown;
        deadlineDelta = 0;
        displayPresentJitter = 0;
        if (!presentTimeValid) {
            mSurfaceFlingerActuals.presentTime = mSurfaceFlingerActuals.endTime;
            mJankType.legacy() |= JankType::DisplayHAL;
        }

        mJankType.experimental() =
                mDisplayState.poweredOn ? mJankType.legacy() : JankType::DisplayNotOn;
        return;
    }

    // Delta between the expected present and the actual present
    const nsecs_t presentDelay =
            mSurfaceFlingerActuals.presentTime - mSurfaceFlingerPredictions.presentTime;
    // Sf actual end time represents the CPU end time. In case of HWC, SF's end time would have
    // included the time for composition. However, for GPU composition, the final end time is max(sf
    // end time, gpu fence time).
    nsecs_t combinedEndTime = mSurfaceFlingerActuals.endTime;
    if (mGpuFence != FenceTime::NO_FENCE) {
        combinedEndTime = std::max(combinedEndTime, mGpuFence->getSignalTime());
    }
    deadlineDelta = combinedEndTime - mSurfaceFlingerPredictions.endTime;

    if (combinedEndTime > mSurfaceFlingerPredictions.endTime) {
        mFrameReadyMetadata = FrameReadyMetadata::LateFinish;
    } else {
        mFrameReadyMetadata = FrameReadyMetadata::OnTimeFinish;
    }

    // How far off was the presentDelay when compared to the vsyncPeriod. Used in checking if there
    // was a prediction error or not.
    displayPresentJitter = calculateDisplayPresentJitter(presentDelay, mRefreshRate);

    if (std::abs(mSurfaceFlingerActuals.startTime - mSurfaceFlingerPredictions.startTime) >
        mJankClassificationThresholds.startThreshold) {
        mFrameStartMetadata =
                mSurfaceFlingerActuals.startTime > mSurfaceFlingerPredictions.startTime
                ? FrameStartMetadata::LateStart
                : FrameStartMetadata::EarlyStart;
    }

    classifyJankLegacy(presentDelay, previousActualPresentTime);

    mPresentDelay = presentDelay;
    mActualPresentDelta = mSurfaceFlingerActuals.presentTime - previousActualPresentTime;
    mExpectedPresentDelta = mSurfaceFlingerPredictions.presentTime - previousPredictedPresentTime;

    const nsecs_t presentThreshold = mJankClassificationThresholds.presentThreshold;

    if (std::abs(presentDelay) <= presentThreshold) {
        mFramePresentMetadata.experimental() = FramePresentMetadata::OnTimePresent;
    } else {
        const nsecs_t presentationConsistencyDelay = mActualPresentDelta - mExpectedPresentDelta;
        const float deltaFramesRatio = mExpectedPresentDelta == 0
                ? 0
                : abs(static_cast<float>(presentationConsistencyDelay) /
                      static_cast<float>(mExpectedPresentDelta));
        const bool smoothAnimation =
                mExpectedPresentDelta < kThresholdFpsForAnimation.getPeriodNsecs() &&
                deltaFramesRatio <= kDeltaFramesRatioThreshold;
        if (smoothAnimation) {
            mFramePresentMetadata.experimental() = FramePresentMetadata::OnTimePresent;
            mJankDebugMetadata = deltaFramesRatio;
        } else {
            const auto delayRatio = static_cast<float>(std::abs(presentDelay)) /
                    static_cast<float>(mExpectedPresentDelta);
            if (delayRatio > kDeltaFramesRatioThreshold) {
                mFramePresentMetadata.experimental() = (presentationConsistencyDelay > 0)
                        ? FramePresentMetadata::LatePresent
                        : FramePresentMetadata::EarlyPresent;
            } else {
                // frame is delayed, it doesn't match the frame pacing but it also pretty far from
                // the previous frame. In that case, mark it as non animation.
                mFramePresentMetadata.experimental() = FramePresentMetadata::UnknownPresent;
            }
            mJankDebugMetadata = delayRatio;
        }
    }

    if (!mDisplayState.poweredOn) {
        mJankType.experimental() = JankType::DisplayNotOn;
        return;
    }

    if (mFramePresentMetadata.experimental() == FramePresentMetadata::OnTimePresent) {
        // Frames presented on time are not janky, but might be buffer stuffed.
        if (std::abs(presentDelay) <= presentThreshold) {
            mJankType.experimental() = JankType::None;
        } else if (presentDelay > mRefreshRate.getPeriodNsecs() - presentThreshold) {
            mJankType.experimental() = JankType::SurfaceFlingerStuffing;
        }
    } else if (mFramePresentMetadata.experimental() == FramePresentMetadata::EarlyPresent) {
        if (mFrameReadyMetadata == FrameReadyMetadata::OnTimeFinish) {
            // Finish on time, Present early
            if (delayMatchVsyncCadence(presentDelay, mRefreshRate, presentThreshold)) {
                // Delta is a factor of vsync if its within the presentTheshold on either side
                // of the vsyncPeriod. Example: 0-2ms and 9-11ms are both within the threshold
                // of the vsyncPeriod if the threshold was 2ms and the vsyncPeriod was 11ms.
                mJankType.experimental() = JankType::SurfaceFlingerScheduling;
            } else {
                // Delta is not a factor of vsync,
                mJankType.experimental() = JankType::PredictionError;
            }
        } else if (mFrameReadyMetadata == FrameReadyMetadata::LateFinish) {
            // Finish late, Present early
            mJankType.experimental() = JankType::SurfaceFlingerScheduling;
        } else {
            // Finish time unknown
            mJankType.experimental() = JankType::Unknown;
        }
    } else if (mFramePresentMetadata.experimental() == FramePresentMetadata::LatePresent) {
        if (mFrameReadyMetadata == FrameReadyMetadata::OnTimeFinish &&
            !(mJankType.experimental() & JankType::SurfaceFlingerStuffing)) {
            // Finish on time, Present late
            if (delayMatchVsyncCadence(presentDelay, mRefreshRate, presentThreshold)) {
                // Delta is a factor of vsync if its within the presentTheshold on either side
                // of the vsyncPeriod. Example: 0-2ms and 9-11ms are both within the threshold
                // of the vsyncPeriod if the threshold was 2ms and the vsyncPeriod was 11ms.
                mJankType.experimental() = JankType::DisplayHAL;
            } else {
                // Delta is not a factor of vsync
                mJankType.experimental() = JankType::PredictionError;
            }
        } else if (mFrameReadyMetadata == FrameReadyMetadata::LateFinish) {
            if (!(mJankType.experimental() & JankType::SurfaceFlingerStuffing) ||
                mSurfaceFlingerActuals.presentTime - previousActualPresentTime >
                        mRefreshRate.getPeriodNsecs() + presentThreshold) {
                // Classify CPU vs GPU if SF wasn't stuffed or if SF was stuffed but this frame
                // was presented more than a vsync late.
                if (mGpuFence != FenceTime::NO_FENCE) {
                    // If SF was in GPU composition, classify it as GPU deadline missed.
                    mJankType.experimental() = JankType::SurfaceFlingerGpuDeadlineMissed;
                } else {
                    mJankType.experimental() = JankType::SurfaceFlingerCpuDeadlineMissed;
                }
            }
        } else {
            // Finish time unknown
            mJankType.experimental() = JankType::Unknown;
        }
    } else {
        // present time unknown, mark the first as none animating
        mJankType.experimental() = JankType::NonAnimating;
    }

    if (mJankType.experimental() != JankType::None && mDisplayState.modeChangeInProgress) {
        mJankType.experimental() |= JankType::DisplayModeChangeInProgress;
    }

    if (mJankType.experimental() != JankType::None && mDisplayState.powerModeChangeInProgress) {
        mJankType.experimental() = JankType::DisplayPowerModeChangeInProgress;
    }
}

void FrameTimeline::DisplayFrame::onPresent(nsecs_t signalTime,
                                            nsecs_t previousPredictedPresentTime,
                                            nsecs_t previousActualPresentTime) {
    mSurfaceFlingerActuals.presentTime = signalTime;
    nsecs_t deadlineDelta = 0;
    nsecs_t displayPresentJitter = 0;
    classifyJank(deadlineDelta, displayPresentJitter, previousPredictedPresentTime,
                 previousActualPresentTime);
    const auto [score, severity] = calculateJankSeverity(mJankType.value(), mExpectedPresentDelta,
                                                         mActualPresentDelta, mRenderRate);
    mJankSeverity = severity;
    mJankScore = score;

    for (auto& surfaceFrame : mSurfaceFrames) {
        surfaceFrame->onPresent(signalTime, mJankType.legacy(), mJankType.experimental(),
                                mRefreshRate, mRenderRate, deadlineDelta, displayPresentJitter);
    }
}

void FrameTimeline::DisplayFrame::onCommitNotComposited() {
    for (auto& surfaceFrame : mSurfaceFrames) {
        surfaceFrame->onCommitNotComposited(mRefreshRate, mRenderRate);
    }
}

void FrameTimeline::DisplayFrame::tracePredictions(pid_t surfaceFlingerPid, nsecs_t monoBootOffset,
                                                   bool filterFramesBeforeTraceStarts) const {
    int64_t expectedTimelineCookie = mTraceCookieCounter.getCookieForTracing();
    bool traced = false;

    // Expected timeline start
    FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
        const auto timestamp = mSurfaceFlingerPredictions.startTime;
        if (filterFramesBeforeTraceStarts && !shouldTraceForDataSource(ctx, timestamp)) {
            // Do not trace packets started before tracing starts.
            return;
        }
        traced = true;

        auto packet = ctx.NewTracePacket();
        packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
        if (monoBootOffset > 0 &&
            FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
            monoBootOffset =
                    systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
        }
        packet->set_timestamp(static_cast<uint64_t>(timestamp + monoBootOffset));

        auto* event = packet->set_frame_timeline_event();
        auto* expectedDisplayFrameStartEvent = event->set_expected_display_frame_start();

        expectedDisplayFrameStartEvent->set_cookie(expectedTimelineCookie);

        expectedDisplayFrameStartEvent->set_token(mToken);
        expectedDisplayFrameStartEvent->set_pid(surfaceFlingerPid);
    });

    if (traced) {
        // Expected timeline end
        FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
            if (monoBootOffset > 0 &&
                FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                monoBootOffset =
                        systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
            }
            packet->set_timestamp(
                    static_cast<uint64_t>(mSurfaceFlingerPredictions.endTime + monoBootOffset));

            auto* event = packet->set_frame_timeline_event();
            auto* expectedDisplayFrameEndEvent = event->set_frame_end();

            expectedDisplayFrameEndEvent->set_cookie(expectedTimelineCookie);
        });
    }
}

void FrameTimeline::DisplayFrame::addSkippedFrame(pid_t surfaceFlingerPid, nsecs_t monoBootOffset,
                                                  nsecs_t previousPredictionPresentTime,
                                                  bool filterFramesBeforeTraceStarts) const {
    nsecs_t skippedFrameStartTime = 0, skippedFramePresentTime = 0;
    const constexpr float kThresh = 0.5f;
    const constexpr float kRange = 1.5f;
    for (auto& surfaceFrame : mSurfaceFrames) {
        if (previousPredictionPresentTime != 0 &&
            static_cast<float>(mSurfaceFlingerPredictions.presentTime -
                               previousPredictionPresentTime) >=
                    static_cast<float>(mRenderRate.getPeriodNsecs()) * kRange &&
            static_cast<float>(surfaceFrame->getPredictions().presentTime) <=
                    (static_cast<float>(mSurfaceFlingerPredictions.presentTime) -
                     kThresh * static_cast<float>(mRenderRate.getPeriodNsecs())) &&
            static_cast<float>(surfaceFrame->getPredictions().presentTime) >=
                    (static_cast<float>(previousPredictionPresentTime) +
                     kThresh * static_cast<float>(mRenderRate.getPeriodNsecs())) &&
            // sf skipped frame is not considered if app is self janked or display is not on
            surfaceFrame->getJankType() != JankType::None && !surfaceFrame->isSelfJanky() &&
            mDisplayState.poweredOn) {
            skippedFrameStartTime = surfaceFrame->getPredictions().endTime;
            skippedFramePresentTime = surfaceFrame->getPredictions().presentTime;
            break;
        }
    }

    // add slice
    if (skippedFrameStartTime != 0 && skippedFramePresentTime != 0) {
        int64_t actualTimelineCookie = mTraceCookieCounter.getCookieForTracing();
        bool traced = false;

        // Actual timeline start
        FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
            if (filterFramesBeforeTraceStarts &&
                !shouldTraceForDataSource(ctx, skippedFrameStartTime)) {
                // Do not trace packets started before tracing starts.
                return;
            }
            traced = true;

            auto packet = ctx.NewTracePacket();
            packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
            if (monoBootOffset > 0 &&
                FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                monoBootOffset =
                        systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
            }
            packet->set_timestamp(static_cast<uint64_t>(skippedFrameStartTime + monoBootOffset));

            auto* event = packet->set_frame_timeline_event();
            auto* actualDisplayFrameStartEvent = event->set_actual_display_frame_start();

            actualDisplayFrameStartEvent->set_cookie(actualTimelineCookie);

            actualDisplayFrameStartEvent->set_token(0);
            actualDisplayFrameStartEvent->set_pid(surfaceFlingerPid);
            actualDisplayFrameStartEvent->set_on_time_finish(mFrameReadyMetadata ==
                                                             FrameReadyMetadata::OnTimeFinish);
            actualDisplayFrameStartEvent->set_gpu_composition(false);
            actualDisplayFrameStartEvent->set_prediction_type(toProto(PredictionState::Valid));
            actualDisplayFrameStartEvent->set_present_type(FrameTimelineEvent::PRESENT_DROPPED);
            actualDisplayFrameStartEvent->set_jank_type(jankTypeBitmaskToProto(JankType::Dropped));
            actualDisplayFrameStartEvent->set_jank_severity_type(toProto(JankSeverityType::None));
            actualDisplayFrameStartEvent->set_jank_type_experimental(
                    jankTypeBitmaskToProto(JankType::Dropped));
            actualDisplayFrameStartEvent->set_present_type_experimental(
                    FrameTimelineEvent::PRESENT_DROPPED);
        });

        if (traced) {
            // Actual timeline end
            FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
                auto packet = ctx.NewTracePacket();
                packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
                if (monoBootOffset > 0 &&
                    FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                    monoBootOffset =
                            systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
                }
                packet->set_timestamp(
                        static_cast<uint64_t>(skippedFramePresentTime + monoBootOffset));

                auto* event = packet->set_frame_timeline_event();
                auto* actualDisplayFrameEndEvent = event->set_frame_end();

                actualDisplayFrameEndEvent->set_cookie(actualTimelineCookie);
            });
        }
    }
}

void FrameTimeline::DisplayFrame::traceActuals(pid_t surfaceFlingerPid, nsecs_t monoBootOffset,
                                               bool filterFramesBeforeTraceStarts) const {
    int64_t actualTimelineCookie = mTraceCookieCounter.getCookieForTracing();
    bool traced = false;

    // Actual timeline start
    FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
        const auto timestamp = mSurfaceFlingerActuals.startTime;
        if (filterFramesBeforeTraceStarts && !shouldTraceForDataSource(ctx, timestamp)) {
            // Do not trace packets started before tracing starts.
            return;
        }
        traced = true;

        auto packet = ctx.NewTracePacket();
        packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
        if (monoBootOffset > 0 &&
            FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
            monoBootOffset =
                    systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
        }
        packet->set_timestamp(static_cast<uint64_t>(timestamp + monoBootOffset));

        auto* event = packet->set_frame_timeline_event();
        auto* actualDisplayFrameStartEvent = event->set_actual_display_frame_start();

        actualDisplayFrameStartEvent->set_cookie(actualTimelineCookie);

        actualDisplayFrameStartEvent->set_token(mToken);
        actualDisplayFrameStartEvent->set_pid(surfaceFlingerPid);
        actualDisplayFrameStartEvent->set_present_type(toProto(mFramePresentMetadata.value()));
        actualDisplayFrameStartEvent->set_on_time_finish(mFrameReadyMetadata ==
                                                         FrameReadyMetadata::OnTimeFinish);
        actualDisplayFrameStartEvent->set_gpu_composition(mGpuFence != FenceTime::NO_FENCE);
        actualDisplayFrameStartEvent->set_jank_type(jankTypeBitmaskToProto(mJankType.value()));
        actualDisplayFrameStartEvent->set_prediction_type(toProto(mPredictionState));
        actualDisplayFrameStartEvent->set_present_type_experimental(
                toProto(mFramePresentMetadata.altValue()));
        actualDisplayFrameStartEvent->set_jank_type_experimental(
                jankTypeBitmaskToProto(mJankType.altValue()));
        actualDisplayFrameStartEvent->set_present_delay_millis(mPresentDelay / 1e6f);
        actualDisplayFrameStartEvent->set_jank_debug_metadata(mJankDebugMetadata);
        actualDisplayFrameStartEvent->set_jank_severity_score(mJankScore);
        actualDisplayFrameStartEvent->set_jank_severity_type(toProto(mJankSeverity));
    });

    if (traced) {
        // Actual timeline end
        FrameTimelineDataSource::Trace([&](FrameTimelineDataSource::TraceContext ctx) {
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
            auto presentTime = mSurfaceFlingerActuals.presentTime;
            if (presentTime <= mSurfaceFlingerActuals.startTime) {
                // this can happen when the display is off and we use a stale fence
                presentTime = mSurfaceFlingerActuals.startTime + ms2ns(4);
            }
            if (monoBootOffset > 0 &&
                FlagManager::getInstance().frametimeline_boottime_in_lambda()) {
                monoBootOffset =
                        systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC);
            }
            packet->set_timestamp(static_cast<uint64_t>(presentTime + monoBootOffset));

            auto* event = packet->set_frame_timeline_event();
            auto* actualDisplayFrameEndEvent = event->set_frame_end();

            actualDisplayFrameEndEvent->set_cookie(actualTimelineCookie);
        });
    }
}

nsecs_t FrameTimeline::DisplayFrame::trace(pid_t surfaceFlingerPid, nsecs_t monoBootOffset,
                                           nsecs_t previousPredictionPresentTime,
                                           bool filterFramesBeforeTraceStarts) const {
    const auto result = (mPredictionState == PredictionState::Valid)
            ? mSurfaceFlingerPredictions.presentTime
            : previousPredictionPresentTime;
    if (mSurfaceFrames.empty()) {
        // We don't want to trace display frames without any surface frames updates as this cannot
        // be janky
        return result;
    }

    if (mToken == FrameTimelineInfo::INVALID_VSYNC_ID) {
        // DisplayFrame should not have an invalid token.
        ALOGE("Cannot trace DisplayFrame with invalid token");
        return result;
    }

    if (mPredictionState == PredictionState::Valid) {
        // Expired and unknown predictions have zeroed timestamps. This cannot be used in any
        // meaningful way in a trace.
        tracePredictions(surfaceFlingerPid, monoBootOffset, filterFramesBeforeTraceStarts);
    }
    traceActuals(surfaceFlingerPid, monoBootOffset, filterFramesBeforeTraceStarts);

    for (auto& surfaceFrame : mSurfaceFrames) {
        surfaceFrame->trace(mToken, monoBootOffset, filterFramesBeforeTraceStarts);
    }

    addSkippedFrame(surfaceFlingerPid, monoBootOffset, previousPredictionPresentTime,
                    filterFramesBeforeTraceStarts);
    return result;
}

float FrameTimeline::computeFps(const std::unordered_set<int32_t>& layerIds) {
    if (layerIds.empty()) {
        return 0.0f;
    }

    std::vector<nsecs_t> presentTimes;
    {
        std::scoped_lock lock(mMutex);
        presentTimes.reserve(mDisplayFrames.size());
        for (size_t i = 0; i < mDisplayFrames.size(); i++) {
            const auto& displayFrame = mDisplayFrames[i];
            if (displayFrame->getActuals().presentTime <= 0) {
                continue;
            }
            for (const auto& surfaceFrame : displayFrame->getSurfaceFrames()) {
                if (surfaceFrame->getPresentState() == SurfaceFrame::PresentState::Presented &&
                    layerIds.count(surfaceFrame->getLayerId()) > 0) {
                    // We're looking for DisplayFrames that presents at least one layer from
                    // layerIds, so push the present time and skip looking through the rest of the
                    // SurfaceFrames.
                    presentTimes.push_back(displayFrame->getActuals().presentTime);
                    break;
                }
            }
        }
    }

    // FPS can't be computed when there's fewer than 2 presented frames.
    if (presentTimes.size() <= 1) {
        return 0.0f;
    }

    nsecs_t priorPresentTime = -1;
    nsecs_t totalPresentToPresentWalls = 0;

    for (const nsecs_t presentTime : presentTimes) {
        if (priorPresentTime == -1) {
            priorPresentTime = presentTime;
            continue;
        }

        totalPresentToPresentWalls += (presentTime - priorPresentTime);
        priorPresentTime = presentTime;
    }

    if (CC_UNLIKELY(totalPresentToPresentWalls <= 0)) {
        ALOGW("Invalid total present-to-present duration when computing fps: %" PRId64,
              totalPresentToPresentWalls);
        return 0.0f;
    }

    const constexpr nsecs_t kOneSecond =
            std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count();
    // (10^9 nanoseconds / second) * (N present deltas) / (total nanoseconds in N present deltas) =
    // M frames / second
    return kOneSecond * static_cast<nsecs_t>((presentTimes.size() - 1)) /
            static_cast<float>(totalPresentToPresentWalls);
}

void FrameTimeline::generateFrameStats(int32_t layer, size_t count, FrameStats* outStats) const {
    std::scoped_lock lock(mMutex);

    // TODO: Include FPS calculation here
    for (auto displayFrame : mDisplayFrames) {
        if (!count--) {
            break;
        }

        if (displayFrame->getActuals().presentTime <= 0) {
            continue;
        }

        for (const auto& surfaceFrame : displayFrame->getSurfaceFrames()) {
            if (surfaceFrame->getLayerId() == layer) {
                outStats->actualPresentTimesNano.push_back(surfaceFrame->getActuals().presentTime);
                outStats->desiredPresentTimesNano.push_back(
                        surfaceFrame->getActuals().desiredPresentTime);
                outStats->frameReadyTimesNano.push_back(surfaceFrame->getActuals().endTime);
            }
        }
    }
}

std::optional<size_t> FrameTimeline::getFirstSignalFenceIndex() const {
    for (size_t i = 0; i < mPendingPresentFences.size(); i++) {
        const auto& [fence, _] = mPendingPresentFences[i];
        if (fence && fence->getSignalTime() != Fence::SIGNAL_TIME_PENDING) {
            return i;
        }
    }

    return {};
}

void FrameTimeline::flushPendingPresentFences() {
    const auto firstSignaledFence = getFirstSignalFenceIndex();
    if (!firstSignaledFence.has_value()) {
        return;
    }

    // Perfetto is using boottime clock to void drifts when the device goes
    // to suspend.
    const auto monoBootOffset = mUseBootTimeClock
            ? (systemTime(SYSTEM_TIME_BOOTTIME) - systemTime(SYSTEM_TIME_MONOTONIC))
            : 0;

    // Present fences are expected to be signaled in order. Mark all the previous
    // pending fences as errors.
    for (size_t i = 0; i < firstSignaledFence.value(); i++) {
        const auto& pendingPresentFence = *mPendingPresentFences.begin();
        const nsecs_t signalTime = Fence::SIGNAL_TIME_INVALID;
        auto& displayFrame = pendingPresentFence.second;
        displayFrame->onPresent(signalTime, mPreviousPredictionPresentTime,
                                mPreviousActualPresentTime);
        mPreviousPredictionPresentTime =
                displayFrame->trace(mSurfaceFlingerPid, monoBootOffset,
                                    mPreviousPredictionPresentTime, mFilterFramesBeforeTraceStarts);
        mPendingPresentFences.erase(mPendingPresentFences.begin());
    }

    for (size_t i = 0; i < mPendingPresentFences.size(); i++) {
        const auto& pendingPresentFence = mPendingPresentFences[i];
        nsecs_t signalTime = Fence::SIGNAL_TIME_INVALID;
        if (pendingPresentFence.first && pendingPresentFence.first->isValid()) {
            signalTime = pendingPresentFence.first->getSignalTime();
            if (signalTime == Fence::SIGNAL_TIME_PENDING) {
                break;
            }
        }

        auto& displayFrame = pendingPresentFence.second;
        displayFrame->onPresent(signalTime, mPreviousPredictionPresentTime,
                                mPreviousActualPresentTime);

        mPreviousPredictionPresentTime =
                displayFrame->trace(mSurfaceFlingerPid, monoBootOffset,
                                    mPreviousPredictionPresentTime, mFilterFramesBeforeTraceStarts);
        mPreviousActualPresentTime = signalTime;

        mPendingPresentFences.erase(mPendingPresentFences.begin() + static_cast<int>(i));
        --i;
    }
}

void FrameTimeline::finalizeCurrentDisplayFrame() {
    while (mDisplayFrames.size() >= mMaxDisplayFrames) {
        // We maintain only a fixed number of frames' data. Pop older frames
        mDisplayFrames.pop_front();
    }
    mDisplayFrames.push_back(mCurrentDisplayFrame);
    mCurrentDisplayFrame.reset();
    mCurrentDisplayFrame = std::make_shared<DisplayFrame>(mTimeStats, mJankClassificationThresholds,
                                                          &mTraceCookieCounter);
}

nsecs_t FrameTimeline::DisplayFrame::getBaseTime() const {
    nsecs_t baseTime =
            getMinTime(mPredictionState, mSurfaceFlingerPredictions, mSurfaceFlingerActuals);
    for (const auto& surfaceFrame : mSurfaceFrames) {
        nsecs_t surfaceFrameBaseTime = surfaceFrame->getBaseTime();
        if (surfaceFrameBaseTime != 0) {
            baseTime = std::min(baseTime, surfaceFrameBaseTime);
        }
    }
    return baseTime;
}

void FrameTimeline::DisplayFrame::dumpJank(std::string& result, nsecs_t baseTime,
                                           int displayFrameCount) const {
    if (mJankType.value() == JankType::None) {
        // Check if any Surface Frame has been janky
        bool isJanky = false;
        for (const auto& surfaceFrame : mSurfaceFrames) {
            if (surfaceFrame->getJankType() != JankType::None) {
                isJanky = true;
                break;
            }
        }
        if (!isJanky) {
            return;
        }
    }
    StringAppendF(&result, "Display Frame %d", displayFrameCount);
    dump<std::milli>(result, baseTime);
}

template <typename Period>
void FrameTimeline::DisplayFrame::dumpAll(std::string& result, nsecs_t baseTime) const {
    dump<Period>(result, baseTime);
}

template <typename Period>
void FrameTimeline::DisplayFrame::dump(std::string& result, nsecs_t baseTime) const {
    if (mJankType.value() != JankType::None) {
        // Easily identify a janky Display Frame in the dump
        StringAppendF(&result, " [*] ");
    }
    StringAppendF(&result, "\n");
    StringAppendF(&result, "Prediction State : %s\n", toString(mPredictionState).c_str());
    StringAppendF(&result, "Jank Type : %s\n", jankTypeBitmaskToString(mJankType.value()).c_str());
    StringAppendF(&result, "Present Metadata : %s\n",
                  toString(mFramePresentMetadata.value()).c_str());
    StringAppendF(&result, "Finish Metadata: %s\n", toString(mFrameReadyMetadata).c_str());
    StringAppendF(&result, "Start Metadata: %s\n", toString(mFrameStartMetadata).c_str());
    std::chrono::nanoseconds vsyncPeriod(mRefreshRate.getPeriodNsecs());
    StringAppendF(&result, "Vsync Period: %10f\n",
                  std::chrono::duration<double, Period>(vsyncPeriod).count());
    nsecs_t presentDelta =
            mSurfaceFlingerActuals.presentTime - mSurfaceFlingerPredictions.presentTime;
    std::chrono::nanoseconds presentDeltaNs(std::abs(presentDelta));
    StringAppendF(&result, "Present delta: %10f\n",
                  std::chrono::duration<double, Period>(presentDeltaNs).count());
    std::chrono::nanoseconds deltaToVsync(std::abs(presentDelta) % mRefreshRate.getPeriodNsecs());
    StringAppendF(&result, "Present delta %% refreshrate: %10f\n",
                  std::chrono::duration<double, Period>(deltaToVsync).count());
    StringAppendF(&result, "Jank Debug Metadata: %10f\n", mJankDebugMetadata);
    dumpTable<Period>(result, mSurfaceFlingerPredictions, mSurfaceFlingerActuals, "",
                      mPredictionState, baseTime);
    StringAppendF(&result, "\n");
    std::string indent = "    "; // 4 spaces
    for (const auto& surfaceFrame : mSurfaceFrames) {
        surfaceFrame->dump<Period>(result, indent, baseTime);
    }
    StringAppendF(&result, "\n");
}

std::string FrameTimeline::dumpStateForTesting() {
    std::scoped_lock lock(mMutex);
    std::string result;
    StringAppendF(&result, "Number of display frames : %d\n", (int)mDisplayFrames.size());
    for (size_t i = 0; i < mDisplayFrames.size(); i++) {
        StringAppendF(&result, "Display Frame %d", static_cast<int>(i));
        mDisplayFrames[i]->dumpAll<std::nano>(result, /*baseTime*/ 0);
    }

    return result;
}

void FrameTimeline::dumpAll(std::string& result) {
    std::scoped_lock lock(mMutex);
    StringAppendF(&result, "Number of display frames : %d\n", (int)mDisplayFrames.size());
    nsecs_t baseTime = (mDisplayFrames.empty()) ? 0 : mDisplayFrames[0]->getBaseTime();
    for (size_t i = 0; i < mDisplayFrames.size(); i++) {
        StringAppendF(&result, "Display Frame %d", static_cast<int>(i));
        mDisplayFrames[i]->dumpAll<std::milli>(result, baseTime);
    }
}

void FrameTimeline::dumpJank(std::string& result) {
    std::scoped_lock lock(mMutex);
    nsecs_t baseTime = (mDisplayFrames.empty()) ? 0 : mDisplayFrames[0]->getBaseTime();
    for (size_t i = 0; i < mDisplayFrames.size(); i++) {
        mDisplayFrames[i]->dumpJank(result, baseTime, static_cast<int>(i));
    }
}

void FrameTimeline::parseArgs(const Vector<String16>& args, std::string& result) {
    SFTRACE_CALL();
    std::unordered_map<std::string, bool> argsMap;
    for (size_t i = 0; i < args.size(); i++) {
        argsMap[std::string(String8(args[i]).c_str())] = true;
    }
    if (argsMap.count("-jank")) {
        dumpJank(result);
    }
    if (argsMap.count("-all")) {
        dumpAll(result);
    }
}

void FrameTimeline::setMaxDisplayFrames(uint32_t size) {
    std::scoped_lock lock(mMutex);

    // The size can either increase or decrease, clear everything, to be consistent
    mDisplayFrames.clear();
    mPendingPresentFences.clear();
    mMaxDisplayFrames = size;
}

void FrameTimeline::reset() {
    setMaxDisplayFrames(kDefaultMaxDisplayFrames);
}

void FrameTimeline::onLayerDestroyed(int32_t layerId) {
    std::scoped_lock lock(mMutex);
    mPreviousSurfaceFrames.erase(layerId);
}

} // namespace impl
} // namespace android::scheduler
