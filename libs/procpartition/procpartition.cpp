/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <procpartition/procpartition.h>
#include <procpartition/apexcache.h>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/logging.h>
#include <android/content/pm/IPackageManagerNative.h>
#include <android/content/pm/PackageInfoNative.h>
#include <binder/IServiceManager.h>
#include <utils/String8.h>
#include <filesystem>
#include <sstream>

namespace android {
namespace procpartition {

using apexcache::ApexCache;

std::ostream& operator<<(std::ostream& os, Partition p) {
    switch (p) {
        case Partition::SYSTEM: return os << "system";
        case Partition::SYSTEM_EXT: return os << "system_ext";
        case Partition::PRODUCT: return os << "product";
        case Partition::VENDOR: return os << "vendor";
        case Partition::ODM: return os << "odm";
        case Partition::UNKNOWN: // fallthrough
        default:
            return os << "(unknown)";
    }
}

std::string getExe(pid_t pid) {
    std::string exe;
    std::string real;
    if (!android::base::Readlink("/proc/" + std::to_string(pid) + "/exe", &exe)) {
        return "";
    }
    if (!android::base::Realpath(exe, &real)) {
        return "";
    }
    return real;
}

Partition parseApexPartition(apex::ApexInfo::Partition partition) {
    switch (partition) {
      case apex::ApexInfo::Partition::SYSTEM: return Partition::SYSTEM;
      case apex::ApexInfo::Partition::SYSTEM_EXT: return Partition::SYSTEM_EXT;
      case apex::ApexInfo::Partition::PRODUCT: return Partition::PRODUCT;
      case apex::ApexInfo::Partition::VENDOR: return Partition::VENDOR;
      case apex::ApexInfo::Partition::ODM: return Partition::ODM;
      default: return Partition::UNKNOWN;
    }
}

Partition parseApex(const std::string& s) {
    std::filesystem::path p(s);
    auto it = p.begin();
    // Expecting components: /, apex, <apexName>, ...
    if (it == p.end() || *it != "/") return Partition::UNKNOWN;
    ++it; // -> apex
    if (it == p.end() || *it != "apex") return Partition::UNKNOWN;
    ++it; // -> <apexName>
    if (it == p.end()) return Partition::UNKNOWN;

    std::string apexName = it->string();

    size_t at = apexName.find('@');
    if (at != std::string::npos) {
        apexName = apexName.substr(0, at);
    }

    apexcache::ApexCache *instance = ApexCache::getInstance();
    for (auto &info: instance->getCache(false /*invalidate*/)) {
        if (info.moduleName == apexName) {
            return parseApexPartition(info.partition);
        }
    }
    return Partition::UNKNOWN;
}

std::string getCmdline(pid_t pid) {
    std::string content;
    if (!android::base::ReadFileToString("/proc/" + std::to_string(pid) + "/cmdline", &content,
                                         false /* follow symlinks */)) {
        return "";
    }
    return std::string{content.c_str()};
}

Partition parsePartition(const std::string& s) {
    if (s == "system") {
        return Partition::SYSTEM;
    }
    if (s == "system_ext") {
        return Partition::SYSTEM_EXT;
    }
    if (s == "product") {
        return Partition::PRODUCT;
    }
    if (s == "vendor") {
        return Partition::VENDOR;
    }
    if (s == "odm") {
        return Partition::ODM;
    }
    return Partition::UNKNOWN;
}

std::optional<uid_t> getUid(pid_t pid) {
    std::string content;
    if (!android::base::ReadFileToString("/proc/" + std::to_string(pid) + "/status", &content)) {
        return std::nullopt;
    }
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (android::base::StartsWith(line, "Uid:")) {
            std::vector<std::string> parts = android::base::Split(line, "\t ");
            for (const auto& part : parts) {
                if (part == "Uid:") continue;
                if (part.empty()) continue;
                uid_t ret;
                return android::base::ParseUint(part, &ret) ?
                    std::make_optional(ret) : std::nullopt;
            }
        }
    }
    return std::nullopt;
}

Partition getPartitionFromRealpath(const std::string& path) {
    if (path == "/system/bin/app_process64" ||
        path == "/system/bin/app_process32") {
        return Partition::UNKNOWN;
    }
    size_t backslash = path.find_first_of('/', 1);
    std::string partition = (backslash != std::string::npos) ? path.substr(1, backslash - 1) : path;
    if (partition == "apex") {
        return parseApex(path);
    }

    return parsePartition(partition);
}

Partition getPartitionFromPackageManager(pid_t pid) {
    std::optional<uid_t> uid = getUid(pid);
    if (!uid.has_value()) {
         LOG(ERROR) << "Process with PID: " << pid << " has no UID";
         return Partition::UNKNOWN;
    }

    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->checkService(String16("package_native"));
    if (binder == nullptr) {
        LOG(ERROR) << "Package_native service is not running, no binder returned";
        return Partition::UNKNOWN;
    }

    sp<content::pm::IPackageManagerNative> packageMgr =
            interface_cast<content::pm::IPackageManagerNative>(binder);

    std::optional<std::vector<std::optional<content::pm::PackageInfoNative>>> packageInfos;

    if (binder::Status status =
        packageMgr->getPackageInfoWithSigningInfoForUid(uid.value(), &packageInfos);
        status.isOk() && packageInfos.has_value()) {
        for (const auto& infoOpt : packageInfos.value()) {
            if (infoOpt.has_value() && infoOpt.value().sourceDir.has_value()) {
                android::String8 sourceDir8(infoOpt.value().sourceDir.value());
                Partition p = getPartitionFromRealpath(sourceDir8.c_str());
                if (p != Partition::UNKNOWN) {
                    return p;
                }
            }
        }
    }

    return Partition::UNKNOWN;
}

Partition getPartitionFromCmdline(pid_t pid) {
    const auto& cmdline = getCmdline(pid);
    if (cmdline == "system_server") {
        return Partition::SYSTEM;
    }
    if (cmdline.empty() || cmdline.front() != '/') {
        LOG(INFO) << "getPartitionFromCmdline empty or front not '/': " << cmdline;
        return Partition::UNKNOWN;
    }
    return getPartitionFromRealpath(cmdline);
}

Partition getPartitionFromExe(pid_t pid) {
    const auto& real = getExe(pid);
    if (real.empty() || real.front() != '/') {
        LOG(INFO) << "getPartitionFromExe empty or front not '/': " << real;
        return Partition::UNKNOWN;
    }
    if (real == "/system/bin/app_process64" ||
        real == "/system/bin/app_process32") {
        return getPartitionFromPackageManager(pid);
    }
    return getPartitionFromRealpath(real);
}


Partition getPartition(pid_t pid) {
    Partition partition = getPartitionFromExe(pid);
    if (partition == Partition::UNKNOWN) {
        partition = getPartitionFromCmdline(pid);
    }
    return partition;
}

}  // namespace procpartition
}  // namespace android
