/*
 * Copyright 2025 The Android Open Source Project
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

#include <ftl/enum.h>
#include <log/log.h>
#include <utils/Timers.h>
#include <array>
#include <atomic>
#include "ftl/concat.h"
#include "log/log_main.h"
#include "ui/DisplayId.h"
#include "ui/DisplayMap.h"

// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡀⠀⠀⠀⠀⠀⠀⢀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠱⡀⠀⠀⢣⠀⠀⢀⡄⠀⠀⡜⠀⠀⢀⠎⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⢣⠀⠀⠀⢣⠀⠀⢸⡀⢀⣾⣷⡄⠀⡇⠀⠀⡘⠀⠀⢀⡜⠀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠈⠢⡀⠀⠀⠱⡀⠀⠈⡆⠀⠀⣧⣾⣿⣿⣿⣼⠀⠀⢰⠁⠀⢀⠞⠀⠀⢀⡔⠁⠀⠀⠀⠀⠀
// ⠀⠀⠀⠢⡀⠀⠀⠙⢦⠀⠀⠱⡄⠀⠸⡄⢠⣿⣿⠃⠈⢿⣿⣆⢀⠏⠀⢠⠎⠀⠀⡰⠋⠀⠀⢀⠄⠀⠀⠀
// ⠀⠀⠀⠀⠈⠢⣀⠀⠀⠳⣄⠀⠘⣆⠀⣻⣿⣿⠃⠀⠀⠈⢿⣿⣿⠀⣰⠋⠀⣠⠞⠀⠀⣠⠔⠁⠀⠀⠀⠀
// ⠀⠐⠢⢄⡀⠀⠈⠓⢤⡀⠈⢳⣄⠘⣶⣿⡿⠁⠀⠀⠀⠀⠈⢻⣿⣿⠃⢠⡾⠁⢀⡤⠚⠁⠀⢀⡠⠔⠀⠀
// ⠀⠀⠀⠀⠈⠒⢤⣀⠀⠙⢦⣄⠙⣿⣿⡟⠁⠀⠀⠀⠀⠀⠀⠀⠹⣿⣿⡏⣠⡴⠋⠀⣀⡤⠒⠁⠀⠀⠀⠀
// ⠈⠁⠒⠦⢄⣀⠀⠈⠛⠶⣤⣙⣿⣿⠟⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠹⣿⣿⣏⣤⠶⠋⠁⢀⣀⠤⠔⠒⠉⠁
// ⢀⣀⠀⠀⠀⠈⠉⠓⠶⢦⣬⣿⣿⠏⠀⣀⣤⡶⠶⠾⠿⠷⠶⣶⣤⣀⠘⣿⣿⣥⡴⠖⠛⠉⠀⠀⠀⣀⣀⡀
// ⠀⠀⠉⠉⠙⠒⠒⠲⠶⣤⣿⣿⣋⣴⣿⣭⡤⠶⣶⣶⣶⣶⡶⠶⣬⣝⣳⣼⣿⣿⣶⠶⠒⠒⠊⠉⠉⠀⠀⠀
// ⠀⠠⠤⠤⠤⠤⠤⠤⣴⣿⣿⡿⠟⠉⠀⣿⠀⢰⣷⣼⣿⣿⡧⠀⢸⡇⠉⠛⠿⣿⣿⣶⠖⠂⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⣀⣀⣠⣴⣿⡿⠙⢄⠀⠀⠀⢻⣄⠈⠻⣿⣿⡿⠃⢀⣾⠃⠀⠀⢀⠜⢻⣿⣷⡤⢤⣀⣀⡀⠀⠀
// ⠀⠈⠉⠉⠀⠀⣼⣿⡿⠁⠀⠀⠙⠶⣤⣀⡻⣦⣄⣀⣀⣀⣤⠞⣁⣠⡴⠞⠁⠀⠀⢻⣿⣷⡀⠀⠀⠈⠉⠁
// ⠀⢀⡀⠤⠰⣾⣿⡟⠀⠀⠀⠀⠀⠀⠀⠉⠛⠛⠻⠿⠿⠿⠛⠛⠋⠁⠀⠀⠀⠀⠀⠀⠹⣿⣿⡓⠲⠤⢄⡀
// ⠀⠁⠀⢀⣾⣿⠏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⣿⡄⠀⠀⠀
// ⠀⠀⣠⣾⣿⣯⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣸⣿⣿⣦⠀⠀
// ⠀⠠⠿⠿⠿⡿⠿⠿⠿⢿⠿⠿⢿⡿⠿⢿⠿⢿⡿⠿⡿⠿⣿⠿⢿⡿⠿⣿⠿⠿⢿⠿⠿⠿⠿⣿⠿⠿⠆⠀
// ⠀⠀⠀⠀⠊⠀⠀⠀⡴⠃⠀⢀⠞⠀⢠⠏⠀⢸⠁⠀⡇⠀⢹⠀⠈⢧⠀⠈⢦⠀⠀⠑⢄⠀⠀⠈⠁⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⡠⠊⠀⠀⢠⠋⠀⠀⡞⠀⠀⡼⠀⠀⡇⠀⠘⡆⠀⠘⡄⠀⠀⢣⠀⠀⠈⠑⡀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⠃⠀⠀⢸⠁⠀⠀⡇⠀⠀⡇⠀⠀⢧⠀⠀⠸⡀⠀⠀⠱⡀⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀⠀⢠⠃⠀⠀⢸⠀⠀⠀⡇⠀⠀⠸⠀⠀⠀⢣⠀⠀⠀⠁⠀⠀⠀⠀⠀⠀⠀⠀
// ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
//
namespace android::panopticon {

// This is the workload type that we want to trace. Equivalent to a perfetto track.
// Prefix by CG_ so that trace markers remain stable
enum class Source : int32_t { CG_FrameSignal = 1, ftl_last = CG_FrameSignal };

// Subcomponent of a workload that we want to trace. Equivalent to a perfetto slice.
// Prefix by CG_ so that trace markers remain stable
enum class SliceType : int32_t {
    CG_Sf_FrameSignal = 0,
    CG_Sf_Commit,
    CG_Sf_Composite,
    CG_Hwc_Present,
    CG_Hwc_Validate,
    CG_Hwc_PresentOrValidate,
    CG_Re_drawLayers,
    CG_Re_gpu,
    CG_Skia_flush,
    CG_Skia_submit,
    ftl_last = CG_Skia_submit
};

class Panopticon;

// Tracks a perfetto slice
struct Slice {
    std::atomic_int64_t startTime = -1;
    std::atomic_int64_t endTime = -1;
    std::atomic_bool isTracing = false;
    std::atomic_int32_t cookie = -1;

    void init(std::string_view track, SliceType slice, int32_t tracingCookie);
    void terminate(std::string_view track);
    int64_t duration() {
        auto end = endTime.load(std::memory_order_acquire);
        auto start = startTime.load(std::memory_order_acquire);
        if (end < 0 || start < 0) {
            return -1;
        }
        return end - start;
    }
};

struct Summary {
    std::atomic_int32_t gpuRenderedLayers = -1;
    std::atomic_int32_t dpuRenderedLayers = -1;
};

// RAII handle for referring to a slice. Ends the slice when destroyed
class SliceToken {
public:
    explicit SliceToken(SliceType type, const std::shared_ptr<Panopticon>& parent);

    ~SliceToken();

    // ban copies
    SliceToken(const SliceToken&) = delete;
    SliceToken& operator=(const SliceToken&) = delete;

    // moves are okay
    SliceToken(SliceToken&&) = default;
    SliceToken& operator=(SliceToken&&) = default;

private:
    SliceType mType;
    std::string mKey;
    int32_t mSliceCookie;

    friend class Panopticon;
};

// A Panopticon is a set performance slices and metadata describing a workload. This class makes it
// easier to integrate with Perfetto and statsd without significant divergence in the data that we
// would send to those systems.
//
// A Panopticon is also a theoretical prison design representing total control by an institution.
// See literature from Bentham and Foucault. These two Panopticons are similar, because they
// create the appearance of perpetual observation to encourages self-regulation. And of course,
// everything is a prison.
class Panopticon : public std::enable_shared_from_this<Panopticon> {
public:
    static std::shared_ptr<Panopticon> make(Source source, const char* suffix, int64_t vsyncId) {
        auto panopticon = std::shared_ptr<Panopticon>(new Panopticon(source, suffix, vsyncId));
        return panopticon;
    }

    ~Panopticon();

    // ban copies and moves
    Panopticon(const Panopticon&) = delete;
    Panopticon& operator=(const Panopticon&) = delete;
    Panopticon(Panopticon&&) = delete;
    Panopticon& operator=(Panopticon&&) = delete;

    // Simlar to SFTRACE_CALL OR SFTRACE_NAME
    SliceToken makeSlice(SliceType type) { return SliceToken(type, shared_from_this()); }

    void reportGpuRenderedLayers(int32_t value) { mSummary.gpuRenderedLayers = value; }
    void reportDpuRenderedLayers(int32_t value) { mSummary.dpuRenderedLayers = value; }

    SliceToken makeTopSlice() { return makeSlice(topSlice()); }

    std::string_view getSourceName() {
        auto name = ftl::enum_name(mSource);
        LOG_ALWAYS_FATAL_IF(!name, "Invalid type: %d", mSource);
        return *name;
    }

    Summary& editSummary() { return mSummary; }

private:
    Source mSource;
    int64_t mVsyncId;
    std::string mTrack;
    std::string mKey;
    int32_t mBaseCookie;
    std::atomic_int32_t mTracingCookie;

    std::array<Slice, ftl::enum_size_v<SliceType>> mSlices;
    Summary mSummary;

    Panopticon(Source source, const char* prefix, int64_t vsyncId);

    Slice& sliceForType(SliceType type) {
        return mSlices[static_cast<size_t>(ftl::to_underlying(type))];
    }

    std::string_view getKey() { return mKey; }

    std::string_view getTrackName() { return mTrack.data(); }

    SliceType topSlice() {
        switch (mSource) {
            case Source::CG_FrameSignal:
                return SliceType::CG_Sf_FrameSignal;
        }
    }

    friend class SliceToken;
};

// RAII handle for handling a registration for a thread's Panopticon
class PanopticonRegistration {
public:
    PanopticonRegistration(std::string id, std::shared_ptr<Panopticon> panopticon)
          : mId(std::move(id)), mPanopticon(std::move(panopticon)) {}

    void start();

    ~PanopticonRegistration();

    // ban copies and moves
    PanopticonRegistration(const PanopticonRegistration&) = delete;
    PanopticonRegistration& operator=(const PanopticonRegistration&) = delete;
    PanopticonRegistration(PanopticonRegistration&& other) = delete;
    PanopticonRegistration& operator=(PanopticonRegistration&& other) = delete;

private:
    std::string mId;
    std::shared_ptr<Panopticon> mPanopticon;
};

class ExclusiveToken {
public:
    ExclusiveToken(std::string id);
    ~ExclusiveToken() { cleanup(); }

    // ban copies
    ExclusiveToken(const ExclusiveToken&) = delete;
    ExclusiveToken& operator=(const ExclusiveToken&) = delete;
    ExclusiveToken(ExclusiveToken&& other) {
        mMovedFrom = other.mMovedFrom;
        other.mMovedFrom = true;
    }
    // This operator is deleted, since it seems difficult to use correctly with
    // our current `cleanup` implementation; see discussion on
    // ag/38394850/comment/350611e0_abf207a5/ . If this is needed, cleanup()
    // will need to search for the right entry.
    ExclusiveToken& operator=(ExclusiveToken&& other) = delete;

private:
    bool mMovedFrom = false;

    void cleanup();
};

// Bunch o' typedefs for common containers that are "reasonably" sized
using PanopticonRegistrations = ftl::SmallVector<std::shared_ptr<PanopticonRegistration>, 10>;
using SliceTokens = ftl::SmallVector<SliceToken, 10>;
using Ids = ftl::SmallVector<std::string, 10>;

// "registrate" because register is a reserved keyword :(
// Registers a new Panopticon to this thread. When this PanopticonRegistration is destroyed, this
// thread's Panopticon is also removed from thread-local storage, so that we don't leak.
void make(std::string id, Source source, int64_t vsyncId);
void make(Ids ids, Source source, int64_t vsyncId);

// "Borrows" a Panopticon for sharing to a new thread. Typical usage is to call
// PanopticonRegistration::register immediately after hopping to the new thread. Do not retain this
// PanopticonRegistration on the original thread.
std::shared_ptr<PanopticonRegistration> share();
PanopticonRegistrations share(Ids ids);

// Registers this id for *exclusive* tracing. must have first called registrate.
[[nodiscard]] ExclusiveToken exclusive(std::string id);

// Creates a trace slice. Simlar to SFTRACE_CALL OR SFTRACE_NAME
[[nodiscard]] SliceTokens slice(SliceType sliceType);

void reportGpuRenderedLayers(int32_t value);
void reportDpuRenderedLayers(int32_t value);

// Effectively terminates this thread's Panopticon by removing from thread-local storage, even if a
// PanopticonRegistration has not yet been destroyed. This is useful for a thread reporting that its
// work is done when deep inside of a call stack. Note that this does not necessarily terminate the
// *workload* slicing itself, since the Panopticon may have moved onto another thread.
void terminate(std::string id);
// Terminates *all* of this thread's panopticons
void terminate();

} // namespace android::panopticon
