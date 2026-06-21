/*
 * Copyright 2019 The Android Open Source Project
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

#include <cinttypes>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <android-base/thread_annotations.h>
#include <ui/DisplayId.h>
#include <ui/FenceTime.h>

#include <scheduler/TimeKeeper.h>

#include "VSyncTracker.h"
#include "VsyncController.h"

#define VSYNC_PREDICTION_ERROR_REPORT "VsyncPredictionError" // used for metrics - do not change

namespace android::scheduler {

class Clock;
class VSyncDispatch;
class VSyncTracker;

// TODO (b/145217110): consider renaming.
class VSyncReactor : public VsyncController {
public:
    VSyncReactor(PhysicalDisplayId, std::unique_ptr<Clock> clock, VSyncTracker& tracker,
                 size_t pendingFenceLimit, bool supportKernelIdleTimer);
    ~VSyncReactor();

    bool addPresentFence(std::shared_ptr<FenceTime>) final;
    void setIgnorePresentFences(bool ignore) final;

    void onDisplayModeChanged(ftl::NonNull<DisplayModePtr>, bool force) final;

    bool addHwVsyncTimestamp(nsecs_t timestamp, std::optional<nsecs_t> hwcVsyncPeriod,
                             bool* periodFlushed, VSyncTracker::VsyncTimeSource source) final;

    void setDisplayPowerMode(hal::PowerMode powerMode) final;

    bool isModeChangeInProgress() const final { return mModeChangeInProgress; }

    void resetModel() final;

    void dump(std::string& result) const final;

    /*
     * A helper struct to report the VSync prediction accuracy.
     * Used for metrics - do not change.
     */
    // TODO(b/483155349): Use Panopticon to provide a structured metric source for this data instead
    // of packing it into a string.
    struct ModelAccuracyMetric {
        VSyncTracker::ModelAccuracy accuracy;
        VSyncTracker::VsyncTimeSource source;
        bool modeChangeInProgress;
        bool accepted;

        std::string to_string() const {
            std::string result =
                    base::StringPrintf("error= %.4f, actualNs= %" PRId64 ", predictedNs= %" PRId64
                                       ", VsyncTimeSource= %s, VsyncPeriod= %.2f, "
                                       "VsyncPeriodsElapsed= %.2f, modeChangeInProgress= %d, "
                                       "accepted= %d",
                                       static_cast<float>(accuracy.modelErrorNs) / 1e6f,
                                       accuracy.actualVsync, accuracy.predictedVsync,
                                       ftl::enum_string(source).c_str(),
                                       static_cast<float>(accuracy.idealPeriod) / 1e6f,
                                       accuracy.vsyncPeriodsElapsed, modeChangeInProgress,
                                       accepted);
            if (accuracy.hwVsyncStability.error) {
                base::StringAppendF(&result, ", hwVsyncError= %.2f",
                                    static_cast<float>(*accuracy.hwVsyncStability.error) / 1e6f);
            }
            if (accuracy.hwVsyncStability.stddev) {
                base::StringAppendF(&result, ", hwVsyncStabStd= %.2f",
                                    static_cast<float>(*accuracy.hwVsyncStability.stddev) / 1e6f);
            }
            return result;
        }
    };

private:
    void reportModelAccuracyMetric(VSyncTracker::ModelAccuracy accuracy,
                                   VSyncTracker::VsyncTimeSource source, bool accepted) const
            REQUIRES(mMutex);
    bool addVsyncTimestampLocked(nsecs_t timestamp, VSyncTracker::VsyncTimeSource source)
            REQUIRES(mMutex);
    void setIgnorePresentFencesInternal(bool ignore) REQUIRES(mMutex);
    void updateIgnorePresentFencesInternal() REQUIRES(mMutex);
    void startPeriodTransitionInternal(ftl::NonNull<DisplayModePtr>) REQUIRES(mMutex);
    void endPeriodTransition() REQUIRES(mMutex);
    bool periodConfirmed(nsecs_t vsync_timestamp, std::optional<nsecs_t> hwcVsyncPeriod)
            REQUIRES(mMutex);
    bool updateTrackerWithSignaledFences() REQUIRES(mMutex);

    const PhysicalDisplayId mId;
    std::unique_ptr<Clock> const mClock;
    VSyncTracker& mTracker;
    size_t const mPendingLimit;

    mutable std::mutex mMutex;
    bool mInternalIgnoreFences GUARDED_BY(mMutex) = false;
    bool mExternalIgnoreFences GUARDED_BY(mMutex) = false;
    std::vector<std::shared_ptr<android::FenceTime>> mUnfiredFences GUARDED_BY(mMutex);

    bool mMoreSamplesNeeded GUARDED_BY(mMutex) = false;
    bool mPeriodConfirmationInProgress GUARDED_BY(mMutex) = false;
    DisplayModePtr mModePtrTransitioningTo GUARDED_BY(mMutex);
    std::atomic<bool> mModeChangeInProgress = false;
    std::optional<DisplayModeId> mDisplayModeId GUARDED_BY(mMutex);

    class LastHwVsync {
    public:
        LastHwVsync() { reset(); }
        void reset() {
            mFirst = true;
            mVsync.reset();
        }
        bool isFirst() const { return mFirst; }
        std::optional<nsecs_t> get() const { return mVsync; }
        void set(nsecs_t vsync) {
            if (mVsync.has_value()) {
                mFirst = false;
            }
            mVsync = vsync;
        }

    private:
        bool mFirst;
        std::optional<nsecs_t> mVsync;
    };
    LastHwVsync mLastHwVsync GUARDED_BY(mMutex);

    hal::PowerMode mDisplayPowerMode GUARDED_BY(mMutex) = hal::PowerMode::ON;

    const bool mSupportKernelIdleTimer = false;
};

class SystemClock : public Clock {
    nsecs_t now() const final;
};

} // namespace android::scheduler
