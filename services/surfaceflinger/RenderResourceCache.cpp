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
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "RenderResourceCache.h"

#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <common/trace.h>
#include <gui/GraphicBuffersRegisterInfo.h>
#include <gui/GraphicBuffersUnregisterInfo.h>

namespace android::surfaceflinger {

RenderResourceCache::RenderResourceCache() = default;

RenderResourceCache::~RenderResourceCache() = default;

void RenderResourceCache::queueRegisterGraphicBuffers(const gui::GraphicBuffersRegisterInfo& info) {
    SFTRACE_CALL();

    if (!info.renderResourceToken) {
        ALOGE("%s: Graphic Buffer registration requested without a valid token", __func__);
        return;
    }

    sp<IBinder> token = info.renderResourceToken;
    sp<DeathRecipient> recipient = sp<DeathRecipient>::fromExisting(this);
    status_t status = token->linkToDeath(recipient);
    if (status != NO_ERROR) {
        ALOGE("%s: Remote render client unexpectedly exited, error: %d", __func__, status);
        // Client is likely already dead, do not queue.
        return;
    }

    mPendingOps.push([this, info]() {
        std::shared_ptr<IPCServerResourceCache> cache = getCache(info.renderResourceToken);

        for (const auto& buffer : info.buffers) {
            SFTRACE_FORMAT("Registering buffer %" PRIu64, buffer->getId());
            cache->bitmaps[buffer->getId()] = IPCServerBitmap{.buffer = buffer};
        }
    });
}

void RenderResourceCache::queueUnregisterGraphicBuffers(
        const gui::GraphicBuffersUnregisterInfo& info) {
    SFTRACE_CALL();

    mPendingOps.push([this, info]() {
        auto cache = mCaches.find(info.renderResourceToken);
        if (cache == mCaches.end()) {
            return;
        }
        for (int64_t id : info.bufferIds) {
            cache->second->bitmaps.erase(static_cast<uint64_t>(id));
        }
    });
}

void RenderResourceCache::processPendingOperations() {
    SFTRACE_CALL();

    while (!mPendingOps.isEmpty()) {
        if (auto f = mPendingOps.pop()) {
            (*f)();
        }
    }
}

std::shared_ptr<IPCServerResourceCache> RenderResourceCache::getCache(const sp<IBinder>& token) {
    SFTRACE_CALL();

    auto it = mCaches.find(token);
    if (it != mCaches.end()) {
        return it->second;
    }
    // Create new cache entry if not found, to ensure shared_ptr is stable for LayerSnapshotBuilder.
    // LayerSnapshotBuilder might query getCache() before the registration command arrives.
    // By returning a valid shared_ptr, we allow LayerSnapshot to hold a reference that will
    // be populated later, avoiding the need for LayerSnapshotBuilder to re-query getCache().
    std::shared_ptr<IPCServerResourceCache>& cache = mCaches[token];
    cache = std::make_shared<IPCServerResourceCache>();

    sp<DeathRecipient> recipient = sp<DeathRecipient>::fromExisting(this);
    token->linkToDeath(recipient);

    return cache;
}

void RenderResourceCache::binderDied(const wp<IBinder>& binder) {
    mPendingOps.push([this, binder]() { mCaches.erase(binder); });
}

} // namespace android::surfaceflinger