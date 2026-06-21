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
#include "common/Panopticon.h"
#include <trace_categories.h>
#include <atomic>
#include <string_view>
#include "common/trace.h"
#include "perfetto/public/te_macros.h"

namespace android::panopticon {

namespace {

std::string_view validateAndStringify(SliceType type) {
    auto name = ftl::enum_name(type);
    LOG_ALWAYS_FATAL_IF(!name, "Invalid type: %d", type);
    return *name;
}

// lol imagine having to debug this
thread_local ftl::SmallMap<std::string, std::shared_ptr<Panopticon>, 10> sPanopticons;
// Stack of IDs to exclusively trace.
// When this is empty, then all panopticons are traced.
thread_local Ids sExclusiveIdStack;

std::optional<SliceToken> slice(std::string id, SliceType sliceType) {
    if (auto panopticon = sPanopticons.get(id.c_str()); panopticon) {
        return panopticon->get()->makeSlice(sliceType);
    }

    return std::nullopt;
}

template <std::invocable<Summary&> F>
void report(std::string id, F reporter) {
    if (auto panopticon = sPanopticons.get(id.c_str()); panopticon) {
        reporter(panopticon->get()->editSummary());
    }
}

template <std::invocable<Summary&> F>
void report(F reporter) {
    if (sExclusiveIdStack.size() > 0) {
        report(sExclusiveIdStack.back(), reporter);
    } else {
        for (const auto& [id, _] : sPanopticons) {
            report(id, reporter);
        }
    }
}

} // namespace

Panopticon::Panopticon(Source source, const char* suffix, int64_t vsyncId)
      : mSource(source), mVsyncId(vsyncId) {
    static std::atomic_int64_t sSeed = 0;
    mBaseCookie = static_cast<int16_t>(sSeed++) << 16;
    mTrack = std::string(
            ftl::Concat(ftl::truncated<32>(getSourceName()), " ", ftl::truncated<32>(suffix))
                    .c_str());
    mKey = std::string(suffix);
    sliceForType(topSlice()).init(getTrackName(), topSlice(), mBaseCookie + 1);
    mTracingCookie = mBaseCookie + 1;
}

Panopticon::~Panopticon() {
    SFTRACE_CALL();

    // TODO(alecmouri): Also package this up into a bow and upload to statsd
    for (auto& slice : mSlices) {
        slice.terminate(getTrackName());
    }

    PERFETTO_TE(
            tracing_perfetto::track_event_categories::rendering,
            PERFETTO_TE_INSTANT("WorkloadSummary"),
            PERFETTO_TE_PROTO_FIELDS(PERFETTO_TE_PROTO_FIELD_NESTED(
                    /* perfetto_protos_TrackEvent_surfaceflinger_workload_field_number */ 2007,
                    PERFETTO_TE_PROTO_FIELD_VARINT(1, static_cast<int32_t>(mSource)),
                    PERFETTO_TE_PROTO_FIELD_BYTES(2, mKey.data(), mKey.size()),
                    PERFETTO_TE_PROTO_FIELD_VARINT(3, mVsyncId),

                    PERFETTO_TE_PROTO_FIELD_NESTED(
                            /* perfetto_protos_AndroidSurfaceFlingerWorkload_summary_field_number
                             */
                            4,
                            PERFETTO_TE_PROTO_FIELD_NESTED(
                                    /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_timings_field_number
                                     */
                                    1,
                                    PERFETTO_TE_PROTO_FIELD_NESTED(
                                            /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_Timings_sf_cpu_field_number
                                             */
                                            1,
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    1,
                                                    sliceForType(SliceType::CG_Sf_FrameSignal)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    2,
                                                    sliceForType(SliceType::CG_Sf_Commit)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    3,
                                                    sliceForType(SliceType::CG_Sf_Composite)
                                                            .duration())),
                                    PERFETTO_TE_PROTO_FIELD_NESTED(
                                            /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_Timings_hwc_field_number
                                             */
                                            2,
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    1,
                                                    sliceForType(SliceType::CG_Hwc_Present)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    2,
                                                    sliceForType(SliceType::CG_Hwc_Validate)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    3,
                                                    sliceForType(
                                                            SliceType::CG_Hwc_PresentOrValidate)
                                                            .duration())),
                                    PERFETTO_TE_PROTO_FIELD_NESTED(
                                            /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_Timings_re_field_number
                                             */
                                            3,
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    1,
                                                    sliceForType(SliceType::CG_Re_drawLayers)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    2,
                                                    sliceForType(SliceType::CG_Re_gpu).duration())),
                                    PERFETTO_TE_PROTO_FIELD_NESTED(
                                            /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_Timings_skia_field_number
                                             */
                                            4,
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    1,
                                                    sliceForType(SliceType::CG_Skia_flush)
                                                            .duration()),
                                            PERFETTO_TE_PROTO_FIELD_VARINT(
                                                    2,
                                                    sliceForType(SliceType::CG_Skia_submit)
                                                            .duration()))),
                            PERFETTO_TE_PROTO_FIELD_NESTED(
                                    /* perfetto_protos_AndroidSurfaceFlingerWorkload_Summary_stats_field_number
                                     */
                                    2,
                                    PERFETTO_TE_PROTO_FIELD_VARINT(
                                            1,
                                            mSummary.gpuRenderedLayers.load(
                                                    std::memory_order_acquire)),
                                    PERFETTO_TE_PROTO_FIELD_VARINT(
                                            2,
                                            mSummary.dpuRenderedLayers.load(
                                                    std::memory_order_acquire)))))),
            PERFETTO_TE_PROTO_TRACK(
                    PerfettoTeNamedTrackUuid(getTrackName().data(), 1,
                                             PerfettoTeProcessTrackUuid()),
                    PERFETTO_TE_PROTO_FIELD_CSTR(
                            perfetto_protos_TrackDescriptor_atrace_name_field_number,
                            getTrackName().data()),
                    PERFETTO_TE_PROTO_FIELD_VARINT(
                            perfetto_protos_TrackDescriptor_parent_uuid_field_number,
                            PerfettoTeProcessTrackUuid())));
}

