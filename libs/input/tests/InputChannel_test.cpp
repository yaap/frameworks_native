/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <array>

#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <android-base/logging.h>
#include <android-base/result-gmock.h>
#include <binder/Binder.h>
#include <binder/Parcel.h>
#include <gtest/gtest.h>
#include <input/InputEventBuilders.h>
#include <input/InputTransport.h>
#include <sys/ioctl.h>
#include <utils/StopWatch.h>
#include <utils/StrongPointer.h>
#include <utils/Timers.h>

using android::base::testing::Ok;
using testing::Not;

namespace android {

namespace {
bool operator==(const InputChannel& left, const InputChannel& right) {
    struct stat lhs, rhs;
    if (fstat(left.getFd(), &lhs) != 0) {
        return false;
    }
    if (fstat(right.getFd(), &rhs) != 0) {
        return false;
    }
    // If file descriptors are pointing to same inode they are duplicated fds.
    return left.getName() == right.getName() &&
            left.getConnectionToken() == right.getConnectionToken() && lhs.st_ino == rhs.st_ino;
}

/**
 * Read a message from the provided channel. Read will continue until there's data, so only call
 * this if there's data in the channel, or it's closed. If there's no data, this will loop forever.
 */
android::base::Result<InputMessage> readMessage(InputChannel& channel) {
    while (true) {
        // Keep reading until we get something other than 'WOULD_BLOCK'
        android::base::Result<InputMessage> result = channel.receiveMessage();
        if (!result.ok() && result.error().code() == WOULD_BLOCK) {
            // The data is not available yet.
            continue; // try again
        }
        return result;
    }
}

InputMessage createFinishedMessage(uint32_t seq) {
    InputMessage finish{};
    finish.header.type = InputMessage::Type::FINISHED;
    finish.header.seq = seq;
    finish.body.finished.handled = true;
    return finish;
}

InputMessage createKeyMessage(uint32_t seq) {
    InputMessage key{};
    key.header.type = InputMessage::Type::KEY;
    key.header.seq = seq;
    key.body.key.action = AKEY_EVENT_ACTION_DOWN;
    return key;
}

} // namespace

class InputChannelTest : public testing::Test {
};

TEST_F(InputChannelTest, ClientAndServerTokensMatch) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result =
            InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";
    EXPECT_EQ(serverChannel->getConnectionToken(), clientChannel->getConnectionToken());
}

TEST_F(InputChannelTest, OpenInputChannelPair_ReturnsAPairOfConnectedChannels) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result = InputChannel::openInputChannelPair("channel name",
            serverChannel, clientChannel);

    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    EXPECT_EQ(serverChannel->getName(), clientChannel->getName());

    // Server->Client communication
    InputMessage serverMsg = {};
    serverMsg.header.type = InputMessage::Type::KEY;
    serverMsg.body.key.action = AKEY_EVENT_ACTION_DOWN;
    EXPECT_EQ(OK, serverChannel->sendMessage(&serverMsg))
            << "server channel should be able to send message to client channel";

    android::base::Result<InputMessage> clientMsgResult = clientChannel->receiveMessage();
    ASSERT_THAT(clientMsgResult, Ok())
            << "client channel should be able to receive message from server channel";
    const InputMessage& clientMsg = *clientMsgResult;
    EXPECT_EQ(serverMsg.header.type, clientMsg.header.type)
            << "client channel should receive the correct message from server channel";
    EXPECT_EQ(serverMsg.body.key.action, clientMsg.body.key.action)
            << "client channel should receive the correct message from server channel";

    // Client->Server communication
    InputMessage clientReply = {};
    clientReply.header.type = InputMessage::Type::FINISHED;
    clientReply.header.seq = 0x11223344;
    clientReply.body.finished.handled = true;
    EXPECT_EQ(OK, clientChannel->sendMessage(&clientReply))
            << "client channel should be able to send message to server channel";

    android::base::Result<InputMessage> serverReplyResult = serverChannel->receiveMessage();
    ASSERT_THAT(serverReplyResult, Ok())
            << "server channel should be able to receive message from client channel";
    const InputMessage& serverReply = *serverReplyResult;
    EXPECT_EQ(clientReply.header.type, serverReply.header.type)
            << "server channel should receive the correct message from client channel";
    EXPECT_EQ(clientReply.header.seq, serverReply.header.seq)
            << "server channel should receive the correct message from client channel";
    EXPECT_EQ(clientReply.body.finished.handled, serverReply.body.finished.handled)
            << "server channel should receive the correct message from client channel";
}

