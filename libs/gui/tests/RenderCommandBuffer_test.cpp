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

#include <gtest/gtest.h>
#include <gui/RenderCommandBuffer.h>
#include <cstring>
#include <memory>
#include <vector>

namespace android {

TEST(RenderCommandBufferTest, RPointerBehavesCorrectlyWhenCopied) {
    struct TestStruct {
        int data;
        RPointer<int> pointer;
    };

    TestStruct structOriginal;
    structOriginal.data = 42;
    structOriginal.pointer = &structOriginal.data;

    // Verify the pointer in the original struct.
    ASSERT_EQ(&structOriginal.data, structOriginal.pointer.get());
    ASSERT_EQ(42, *structOriginal.pointer);

    // Create a copy.
    TestStruct structCopy = structOriginal;

    // Verify that the copy's pointer points to the copy's data, not the original's.
    ASSERT_NE(&structOriginal.data, structCopy.pointer.get());
    ASSERT_EQ(&structCopy.data, structCopy.pointer.get());
    ASSERT_EQ(42, *structCopy.pointer);

    // Modify the copy's data and check if the pointer reflects the change.
    structCopy.data = 99;
    ASSERT_EQ(99, *structCopy.pointer);

    // Ensure the original struct is unchanged.
    ASSERT_EQ(42, structOriginal.data);
    ASSERT_EQ(42, *structOriginal.pointer);
}

TEST(RenderCommandBufferTest, AllocAndPushOp) {
    auto bufferOriginal = std::make_unique<RenderCommandBuffer>();
    ASSERT_NE(nullptr, bufferOriginal.get());

    auto* opOriginal = bufferOriginal->allocAligned<IPCRenderBufferOp>();
    ASSERT_NE(nullptr, opOriginal);

    opOriginal->type = 1;
    bufferOriginal->pushOp(opOriginal);

    auto bufferCopy = std::make_unique<RenderCommandBuffer>();
    memcpy(bufferCopy.get(), bufferOriginal.get(), sizeof(RenderCommandBuffer));

    IPCRenderBufferOp* opCopy = bufferCopy->getOps();
    ASSERT_NE(nullptr, opCopy);
    // the copy should have different address but same value
    ASSERT_NE(opOriginal, opCopy);
    ASSERT_EQ(opOriginal->type, opCopy->type);
}

TEST(RenderCommandBufferTest, PushMultipleOps) {
    auto bufferOriginal = std::make_unique<RenderCommandBuffer>();
    ASSERT_NE(nullptr, bufferOriginal.get());

    constexpr unsigned int opCount = 3;
    IPCRenderBufferOp* opsOriginal[opCount];

    for (unsigned int i = 0; i < opCount; i++) {
        opsOriginal[i] = bufferOriginal->allocAligned<IPCRenderBufferOp>();
        ASSERT_NE(nullptr, opsOriginal[i]);
        opsOriginal[i]->type = i;
        bufferOriginal->pushOp(opsOriginal[i]);
    }

    auto bufferCopy = std::make_unique<RenderCommandBuffer>();
    memcpy(bufferCopy.get(), bufferOriginal.get(), sizeof(RenderCommandBuffer));

    IPCRenderBufferOp* opCopy = bufferCopy->getOps();
    for (unsigned int i = 0; i < opCount; i++) {
        const auto* opOriginal = opsOriginal[i];
        ASSERT_NE(nullptr, opCopy);
        // the copy should have different address but same value
        ASSERT_NE(opOriginal, opCopy);
        ASSERT_EQ(opOriginal->type, opCopy->type);
        opCopy = opCopy->next;
    }
    ASSERT_EQ(nullptr, opCopy);
}

TEST(RenderCommandBufferTest, PushMultipleOpsWithData) {
    auto bufferOriginal = std::make_unique<RenderCommandBuffer>();
    ASSERT_NE(nullptr, bufferOriginal.get());

    struct TestOp : public IPCRenderBufferOp {
        RSpan<int> data;
    };

    constexpr unsigned int opCount = 3;
    TestOp* opsOriginal[opCount];
    std::vector<std::vector<int>> testData(opCount);

    for (unsigned int i = 0; i < opCount; i++) {
        TestOp* op = bufferOriginal->allocAligned<TestOp>();
        ASSERT_NE(nullptr, op);
        opsOriginal[i] = op;

        op->type = i;

        std::vector<int>& td = testData[i];
        td.resize(5 + i * 5);
        for (size_t j = 0; j < td.size(); j++) {
            td[j] = i * 10 + j;
        }

        ASSERT_TRUE(SetRSpan(op->data, bufferOriginal.get(), td.data(), td.size()));
        bufferOriginal->pushOp(op);
    }

    auto bufferCopy = std::make_unique<RenderCommandBuffer>();
    memcpy(bufferCopy.get(), bufferOriginal.get(), sizeof(RenderCommandBuffer));

    IPCRenderBufferOp* opCopy = bufferCopy->getOps();
    for (unsigned int i = 0; i < opCount; i++) {
        const auto* opOriginal = opsOriginal[i];
        ASSERT_NE(nullptr, opCopy);
        ASSERT_NE(opOriginal, opCopy);
        ASSERT_EQ(opOriginal->type, opCopy->type);

        TestOp* testOpCopy = static_cast<TestOp*>(opCopy);
        std::vector<int>& td = testData[i];
        ASSERT_EQ(td.size(), testOpCopy->data.size);
        ASSERT_EQ(0, memcmp(td.data(), testOpCopy->data.data.get(), td.size() * sizeof(int)));
        opCopy = opCopy->next;
    }
    ASSERT_EQ(nullptr, opCopy);
}

} // namespace android