void Slice::init(std::string_view track, SliceType slice, int32_t tracingCookie) {
    startTime.store(systemTime(), std::memory_order_release);
    isTracing.store(true, std::memory_order_release);
    cookie = tracingCookie;
    SFTRACE_ASYNC_FOR_TRACK_BEGIN(track.data(), validateAndStringify(slice).data(), tracingCookie);
}

void Slice::terminate(std::string_view track) {
    if (isTracing.exchange(false, std::memory_order_acq_rel)) {
        endTime.store(systemTime(), std::memory_order_release);
        SFTRACE_ASYNC_FOR_TRACK_END(track.data(), cookie);
    }
}

SliceToken::SliceToken(SliceType type, const std::shared_ptr<Panopticon>& parent)
      : mType(type), mKey(parent->getKey()), mSliceCookie(++parent->mTracingCookie) {
    parent->sliceForType(mType).init(parent->getTrackName(), mType, mSliceCookie);
}

SliceToken::~SliceToken() {
    if (mKey != "") {
        if (auto panopticon = sPanopticons.get(mKey.c_str()); panopticon) {
            panopticon->get()->sliceForType(mType).terminate(panopticon->get()->getTrackName());
        }
    }
}

void PanopticonRegistration::start() {
    if (mPanopticon) {
        sPanopticons.try_emplace(mId, std::move(mPanopticon));
    }
}

PanopticonRegistration::~PanopticonRegistration() {
    sPanopticons.erase(mId);
}

ExclusiveToken::ExclusiveToken(std::string id) {
    sExclusiveIdStack.emplace_back(std::move(id));
}

void ExclusiveToken::cleanup() {
    if (!mMovedFrom) {
        sExclusiveIdStack.pop_back();
    }
}

void make(std::string id, Source source, int64_t vsyncId) {
    if (auto panopticon = sPanopticons.get(id.c_str()); panopticon) {
        ALOGV("Clobbering metrics for: %s, %s", id.c_str(),
              panopticon->get()->getSourceName().data());
    }

    auto panopticon = Panopticon::make(source, id.c_str(), vsyncId);
    sPanopticons.try_emplace(std::move(id), std::move(panopticon));
}

std::shared_ptr<PanopticonRegistration> share() {
    std::string view = "";

    if (sExclusiveIdStack.size() > 0) {
        view = sExclusiveIdStack.back();
    } else if (sPanopticons.size() == 1) {
        view = sPanopticons.begin()->first;
    }

    if (view != "") {
        if (auto panopticon = sPanopticons.get(view); panopticon) {
            return std::make_shared<PanopticonRegistration>(std::move(view), panopticon->get());
        }
    }
    return std::make_shared<PanopticonRegistration>("", nullptr);
}

PanopticonRegistrations share(Ids ids) {
    PanopticonRegistrations registrations;
    for (const auto& id : ids) {
        if (auto panopticon = sPanopticons.get(id); panopticon) {
            registrations.emplace_back(
                    std::make_shared<PanopticonRegistration>(id, panopticon->get()));
        }
    }
    return registrations;
}

void terminate(std::string id) {
    sPanopticons.erase(std::move(id));
}

void terminate() {
    sPanopticons.clear();
}

ExclusiveToken exclusive(std::string id) {
    return ExclusiveToken(std::move(id));
}

SliceTokens slice(SliceType sliceType) {
    SliceTokens tokens;
    if (sExclusiveIdStack.size() > 0) {
        auto token = slice(sExclusiveIdStack.back(), sliceType);
        if (token) {
            tokens.push_back(std::move(*token));
        }
    } else {
        for (const auto& [id, _] : sPanopticons) {
            auto token = slice(id, sliceType);
            if (token) {
                tokens.push_back(std::move(*token));
            }
        }
    }
    return tokens;
}

void reportGpuRenderedLayers(int32_t value) {
    report([&](Summary& summary) {
        summary.gpuRenderedLayers.store(value, std::memory_order_release);
    });
}

void reportDpuRenderedLayers(int32_t value) {
    report([&](Summary& summary) {
        summary.dpuRenderedLayers.store(value, std::memory_order_release);
    });
}

void make(Ids ids, Source source, int64_t vsyncId) {
    for (const auto& id : ids) {
        make(id, source, vsyncId);
    }
}
} // namespace android::panopticon