TEST_F(InputChannelTest, ProbablyHasInput) {
    std::unique_ptr<InputChannel> senderChannel, receiverChannel;

    // Open a pair of channels.
    status_t result =
            InputChannel::openInputChannelPair("channel name", senderChannel, receiverChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    ASSERT_FALSE(receiverChannel->probablyHasInput());

    // Send one message.
    InputMessage serverMsg = {};
    serverMsg.header.type = InputMessage::Type::KEY;
    serverMsg.body.key.action = AKEY_EVENT_ACTION_DOWN;
    EXPECT_EQ(OK, senderChannel->sendMessage(&serverMsg))
            << "server channel should be able to send message to client channel";

    // Verify input is available.
    bool hasInput = false;
    do {
        // The probablyHasInput() can return false positive under rare circumstances uncontrollable
        // by the tests. Re-request the availability in this case. Returning |false| for a long
        // time is not intended, and would cause a test timeout.
        hasInput = receiverChannel->probablyHasInput();
    } while (!hasInput);
    EXPECT_TRUE(hasInput)
            << "client channel should observe that message is available before receiving it";

    // Receive (consume) the message.
    android::base::Result<InputMessage> clientMsgResult = receiverChannel->receiveMessage();
    ASSERT_THAT(clientMsgResult, Ok())
            << "client channel should be able to receive message from server channel";
    const InputMessage& clientMsg = *clientMsgResult;
    EXPECT_EQ(serverMsg.header.type, clientMsg.header.type)
            << "client channel should receive the correct message from server channel";
    EXPECT_EQ(serverMsg.body.key.action, clientMsg.body.key.action)
            << "client channel should receive the correct message from server channel";

    // Verify input is not available.
    EXPECT_FALSE(receiverChannel->probablyHasInput())
            << "client should not observe any more messages after receiving the single one";
}

TEST_F(InputChannelTest, ReceiveSignal_WhenNoSignalPresent_ReturnsAnError) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result = InputChannel::openInputChannelPair("channel name",
            serverChannel, clientChannel);

    ASSERT_EQ(OK, result)
            << "should have successfully opened a channel pair";

    android::base::Result<InputMessage> msgResult = clientChannel->receiveMessage();
    EXPECT_EQ(WOULD_BLOCK, msgResult.error().code())
            << "receiveMessage should have returned WOULD_BLOCK";
}

TEST_F(InputChannelTest, ReceiveSignal_WhenPeerClosed_ReturnsAnError) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result = InputChannel::openInputChannelPair("channel name",
            serverChannel, clientChannel);

    ASSERT_EQ(OK, result)
            << "should have successfully opened a channel pair";

    serverChannel.reset(); // close server channel

    android::base::Result<InputMessage> msgResult = clientChannel->receiveMessage();
    EXPECT_EQ(DEAD_OBJECT, msgResult.error().code())
            << "receiveMessage should have returned DEAD_OBJECT";
}

TEST_F(InputChannelTest, SendSignal_WhenPeerClosed_ReturnsAnError) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result = InputChannel::openInputChannelPair("channel name",
            serverChannel, clientChannel);

    ASSERT_EQ(OK, result)
            << "should have successfully opened a channel pair";

    serverChannel.reset(); // close server channel

    InputMessage msg;
    msg.header.type = InputMessage::Type::KEY;
    EXPECT_EQ(DEAD_OBJECT, clientChannel->sendMessage(&msg))
            << "sendMessage should have returned DEAD_OBJECT";
}

