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

#include <binder/Parcel.h>
#include <gui/GraphicBuffersRegisterInfo.h>
#include <private/gui/ParcelUtils.h>

namespace android::gui {

status_t GraphicBuffersRegisterInfo::writeToParcel(Parcel* output) const {
    SAFE_PARCEL(output->writeStrongBinder, renderResourceToken);
    SAFE_PARCEL(output->writeInt32, buffers.size());
    for (const auto& buffer : buffers) {
        SAFE_PARCEL(output->write, *buffer);
    }
    return NO_ERROR;
}

status_t GraphicBuffersRegisterInfo::readFromParcel(const Parcel* output) {
    SAFE_PARCEL(output->readStrongBinder, &renderResourceToken);
    int32_t size;
    SAFE_PARCEL(output->readInt32, &size);
    for (int i = 0; i < size; i++) {
        auto buffer = sp<GraphicBuffer>::make();
        SAFE_PARCEL(output->read, *buffer);
        buffers.push_back(buffer);
    }
    return NO_ERROR;
}

} // namespace android::gui