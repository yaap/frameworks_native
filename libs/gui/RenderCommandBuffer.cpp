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

#include <gui/RenderCommandBuffer.h>
#include <fstream>
#include <iostream>

namespace android {

void RenderCommandBuffer::pushOp(IPCRenderBufferOp* op) {
    assert(reinterpret_cast<const uint8_t*>(op) > mBytes &&
           reinterpret_cast<const uint8_t*>(op) < mBytes + sizeof(mBytes));

    if (mTail) {
        mTail->next = op;
    }
    if (!mHead) {
        mHead = op;
    }
    mTail = op;
    op->next = nullptr;
}

void RenderCommandBuffer::reset() {
    mTail = nullptr;
    mHead = nullptr;
    this->resetArena();
}

bool RenderCommandBuffer::dumpToFile(const char* filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        ALOGE("RenderCommandBuffer::dumpToFile: Failed to open file for writing: %s", filename);
        return false;
    }

    file.write(reinterpret_cast<const char*>(&mWidth), sizeof(mWidth));
    file.write(reinterpret_cast<const char*>(&mHeight), sizeof(mHeight));
    file.write(reinterpret_cast<const char*>(&mUsed), sizeof(mUsed));
    file.write(reinterpret_cast<const char*>(mBytes), mUsed);

    if (file.fail()) {
        ALOGE("RenderCommandBuffer::dumpToFile: Error writing to file: %s", filename);
        file.close();
        return false;
    }

    file.close();
    ALOGE("RenderCommandBuffer::dumpToFile: Command buffer dumped to %s", filename);
    return true;
}

RenderCommandBuffer* RenderCommandBuffer::loadFromFile(const char* filename) {
    RenderCommandBuffer* cmdBuffer = new RenderCommandBuffer();
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        ALOGE("RenderCommandBuffer::loadFromFile: Failed to open file for reading: %s", filename);
        return nullptr;
    }

    if (!file.read(reinterpret_cast<char*>(&cmdBuffer->mWidth), sizeof(cmdBuffer->mWidth))) {
        ALOGE("RenderCommandBuffer::loadFromFile: Error reading width from file: %s", filename);
        file.close();
        delete cmdBuffer;
        return nullptr;
    }
    if (!file.read(reinterpret_cast<char*>(&cmdBuffer->mHeight), sizeof(cmdBuffer->mHeight))) {
        ALOGE("RenderCommandBuffer::loadFromFile: Error reading height from file: %s", filename);
        file.close();
        delete cmdBuffer;
        return nullptr;
    }

    size_t usedSize;
    if (!file.read(reinterpret_cast<char*>(&usedSize), sizeof(usedSize))) {
        ALOGE("RenderCommandBuffer::loadFromFile: Error reading used size from file: %s", filename);
        file.close();
        delete cmdBuffer;
        return nullptr;
    }
    if (usedSize > RENDER_COMMAND_BUFFER_DEFAULT_SIZE) {
        ALOGE("RenderCommandBuffer::loadFromFile: Used size in file is larger than buffer size: "
              "%zu > %d",
              usedSize, RENDER_COMMAND_BUFFER_DEFAULT_SIZE);
        file.close();
        delete cmdBuffer;
        return nullptr;
    }

    if (!file.read(reinterpret_cast<char*>(cmdBuffer->mBytes), usedSize)) {
        ALOGE("RenderCommandBuffer::loadFromFile: Error reading command buffer data from file: %s",
              filename);
        file.close();
        delete cmdBuffer;
        return nullptr;
    }
    cmdBuffer->mUsed = usedSize;

    file.close();
    ALOGE("RenderCommandBuffer::loadFromFile: Command buffer loaded from %s, used size: %zu, "
          "width: %d, height: %d",
          filename, usedSize, cmdBuffer->mWidth, cmdBuffer->mHeight);
    return cmdBuffer;
}

} // namespace android