TEST_F(InputChannelTest, SendAndReceive_MotionClassification) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;
    status_t result = InputChannel::openInputChannelPair("channel name",
            serverChannel, clientChannel);
    ASSERT_EQ(OK, result)
            << "should have successfully opened a channel pair";

    std::array<MotionClassification, 3> classifications = {
        MotionClassification::NONE,
        MotionClassification::AMBIGUOUS_GESTURE,
        MotionClassification::DEEP_PRESS,
    };

    InputMessage serverMsg = {};
    serverMsg.header.type = InputMessage::Type::MOTION;
    serverMsg.header.seq = 1;
    serverMsg.body.motion.pointerCount = 1;

    for (MotionClassification classification : classifications) {
        // Send and receive a message with classification
        serverMsg.body.motion.classification = classification;
        EXPECT_EQ(OK, serverChannel->sendMessage(&serverMsg))
                << "server channel should be able to send message to client channel";

        android::base::Result<InputMessage> clientMsgResult = clientChannel->receiveMessage();
        ASSERT_THAT(clientMsgResult, Ok())
                << "client channel should be able to receive message from server channel";
        const InputMessage& clientMsg = *clientMsgResult;
        EXPECT_EQ(serverMsg.header.type, clientMsg.header.type);
        EXPECT_EQ(classification, clientMsg.body.motion.classification)
                << "Expected to receive " << motionClassificationToString(classification);
    }
}

/**
 * In this test, server writes 3 key events to the client. The client, upon receiving the first key,
 * sends a "finished" signal back to server, and then closes the fd.
 *
 * Next, we check what the server receives.
 *
 * In most cases, the server will receive the finish event, and then an 'fd closed' event.
 *
 * However, sometimes, the 'finish' event will not be delivered to the server. This is communicated
 * to the server via 'ECONNRESET', which the InputChannel converts into DEAD_OBJECT.
 *
 * The server needs to be aware of this behaviour and correctly clean up any state associated with
 * the  client, even if the client did not end up finishing some of the messages.
 *
 * This test is written to expose a behaviour on the linux side - occasionally, the
 * last events written to the fd by the consumer are not delivered to the server.
 *
 * When tested on 2025 hardware, ECONNRESET was received  approximately 1 out of 40 tries.
 * In vast majority (~ 29999 / 30000) of cases, after receiving ECONNRESET, the server could still
 * read the client data after receiving ECONNRESET.
 */
TEST_F(InputChannelTest, ReceiveAfterCloseMultiThreaded) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;
    status_t result =
            InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    // Sender / publisher: publish 3 keys
    InputMessage key1 = createKeyMessage(/*seq=*/1);
    serverChannel->sendMessage(&key1);
    // The client should close the fd after it reads this one, but we will send 2 more here.
    InputMessage key2 = createKeyMessage(/*seq=*/2);
    serverChannel->sendMessage(&key2);
    InputMessage key3 = createKeyMessage(/*seq=*/3);
    serverChannel->sendMessage(&key3);

    std::thread consumer = std::thread([clientChannel = std::move(clientChannel)]() mutable {
        // Read the first key
        android::base::Result<InputMessage> firstKey = readMessage(*clientChannel);
        if (!firstKey.ok()) {
            FAIL() << "Did not receive the first key";
        }

        // Send finish
        const InputMessage finish = createFinishedMessage(firstKey->header.seq);
        clientChannel->sendMessage(&finish);
        // Now close the fd
        clientChannel.reset();
    });

    // Now try to read the finish message, even though client closed the fd
    android::base::Result<InputMessage> response = readMessage(*serverChannel);
    consumer.join();
    if (response.ok()) {
        ASSERT_EQ(response->header.type, InputMessage::Type::FINISHED);
    } else {
        // It's possible that after the client closes the fd, server will receive ECONNRESET.
        // In those situations, this error code will be translated into DEAD_OBJECT by the
        // InputChannel.
        ASSERT_EQ(response.error().code(), DEAD_OBJECT);
        // In most cases, subsequent attempts to read the client channel at this
        // point would succeed. However, for simplicity, we exit here (since
        // it's not guaranteed).
        return;
    }

    // There should not be any more events from the client, since the client closed fd after the
    // first key.
    android::base::Result<InputMessage> noEvent = serverChannel->receiveMessage();
    ASSERT_THAT(noEvent, Not(Ok())) << "Got event " << *noEvent;
}

