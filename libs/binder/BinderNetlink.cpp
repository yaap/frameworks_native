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

#define LOG_TAG "BinderNetlink"

// #define LOG_NDEBUG 0

#include <android-base/stringprintf.h>
#include <binder/BinderNetlink.h>
#include <errno.h>
#include <linux/android/binderfs.h>
#include <log/log.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/mngt.h>
#include <netlink/socket.h>
#include <string.h>
#include <unistd.h>

#if __has_include(<linux/android/binder_netlink.h>)
#include <linux/android/binder_netlink.h>
#else
// Copy of binder netlink header until include/uapi/linux/*.h are up to date
#ifndef _UAPI_LINUX_ANDROID_BINDER_NETLINK_H
#define _UAPI_LINUX_ANDROID_BINDER_NETLINK_H

#define BINDER_FAMILY_NAME "binder"
#define BINDER_FAMILY_VERSION 1

enum {
    BINDER_A_REPORT_ERROR = 1,
    BINDER_A_REPORT_CONTEXT,
    BINDER_A_REPORT_FROM_PID,
    BINDER_A_REPORT_FROM_TID,
    BINDER_A_REPORT_TO_PID,
    BINDER_A_REPORT_TO_TID,
    BINDER_A_REPORT_IS_REPLY,
    BINDER_A_REPORT_FLAGS,
    BINDER_A_REPORT_CODE,
    BINDER_A_REPORT_DATA_SIZE,

    __BINDER_A_REPORT_MAX,
    BINDER_A_REPORT_MAX = (__BINDER_A_REPORT_MAX - 1)
};

enum {
    BINDER_CMD_REPORT = 1,

    __BINDER_CMD_MAX,
    BINDER_CMD_MAX = (__BINDER_CMD_MAX - 1)
};

#define BINDER_MCGRP_REPORT "report"

#endif /* _UAPI_LINUX_ANDROID_BINDER_NETLINK_H */

#endif // __has_include(<linux/android/binder_netlink.h>)

