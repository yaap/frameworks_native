#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "FenceUtils.h"

#include <vector>

#include <common/trace.h>

namespace android {

sp<Fence> FenceMerger::waitAndGetFence(const char* name) {
    SFTRACE_FORMAT("FenceMerger::waitAndGetFence for %s", name);
    if (mFenceFutureList.empty()) {
        return mFence;
    }

    for (auto& future : mFenceFutureList) {
        mergeFence(name, future.get().value_or(Fence::NO_FENCE), mFence);
    }
    mFenceFutureList.clear();
    return mFence;
}

void FenceMerger::addFuture(ftl::Future<FenceResult>&& future) {
    mFenceFutureList.emplace_back(std::move(future));
}

void FenceMerger::addFence(const char* name, sp<Fence> fence) {
    mergeFence(name, std::move(fence), mFence);
}

const sp<Fence>& FenceMerger::getFence() const {
    return mFence;
}

} // namespace android