/**
 * Similar test as above, but single-threaded.
 */
TEST_F(InputChannelTest, ReceiveAfterCloseSingleThreaded) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;
    status_t result =
            InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    // Sender / publisher: publish 3 keys
    InputMessage key1 = createKeyMessage(/*seq=*/1);
    serverChannel->sendMessage(&key1);
    // The client should close the fd after it reads this one, but we will send 2 more here.
    InputMessage key2 = createKeyMessage(/*seq=*/2);
    serverChannel->sendMessage(&key2);
    InputMessage key3 = createKeyMessage(/*seq=*/3);
    serverChannel->sendMessage(&key3);

    // Read the first key
    android::base::Result<InputMessage> firstKey = readMessage(*clientChannel);
    if (!firstKey.ok()) {
        FAIL() << "Did not receive the first key";
    }

    // Send finish
    const InputMessage finish = createFinishedMessage(firstKey->header.seq);
    clientChannel->sendMessage(&finish);
    // Now close the fd
    clientChannel.reset();

    // Now try to read the finish message, even though client closed the fd
    android::base::Result<InputMessage> response = readMessage(*serverChannel);
    ASSERT_THAT(response, Not(Ok()));
    ASSERT_EQ(response.error().code(), DEAD_OBJECT);

    // We can still read the finish event (but in practice, the expectation is that the server will
    // not be doing this after getting DEAD_OBJECT).
    android::base::Result<InputMessage> finishEvent = serverChannel->receiveMessage();
    ASSERT_THAT(finishEvent, Ok());
    ASSERT_EQ(finishEvent->header.type, InputMessage::Type::FINISHED);
}

TEST_F(InputChannelTest, DuplicateChannelAndAssertEqual) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;

    status_t result =
            InputChannel::openInputChannelPair("channel dup", serverChannel, clientChannel);

    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    std::unique_ptr<InputChannel> dupChan = serverChannel->dup();

    EXPECT_EQ(*serverChannel == *dupChan, true) << "inputchannel should be equal after duplication";
}

/**
 * In situations of high load, the channel's socket can be filled. This leads to sendMessage
 * returning status WOULD_BLOCK. This can result in ANRs when not properly handled. Therefore it is
 * useful to understand the capacity of these sockets for various important messages, including
 * finished messages. The specific number of messages is determined by the socket buffer size and
 * effective size of InputMessages on the socket, which this test determines by sending
 * messages until sendMessage return a non-OK status such as WOULD_BLOCK.
 *
 * Depending on the hardware architecture, kernel, and the type of message, the effective message
 * size could differ, requiring fewer or more messages to fill the socket's effective capacity.
 */
TEST_F(InputChannelTest, FinishedMessageCapacityInSocket) {
    std::unique_ptr<InputChannel> serverChannel, clientChannel;
    status_t result =
            InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    uint32_t seq = 1;
    for (;;) {
        InputMessage finishedMessage = createFinishedMessage(seq);
        status_t status = serverChannel->sendMessage(&finishedMessage);
        if (status != OK) {
            break;
        }
        seq++;
    }
    const uint32_t expected_capacity = 87;
    ASSERT_EQ(seq, expected_capacity) << "server should have sent " << expected_capacity
                                      << " finished messages, instead sent " << seq;
}

/**
 * Similar to above, but for motion messages.
 *
 * Effective capacity depends on `sk_wmem_alloc`. The kernel allocates memory using
 * power-of-two slab buckets, padding structs to hardware cache lines (SMP_CACHE_BYTES).
 */
