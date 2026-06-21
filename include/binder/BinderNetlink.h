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

#pragma once

#include <memory>
#include <mutex>
#include <netlink/netlink.h>
#include <string>

namespace android {

namespace bindernetlink {

/**
 * Information extracted from a BINDER_CMD_REPORT.
 */
struct Report {
    // The context length is meant to hold the largest binderfs name plus a null terminator.
    static constexpr size_t CONTEXT_LENGTH = 256;

    // The fields supplied by the netlink message.
    char context[CONTEXT_LENGTH];

    uint32_t error;
    uint32_t fromPid;
    uint32_t fromTid;
    uint32_t toPid;
    uint32_t toTid;
    uint32_t flags;
    uint32_t code;
    uint32_t dataSize;

    bool isReply;

    // This is a convenience function for clients that might want to report receipt of a
    // message.
    std::string toString() const;
};

/**
 * Error statistics.
 */
struct Statistics {
    // The number of times an unexpected command type was seen.
    int mUnknownCommand;

    // The number of times an unexpected nla type was seen.
    int mUnknownAttribute;
};

/**
 * The netlink primitive functions talking to kernel binder netlink driver.
 */
class BinderNetlink {
  private:
    /** The multicast netlink socket for events. */
    std::unique_ptr<nl_sock, void (*)(nl_sock*)> mMcSock;

    /** The generic netlink family ID. */
    int mId;

    /** The multicast group. */
    int mGroup;

    /** The buffer that hold received messages. */
    Report mReport;

    /** The cumulative statistics. */
    Statistics mStatistics;

    /** A mutex that serializes access to the report. */
    std::mutex mReportMutex;

    /** A mutex that serializes access to the statistics. */
    std::mutex mStatisticsMutex;

  public:
    /**
     * Constructor
     */
    BinderNetlink();

    /**
     * Opens the generic netlink socket and initializes it.
     *
     * @return    0 on success, or <0 on error
     */
    int open();

    /**
     * Cleans up the generic netlink socket.
     */
    void close();

    /**
     * Receives next binder netlink report.  This method blocks until a report is available.  It
     * holds the mReportMutex while waiting.
     *
     * @param report The netlink attributes for BINDER_CMD_REPORT
     * @return 0 on success, or -1 on error
     */
    int getReport(Report* report);

    /**
     * Sets the timeout.  This is meant for tests, which may need to spin on external conditions.
     *
     * @param seconds The timeout in seconds.  A non-positive timeout means no timeout.
     * @return 0 on success, or -1 on error.
     */
    int setTimeout(int seconds);

    /**
     * Retrieves the cumulative statistics.
     */
    Statistics getStatistics();

  private:
    /**
     * This is a netlink callback that forwards to readReport().  The cookie is a BinderNetlink
     * pointer.
     */
    static int callback(struct nl_msg*, void*);

    /**
     * Extracts a report from the message.  This is invoked from the callback() method.
     */
    int readReport(struct nl_msg*);
};

} // namespace bindernetlink

} // namespace android