namespace android {

namespace bindernetlink {

// Control verbose logging.
static constexpr bool DEBUG = false;

// The number of context bytes to copy with strncpy().
static constexpr size_t COPY_LEN =
        std::min(sizeof(Report::context) - 1, static_cast<size_t>(BINDERFS_MAX_NAME));

std::string Report::toString() const {
    return android::base::StringPrintf("%s %u 0x%08x %u:%u -> %u:%u %u 0x08%x %u %u", context,
                                       error, error, fromPid, fromTid, toPid, toTid, isReply, flags,
                                       code, dataSize);
}

BinderNetlink::BinderNetlink()
      : mMcSock(nullptr, nl_socket_free), mId(0), mGroup(0), mStatistics({0, 0}) {}

int BinderNetlink::open() {
    int ret;

    std::unique_ptr<nl_sock, decltype(&nl_socket_free)> mcsock(nl_socket_alloc(), nl_socket_free);
    if (!mcsock.get()) {
        ALOGE("Failed to allocate binder netlink event socket");
        return -1;
    }

    ret = genl_connect(mcsock.get());
    if (ret < 0) {
        ALOGE("Failed to open binder netlink event socket: %s", nl_geterror(ret));
        return -1;
    }

    nl_socket_disable_seq_check(mcsock.get());

    ret = genl_ctrl_resolve(mcsock.get(), BINDER_FAMILY_NAME);
    if (ret < 0) {
        ALOGW("Failed to get binder netlink family id: %s", nl_geterror(ret));
        // errno is not set by the above failure, so set it now.
        if (ret == -NLE_OBJ_NOTFOUND) errno = EOPNOTSUPP;
        return -1;
    }
    mId = ret;

    ret = genl_ctrl_resolve_grp(mcsock.get(), BINDER_FAMILY_NAME, BINDER_MCGRP_REPORT);
    if (ret < 0) {
        ALOGW("Failed to get binder netlink multicast group: %s", nl_geterror(ret));
        return -1;
    }
    mGroup = ret;

    ret = nl_socket_add_membership(mcsock.get(), mGroup);
    if (ret < 0) {
        ALOGW("Failed to join multicast group: %s", nl_geterror(ret));
        return -1;
    }

    // Configure the callback.  Messages will appear in mReport.
    ret = nl_socket_modify_cb(mcsock.get(), NL_CB_VALID, NL_CB_CUSTOM, callback, this);
    if (ret < 0) {
        ALOGW("Failed to set multicast callback: %s", nl_geterror(ret));
        return -1;
    }

    mMcSock = std::move(mcsock);

    ALOGD("Binder Netlink id=%d group=%d", mId, mGroup);
    return 0;
}

void BinderNetlink::close() {
    mMcSock.reset();
}

int BinderNetlink::getReport(struct Report* report) {
    if (!mMcSock) {
        ALOGE("Called on a closed socket");
        errno = EBADF;
        return -1;
    }

    if (!report) {
        ALOGE("Valid Report required");
        errno = EFAULT;
        return -1;
    }

    // Only one thread is allowed to fetch a report at a time.
    std::lock_guard<std::mutex> _lock(mReportMutex);

    if (int ret = nl_recvmsgs_default(mMcSock.get()); ret < 0) {
        ALOGW("Failed to receive messages: %s", nl_geterror(ret));
        return -1;
    }
    *report = mReport;

    return 0;
}

int BinderNetlink::setTimeout(int seconds) {
    if (seconds < 0) seconds = 0;
    struct timeval tv = {.tv_sec = seconds, .tv_usec = 0};
    int fd = nl_socket_get_fd(mMcSock.get());
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ALOGW("Failed so set timeout to %d: %s", seconds, strerror(errno));
        return -1;
    }
    return 0;
}

Statistics BinderNetlink::getStatistics() {
    // Serialize with respect to readReport(), which updates statistics.
    std::lock_guard<std::mutex> _lock(mStatisticsMutex);
    return mStatistics;
}

int BinderNetlink::callback(struct nl_msg* msg, void* arg) {
    if (!arg) {
        ALOGW("Invalid netlink callback arg");
        return NL_SKIP;
    }
    return static_cast<BinderNetlink*>(arg)->readReport(msg);
}

int BinderNetlink::readReport(struct nl_msg* msg) {
    struct nlmsghdr* nlh = nlmsg_hdr(msg);
    struct genlmsghdr* glh = static_cast<struct genlmsghdr*>(nlmsg_data(nlh));
    nlattr* nla = genlmsg_attrdata(glh, 0);
    int rem = genlmsg_attrlen(glh, 0);

    ALOGV_IF(DEBUG, "msg=%p arg=%p", msg, arg);
    ALOGV_IF(DEBUG, "nlh: pid=%d seq=%d, type=%d, len=%d", nlh->nlmsg_pid, nlh->nlmsg_seq,
             nlh->nlmsg_type, nlh->nlmsg_len);
    ALOGV_IF(DEBUG, "glh: cmd=%d, version=%d", glh->cmd, glh->version);
    ALOGV_IF(DEBUG, "nla: type=%d len=%d / %d", nla->nla_type, nla->nla_len, rem);

    // Serialize with respect to getStatistics(), which reads statistics.
    std::lock_guard<std::mutex> _lock(mStatisticsMutex);
    switch (glh->cmd) {
        case BINDER_CMD_REPORT: {
            // Zero the report buffer.
            mReport = {{0, 0}};
            nla_for_each_attr(nla, nla, rem, rem) {
                const int type = nla_type(nla);
                const uint32_t val = nla_get_u32(nla);
                switch (type) {
                    case BINDER_A_REPORT_CONTEXT:
                        // Copy the context from the message.  The destination has been zeroed
                        // and the copy length is at least one byte less than the destination,
                        // so the result is always null terminated.
                        strncpy(mReport.context, nla_get_string(nla), COPY_LEN);
                        break;
                    case BINDER_A_REPORT_IS_REPLY:
                        mReport.isReply = true;
                        break;
                    case BINDER_A_REPORT_ERROR:
                        mReport.error = val;
                        break;
                    case BINDER_A_REPORT_FROM_PID:
                        mReport.fromPid = val;
                        break;
                    case BINDER_A_REPORT_FROM_TID:
                        mReport.fromTid = val;
                        break;
                    case BINDER_A_REPORT_TO_PID:
                        mReport.toPid = val;
                        break;
                    case BINDER_A_REPORT_TO_TID:
                        mReport.toTid = val;
                        break;
                    case BINDER_A_REPORT_FLAGS:
                        mReport.flags = val;
                        break;
                    case BINDER_A_REPORT_CODE:
                        mReport.code = val;
                        break;
                    case BINDER_A_REPORT_DATA_SIZE:
                        mReport.dataSize = val;
                        break;
                    default:
                        ALOGW("Unknown report attribute: %d", type);
                        mStatistics.mUnknownAttribute++;
                        break;
                }
            }
            break;
        }

        default:
            ALOGV("Unknown report cmd: %d", glh->cmd);
            mStatistics.mUnknownCommand++;
            return NL_SKIP;
    }
    return NL_OK;
}

} // namespace bindernetlink

} // namespace android
