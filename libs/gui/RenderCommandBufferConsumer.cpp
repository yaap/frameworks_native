/*
 * Copyright (C) 2025 The Android Open Source Project
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
 */

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <inttypes.h>
#include <sys/mman.h>

#include <private/gui/ParcelUtils.h>
#include "utils/Errors.h"

namespace android {

RenderCommandBufferConsumer::RenderCommandBufferConsumer() {}

RenderCommandBufferConsumer::~RenderCommandBufferConsumer() {
    munmap(mRenderRegion, sizeof(IpcRenderRegion));
    if (mAshmemFdRenderRegion != -1) {
        close(mAshmemFdRenderRegion);
    }
    if (mContext != nullptr) {
        mContextFreeCallback(mContext);
    }
}

void RenderCommandBufferConsumer::adoptFdCommandBuffer(int fd) {
    mAshmemFdRenderRegion = fd;
    mRenderRegion = (IpcRenderRegion*)mmap(nullptr, sizeof(IpcRenderRegion), PROT_READ | PROT_WRITE,
                                           MAP_SHARED, mAshmemFdRenderRegion, 0);

    if (mRenderRegion == MAP_FAILED) {
        LOG_ALWAYS_FATAL("Failed to map shared memory region for RenderCommandBufferConsumer");
    }

    if (!mRenderRegion->mUploadBuf.initMagicMapping(mAshmemFdRenderRegion,
                                                    offsetof(IpcRenderRegion, mUploadBuf))) {
        LOG_ALWAYS_FATAL(
                "Failed to initialize MagicRingBuffer mapping for RenderCommandBufferConsumer");
    }
}

void RenderCommandBufferConsumer::consumerAcquire(uint64_t frameNumber) {
    while (!mRenderRegion->mCommandBuffers.empty()) {
        uint64_t lo = mRenderRegion->mCommandBuffers.getLo();
        uint64_t hi = mRenderRegion->mCommandBuffers.getHi();

        // Skip until we get to the requested frameNumber OR the latest available frame.
        if (lo + 1 >= frameNumber || lo + 1 == hi) {
            return;
        }

        mRenderRegion->mCommandBuffers.popFront();
    }
}

status_t RenderCommandBufferConsumer::readFromParcel(const Parcel& parcel,
                                                     RenderCommandBufferConsumer* consumer) {
    int fd = -1;
    fd = parcel.readFileDescriptor();
    LOG_ALWAYS_FATAL_IF(fd < 0, "Failed to read file descriptor from parcel");
    consumer->adoptFdCommandBuffer(dup(fd));
    return NO_ERROR;
}

void RenderCommandBufferConsumer::setContext(void* context,
                                             std::function<void(void*)> contextFreeCallback) {
    mContextFreeCallback = std::move(contextFreeCallback);
    mContext = context;
}

} // namespace android