TEST_F(InputChannelTest, MotionMessageCapacityInSocket) {
    // Total Footprint = slab_bucket(sk_buff) + slab_bucket(aligned_payload + tail).
    //
    // 1. sk_buff bucket: ~232-248 bytes. Allocated from the 256-byte slab bucket.
    // 2. Data bucket: 168-byte MotionMessage payload + `skb_shared_info` tail.
    //
    // Note: In most kernels, the payload is aligned to the L1 cache line,
    // but the shinfo tail is added as a raw size. However, because the payload is
    // aligned, the tail still begins on a fresh cache line boundary.
    //
    // Memory footprint branches based on alignment and kernel KABI padding:
    //
    // - 512-byte Data Bucket (Total Footprint: 768 bytes)
    //   Condition: 64-byte alignment, standard 320-byte tail.
    //   Calculation: ALIGN(168, 64) + 320 = 192 + 320 = 512 bytes.
    //   Allocation: 256 (sk_buff) + 512 (data) = 768 bytes.
    constexpr int kFootprint768Byte = 768;
    constexpr uint32_t kCapacity768Byte = 87;

    // - 1024-byte Data Bucket (Total Footprint: 1280 bytes)
    //   Condition: Padded >320-byte tail (e.g., 344-byte tail due to Android KABI).
    //   Calculation: ALIGN(168, 64) + 344 = 192 + 344 = 536 bytes.
    //   Overflow: Since 536 > 512, it overflows into the 1024-byte slab bucket
    //   Allocation: 256 (sk_buff) + 1024 (data) = 1280 bytes.
    constexpr int kFootprint1280Byte = 1280;
    constexpr uint32_t kCapacity1280Byte = 53;

    // Note: 24-byte FinishedMessages maintain a 768-byte footprint globally.
    // ALIGN(24, 64) + 344 = 64 + 344 = 408 bytes (fits the 512-byte bucket).

    std::unique_ptr<InputChannel> serverChannel, clientChannel;
    status_t result =
            InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
    ASSERT_EQ(OK, result) << "should have successfully opened a channel pair";

    int footprint = 0;
    uint32_t seq = 1;
    status_t status = OK;

    for (;;) {
        InputMessage msg = InputMessageBuilder{InputMessage::Type::MOTION, seq}
                                   .deviceId(0)
                                   .action(AMOTION_EVENT_ACTION_MOVE)
                                   .build();

        status = serverChannel->sendMessage(&msg);
        if (status != OK) {
            break;
        }

        // TIOCOUTQ returns the total kernel memory allocated for the socket's write
        // queue (sk_wmem_alloc). Because this footprint fluctuates based on the
        // architecture's cache line size and slab allocator rules, we dynamically
        // measure the first message's footprint to correctly assert the expected
        // socket capacity and prevent test flakiness across different devices.
        if (seq == 1) {
            ASSERT_EQ(0, ioctl(serverChannel->getFd(), TIOCOUTQ, &footprint))
                    << "Failed to read socket memory footprint";
        }
        seq++;
    }

    ASSERT_EQ(WOULD_BLOCK, status)
            << "Expected sendMessage to return WOULD_BLOCK when the socket is full";

    if (footprint == kFootprint768Byte) {
        ASSERT_EQ(seq, kCapacity768Byte);
    } else if (footprint == kFootprint1280Byte) {
        ASSERT_EQ(seq, kCapacity1280Byte);
    } else {
        // If a future kernel changes the sk_buff layout or slab allocator rules,
        // we don't want to break the test. We just need to guarantee the socket
        // can still hold a safe minimum number of messages to prevent dropped inputs.
        ASSERT_GE(seq, kCapacity1280Byte)
                << "Unrecognized kernel footprint (" << footprint << " bytes). "
                << "Socket capacity broke at " << seq << ", which is below the safe minimum";
    }
}
} // namespace android
