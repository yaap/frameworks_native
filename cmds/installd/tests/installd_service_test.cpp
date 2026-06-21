/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <sstream>
#include <string>

#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/stringprintf.h>
#include <cutils/properties.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

#include <android/content/pm/IPackageManagerNative.h>
#include <android_installd_flags.h>
#include <binder/IServiceManager.h>
#include "InstalldNativeService.h"
#include "QuotaUtils.h"
#include "binder/Status.h"
#include "binder_test_utils.h"
#include "dexopt.h"
#include "globals.h"
#include "unique_file.h"
#include "utils.h"

using android::base::StringPrintf;
using android::base::unique_fd;
using android::os::ParcelFileDescriptor;
using std::filesystem::is_empty;
namespace flags = android::installd::flags;

namespace android {
std::string get_package_name(uid_t uid) {
    sp<IServiceManager> sm = defaultServiceManager();
    sp<content::pm::IPackageManagerNative> package_mgr;
    if (sm.get() == nullptr) {
        LOG(INFO) << "Cannot find service manager";
    } else {
        sp<IBinder> binder = sm->getService(String16("package_native"));
        if (binder.get() == nullptr) {
            LOG(INFO) << "Cannot find package_native";
        } else {
            package_mgr = interface_cast<content::pm::IPackageManagerNative>(binder);
        }
    }
    // find package name
    std::string pkg;
    if (package_mgr != nullptr) {
        std::vector<std::string> names;
        binder::Status status = package_mgr->getNamesForUids({(int)uid}, &names);
        if (!status.isOk()) {
            LOG(INFO) << "getNamesForUids failed: %s", status.exceptionMessage().c_str();
        } else {
            if (!names[0].empty()) {
                pkg = names[0].c_str();
            }
        }
    }
    return pkg;
}
namespace installd {

static constexpr const char* kTestUuid = "TEST";
static const std::string kTestPath = "/data/local/tmp";
static constexpr const uid_t kNobodyUid = 9999;
static constexpr const uid_t kSystemUid = 1000;
static constexpr const int32_t kTestUserId = 0;
static constexpr const uid_t kTestAppId = 19999;
static constexpr const int FLAG_STORAGE_SDK = InstalldNativeService::FLAG_STORAGE_SDK;
static constexpr const int FLAG_CLEAR_CACHE_ONLY = InstalldNativeService::FLAG_CLEAR_CACHE_ONLY;
static constexpr const int FLAG_CLEAR_CODE_CACHE_ONLY =
        InstalldNativeService::FLAG_CLEAR_CODE_CACHE_ONLY;
static constexpr const uid_t kTestPccAppId = kTestAppId + 20000;
const uid_t kTestPccAppUid = multiuser_get_uid(kTestUserId, kTestPccAppId);
const gid_t kTestPccCacheGid = multiuser_get_cache_gid(kTestUserId, kTestPccAppId);

const gid_t kTestAppUid = multiuser_get_uid(kTestUserId, kTestAppId);
static constexpr const int32_t kSecondaryUserId = 10;
const gid_t kSecondaryAppUid = multiuser_get_uid(kSecondaryUserId, kTestAppId);
const gid_t kTestCacheGid = multiuser_get_cache_gid(kTestUserId, kTestAppId);
const uid_t kTestSdkSandboxUid = multiuser_get_sdk_sandbox_uid(kTestUserId, kTestAppId);

#define FLAG_FORCE InstalldNativeService::FLAG_FORCE

int get_property(const char *key, char *value, const char *default_value) {
    return property_get(key, value, default_value);
}

bool calculate_oat_file_path(char path[PKG_PATH_MAX], const char *oat_dir, const char *apk_path,
        const char *instruction_set) {
    return calculate_oat_file_path_default(path, oat_dir, apk_path, instruction_set);
}

bool calculate_odex_file_path(char path[PKG_PATH_MAX], const char *apk_path,
        const char *instruction_set) {
    return calculate_odex_file_path_default(path, apk_path, instruction_set);
}

bool create_cache_path(char path[PKG_PATH_MAX], const char *src, const char *instruction_set) {
    return create_cache_path_default(path, src, instruction_set);
}

bool force_compile_without_image() {
    return false;
}

static std::string get_full_path(const std::string& path) {
    return StringPrintf("%s/%s", kTestPath.c_str(), path.c_str());
}

static void mkdir(const std::string& path, uid_t owner, gid_t group, mode_t mode) {
    const std::string fullPath = get_full_path(path);
    EXPECT_EQ(::mkdir(fullPath.c_str(), mode), 0);
    EXPECT_EQ(::chown(fullPath.c_str(), owner, group), 0);
    EXPECT_EQ(::chmod(fullPath.c_str(), mode), 0);
}

static int create(const std::string& path, uid_t owner, gid_t group, mode_t mode) {
    int fd = ::open(get_full_path(path).c_str(), O_RDWR | O_CREAT, mode);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(::fchown(fd, owner, group), 0);
    EXPECT_EQ(::fchmod(fd, mode), 0);
    return fd;
}

static void create_with_content(const std::string& path, uid_t owner, gid_t group, mode_t mode,
                                const std::string& content) {
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, mode);
    EXPECT_NE(fd, -1);
    EXPECT_TRUE(android::base::WriteStringToFd(content, fd));
    EXPECT_EQ(::fchown(fd, owner, group), 0);
    EXPECT_EQ(::fchmod(fd, mode), 0);
    close(fd);
}

static void touch(const std::string& path, uid_t owner, gid_t group, mode_t mode) {
    EXPECT_EQ(::close(create(path.c_str(), owner, group, mode)), 0);
}

static int stat_gid(const char* path) {
    struct stat buf;
    EXPECT_EQ(::stat(get_full_path(path).c_str(), &buf), 0);
    return buf.st_gid;
}

static int stat_mode(const char* path) {
    struct stat buf;
    EXPECT_EQ(::stat(get_full_path(path).c_str(), &buf), 0);
    return buf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISGID);
}

static uid_t stat_uid(const char* path) {
    struct stat buf;
    EXPECT_EQ(::stat(get_full_path(path).c_str(), &buf), 0);
    return buf.st_uid;
}

static bool exists(const std::string& path) {
    return ::access(get_full_path(path).c_str(), F_OK) == 0;
}

template <class Pred>
static bool find_file(const char* path, Pred&& pred) {
    bool result = false;
    auto d = opendir(path);
    if (d == nullptr) {
        return result;
    }
    struct dirent* de;
    while ((de = readdir(d))) {
        const char* name = de->d_name;
        if (pred(name, de->d_type == DT_DIR)) {
            result = true;
            break;
        }
    }
    closedir(d);
    return result;
}

static bool exists_renamed_deleted_dir(const std::string& rootDirectory) {
    return find_file((kTestPath + rootDirectory).c_str(), [](const std::string& name, bool is_dir) {
        return is_dir && is_renamed_deleted_dir(name);
    });
}

static void unlink_path(const std::string& path) {
    if (unlink(path.c_str()) < 0) {
        PLOG(DEBUG) << "Failed to unlink " + path;
    }
}

static void verifyPccStatsIncluded(std::vector<int64_t> sizesBeforePccData,
                                   std::vector<int64_t> sizesAfterPccData,
                                   int64_t dataBytesAfterPccData, int64_t cacheBytesAfterPccData) {
    // Verification: Size should increase because PCC data is included
    // Data size >= App data size + PCC data size
    EXPECT_GE(sizesAfterPccData[1], dataBytesAfterPccData);
    // Data size < Data written + buffer (1MB)
    EXPECT_LE(sizesAfterPccData[1], dataBytesAfterPccData + 1 * 1024 * 1024);
    // Data size after PCC data written should definitely be > before
    EXPECT_GT(sizesAfterPccData[1], sizesBeforePccData[1]);

    // Final cache size >= App data cache size + PCC data cache size
    EXPECT_GE(sizesAfterPccData[2], cacheBytesAfterPccData);
    // Data size < Data written + buffer (1MB)
    EXPECT_LE(sizesAfterPccData[2], cacheBytesAfterPccData + 1 * 1024 * 1024);
    // Cache data size after PCC data written should definitely be > before
    EXPECT_GT(sizesAfterPccData[2], sizesBeforePccData[2]);

    // Except for app data and cache, other sizes shouldn't be affected
    for (size_t i = 0; i < sizesBeforePccData.size(); i++) {
        if (i == 1 || i == 2) {
            continue;
        }
        EXPECT_EQ(sizesBeforePccData[i], sizesAfterPccData[i]);
    }
}

class ServiceTest : public testing::Test {
protected:
    InstalldNativeService* service;
    std::optional<std::string> testUuid;

    virtual void SetUp() {
        setenv("ANDROID_LOG_TAGS", "*:v", 1);
        android::base::InitLogging(nullptr);

        service = new InstalldNativeService();
        testUuid = kTestUuid;
        system("rm -rf /data/local/tmp/user");
        system("rm -rf /data/local/tmp/user_de");
        system("rm -rf /data/local/tmp/misc_ce");
        system("rm -rf /data/local/tmp/misc_de");
        system("mkdir -p /data/local/tmp/user/0");
        system("mkdir -p /data/local/tmp/user_de/0");
        system("mkdir -p /data/local/tmp/misc_ce/0/sdksandbox");
        system("mkdir -p /data/local/tmp/misc_de/0/sdksandbox");
        init_globals_from_data_and_root();
    }

    virtual void TearDown() {
        delete service;
        system("rm -rf /data/local/tmp/user");
        system("rm -rf /data/local/tmp/misc_ce");
        system("rm -rf /data/local/tmp/misc_de");
    }
};

TEST_F(ServiceTest, CreateAppData_QuotaEnforcementSet) {
    LOG(INFO) << "CreateAppData_QuotaEnforcementSet";
    if (!flags::enable_set_inode_quotas()) {
        return;
    }
    #if APPLY_HARD_QUOTAS
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.uuid = std::nullopt;
    args.packageName = "com.foo";
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_DE;

    // initialise the mounts
    service->invalidateMounts();
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    EXPECT_LT(0, GetInodesQuotaHardLimitsForUid("", kTestAppId));
    #endif
}

TEST_F(ServiceTest, FixupAppData_Upgrade) {
    LOG(INFO) << "FixupAppData_Upgrade";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/normal", 10000, 10000, 0700);
    mkdir("user/0/com.example/cache", 10000, 10000, 0700);
    touch("user/0/com.example/cache/file", 10000, 10000, 0700);

    service->fixupAppData(testUuid, 0);

    EXPECT_EQ(10000, stat_gid("user/0/com.example/normal"));
    EXPECT_EQ(20000, stat_gid("user/0/com.example/cache"));
    EXPECT_EQ(20000, stat_gid("user/0/com.example/cache/file"));

    EXPECT_EQ(0700, stat_mode("user/0/com.example/normal"));
    EXPECT_EQ(02771, stat_mode("user/0/com.example/cache"));
    EXPECT_EQ(0700, stat_mode("user/0/com.example/cache/file"));
}

TEST_F(ServiceTest, FixupAppData_Moved) {
    LOG(INFO) << "FixupAppData_Moved";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0700);
    mkdir("user/0/com.example/bar", 10000, 20000, 0700);
    touch("user/0/com.example/bar/file", 10000, 20000, 0700);

    service->fixupAppData(testUuid, 0);

    EXPECT_EQ(10000, stat_gid("user/0/com.example/foo"));
    EXPECT_EQ(20000, stat_gid("user/0/com.example/foo/file"));
    EXPECT_EQ(10000, stat_gid("user/0/com.example/bar"));
    EXPECT_EQ(10000, stat_gid("user/0/com.example/bar/file"));

    service->fixupAppData(testUuid, FLAG_FORCE);

    EXPECT_EQ(10000, stat_gid("user/0/com.example/foo"));
    EXPECT_EQ(10000, stat_gid("user/0/com.example/foo/file"));
    EXPECT_EQ(10000, stat_gid("user/0/com.example/bar"));
    EXPECT_EQ(10000, stat_gid("user/0/com.example/bar/file"));
}

TEST_F(ServiceTest, DestroyUserData) {
    LOG(INFO) << "DestroyUserData";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0700);
    mkdir("user/0/com.example/bar", 10000, 20000, 0700);
    touch("user/0/com.example/bar/file", 10000, 20000, 0700);

    EXPECT_TRUE(exists("user/0/com.example/foo"));
    EXPECT_TRUE(exists("user/0/com.example/foo/file"));
    EXPECT_TRUE(exists("user/0/com.example/bar"));
    EXPECT_TRUE(exists("user/0/com.example/bar/file"));

    service->destroyUserData(testUuid, 0, FLAG_STORAGE_DE | FLAG_STORAGE_CE);

    EXPECT_FALSE(exists("user/0/com.example/foo"));
    EXPECT_FALSE(exists("user/0/com.example/foo/file"));
    EXPECT_FALSE(exists("user/0/com.example/bar"));
    EXPECT_FALSE(exists("user/0/com.example/bar/file"));

    EXPECT_FALSE(exists_renamed_deleted_dir("/user/0"));
}

TEST_F(ServiceTest, DestroyAppData) {
    LOG(INFO) << "DestroyAppData";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0700);
    mkdir("user/0/com.example/bar", 10000, 20000, 0700);
    touch("user/0/com.example/bar/file", 10000, 20000, 0700);

    EXPECT_TRUE(exists("user/0/com.example/foo"));
    EXPECT_TRUE(exists("user/0/com.example/foo/file"));
    EXPECT_TRUE(exists("user/0/com.example/bar"));
    EXPECT_TRUE(exists("user/0/com.example/bar/file"));

    service->destroyAppData(testUuid, "com.example", 0, FLAG_STORAGE_DE | FLAG_STORAGE_CE, 0, 0);

    EXPECT_FALSE(exists("user/0/com.example/foo"));
    EXPECT_FALSE(exists("user/0/com.example/foo/file"));
    EXPECT_FALSE(exists("user/0/com.example/bar"));
    EXPECT_FALSE(exists("user/0/com.example/bar/file"));

    EXPECT_FALSE(exists_renamed_deleted_dir("/user/0"));
}

TEST_F(ServiceTest, CleanupInvalidPackageDirs) {
    LOG(INFO) << "CleanupInvalidPackageDirs";

    std::string rootDirectoryPrefix[] = {"user/0", "misc_ce/0/sdksandbox", "misc_de/0/sdksandbox"};
    for (auto& prefix : rootDirectoryPrefix) {
        mkdir(prefix + "/5b14b6458a44==deleted==", 10000, 10000, 0700);
        mkdir(prefix + "/5b14b6458a44==deleted==/foo", 10000, 10000, 0700);
        touch(prefix + "/5b14b6458a44==deleted==/foo/file", 10000, 20000, 0700);
        mkdir(prefix + "/5b14b6458a44==deleted==/bar", 10000, 20000, 0700);
        touch(prefix + "/5b14b6458a44==deleted==/bar/file", 10000, 20000, 0700);

        auto fd = create(prefix + "/5b14b6458a44==deleted==/bar/opened_file", 10000, 20000, 0700);

        mkdir(prefix + "/b14b6458a44NOTdeleted", 10000, 10000, 0700);
        mkdir(prefix + "/b14b6458a44NOTdeleted/foo", 10000, 10000, 0700);
        touch(prefix + "/b14b6458a44NOTdeleted/foo/file", 10000, 20000, 0700);
        mkdir(prefix + "/b14b6458a44NOTdeleted/bar", 10000, 20000, 0700);
        touch(prefix + "/b14b6458a44NOTdeleted/bar/file", 10000, 20000, 0700);

        mkdir(prefix + "/com.example", 10000, 10000, 0700);
        mkdir(prefix + "/com.example/foo", 10000, 10000, 0700);
        touch(prefix + "/com.example/foo/file", 10000, 20000, 0700);
        mkdir(prefix + "/com.example/bar", 10000, 20000, 0700);
        touch(prefix + "/com.example/bar/file", 10000, 20000, 0700);

        mkdir(prefix + "/==deleted==", 10000, 10000, 0700);
        mkdir(prefix + "/==deleted==/foo", 10000, 10000, 0700);
        touch(prefix + "/==deleted==/foo/file", 10000, 20000, 0700);
        mkdir(prefix + "/==deleted==/bar", 10000, 20000, 0700);
        touch(prefix + "/==deleted==/bar/file", 10000, 20000, 0700);

        EXPECT_TRUE(exists(prefix + "/5b14b6458a44==deleted==/foo"));
        EXPECT_TRUE(exists(prefix + "/5b14b6458a44==deleted==/foo/file"));
        EXPECT_TRUE(exists(prefix + "/5b14b6458a44==deleted==/bar"));
        EXPECT_TRUE(exists(prefix + "/5b14b6458a44==deleted==/bar/file"));
        EXPECT_TRUE(exists(prefix + "/5b14b6458a44==deleted==/bar/opened_file"));

        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/foo"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/foo/file"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/bar"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/bar/file"));

        EXPECT_TRUE(exists(prefix + "/com.example/foo"));
        EXPECT_TRUE(exists(prefix + "/com.example/foo/file"));
        EXPECT_TRUE(exists(prefix + "/com.example/bar"));
        EXPECT_TRUE(exists(prefix + "/com.example/bar/file"));

        EXPECT_TRUE(exists(prefix + "/==deleted==/foo"));
        EXPECT_TRUE(exists(prefix + "/==deleted==/foo/file"));
        EXPECT_TRUE(exists(prefix + "/==deleted==/bar"));
        EXPECT_TRUE(exists(prefix + "/==deleted==/bar/file"));

        EXPECT_TRUE(exists_renamed_deleted_dir("/" + prefix));

        service->cleanupInvalidPackageDirs(testUuid, 0, FLAG_STORAGE_CE | FLAG_STORAGE_DE);

        EXPECT_EQ(::close(fd), 0);

        EXPECT_FALSE(exists(prefix + "/5b14b6458a44==deleted==/foo"));
        EXPECT_FALSE(exists(prefix + "/5b14b6458a44==deleted==/foo/file"));
        EXPECT_FALSE(exists(prefix + "/5b14b6458a44==deleted==/bar"));
        EXPECT_FALSE(exists(prefix + "/5b14b6458a44==deleted==/bar/file"));
        EXPECT_FALSE(exists(prefix + "/5b14b6458a44==deleted==/bar/opened_file"));

        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/foo"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/foo/file"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/bar"));
        EXPECT_TRUE(exists(prefix + "/b14b6458a44NOTdeleted/bar/file"));

        EXPECT_TRUE(exists(prefix + "/com.example/foo"));
        EXPECT_TRUE(exists(prefix + "/com.example/foo/file"));
        EXPECT_TRUE(exists(prefix + "/com.example/bar"));
        EXPECT_TRUE(exists(prefix + "/com.example/bar/file"));

        EXPECT_FALSE(exists(prefix + "/==deleted==/foo"));
        EXPECT_FALSE(exists(prefix + "/==deleted==/foo/file"));
        EXPECT_FALSE(exists(prefix + "/==deleted==/bar"));
        EXPECT_FALSE(exists(prefix + "/==deleted==/bar/file"));

        EXPECT_FALSE(exists_renamed_deleted_dir(prefix));
    }
}

TEST_F(ServiceTest, HashSecondaryDex) {
    LOG(INFO) << "HashSecondaryDex";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0700);

    std::vector<uint8_t> result;
    std::string dexPath = get_full_path("user/0/com.example/foo/file");
    EXPECT_BINDER_SUCCESS(service->hashSecondaryDexFile(
        dexPath, "com.example", 10000, testUuid, FLAG_STORAGE_CE, &result));

    EXPECT_EQ(result.size(), 32U);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (auto b : result) {
        output << std::setw(2) << +b;
    }

    // This is the SHA256 of an empty string (sha256sum /dev/null)
    EXPECT_EQ(output.str(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(ServiceTest, HashSecondaryDex_NoSuch) {
    LOG(INFO) << "HashSecondaryDex_NoSuch";

    std::vector<uint8_t> result;
    std::string dexPath = get_full_path("user/0/com.example/foo/file");
    EXPECT_BINDER_SUCCESS(service->hashSecondaryDexFile(
        dexPath, "com.example", 10000, testUuid, FLAG_STORAGE_CE, &result));

    EXPECT_EQ(result.size(), 0U);
}

TEST_F(ServiceTest, HashSecondaryDex_Unreadable) {
    LOG(INFO) << "HashSecondaryDex_Unreadable";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0300);

    std::vector<uint8_t> result;
    std::string dexPath = get_full_path("user/0/com.example/foo/file");
    EXPECT_BINDER_SUCCESS(service->hashSecondaryDexFile(
        dexPath, "com.example", 10000, testUuid, FLAG_STORAGE_CE, &result));

    EXPECT_EQ(result.size(), 0U);
}

TEST_F(ServiceTest, HashSecondaryDex_WrongApp) {
    LOG(INFO) << "HashSecondaryDex_WrongApp";

    mkdir("user/0/com.example", 10000, 10000, 0700);
    mkdir("user/0/com.example/foo", 10000, 10000, 0700);
    touch("user/0/com.example/foo/file", 10000, 20000, 0700);

    std::vector<uint8_t> result;
    std::string dexPath = get_full_path("user/0/com.example/foo/file");
    EXPECT_BINDER_FAIL(service->hashSecondaryDexFile(
        dexPath, "com.wrong", 10000, testUuid, FLAG_STORAGE_CE, &result));
}

TEST_F(ServiceTest, CalculateOat) {
    char buf[PKG_PATH_MAX];

    EXPECT_TRUE(calculate_oat_file_path(buf, "/path/to/oat", "/path/to/file.apk", "isa"));
    EXPECT_EQ("/path/to/oat/isa/file.odex", std::string(buf));

    EXPECT_FALSE(calculate_oat_file_path(buf, "/path/to/oat", "/path/to/file", "isa"));
    EXPECT_FALSE(calculate_oat_file_path(buf, "/path/to/oat", "file", "isa"));
}

TEST_F(ServiceTest, CalculateOdex) {
    char buf[PKG_PATH_MAX];

    EXPECT_TRUE(calculate_odex_file_path(buf, "/path/to/file.apk", "isa"));
    EXPECT_EQ("/path/to/oat/isa/file.odex", std::string(buf));
}

TEST_F(ServiceTest, CalculateCache) {
    char buf[PKG_PATH_MAX];

    EXPECT_TRUE(create_cache_path(buf, "/path/to/file.apk", "isa"));
    EXPECT_EQ("/data/dalvik-cache/isa/path@to@file.apk@classes.dex", std::string(buf));
}
TEST_F(ServiceTest, GetAppSizeManualForMedia) {
    struct stat s;

    std::string externalPicDir =
            StringPrintf("%s/Pictures", create_data_media_path(nullptr, 0).c_str());
    if (stat(externalPicDir.c_str(), &s) == 0) {
        // fetch the appId from the uid of the external storage owning app
        int32_t externalStorageAppId = multiuser_get_app_id(s.st_uid);
        // Fetch Package Name for the external storage owning app uid
        std::string pkg = get_package_name(s.st_uid);

        std::vector<int64_t> externalStorageSize, externalStorageSizeAfterAddingExternalFile;
        std::vector<int64_t> ceDataInodes;

        std::vector<std::string> codePaths;
        std::vector<std::string> packageNames;
        // set up parameters
        packageNames.push_back(pkg);
        ceDataInodes.push_back(0);
        // initialise the mounts
        service->invalidateMounts();
        // call the getAppSize to get the current size of the external storage owning app
        service->getAppSize(std::nullopt, packageNames, 0, InstalldNativeService::FLAG_USE_QUOTA,
                            externalStorageAppId, 0, ceDataInodes, codePaths, &externalStorageSize);
        // add a file with 20MB size to the external storage
        std::string externalFileLocation =
                StringPrintf("%s/Pictures/%s", getenv("EXTERNAL_STORAGE"), "External.jpg");
        std::string externalFileContentCommand =
                StringPrintf("dd if=/dev/zero of=%s bs=1M count=20", externalFileLocation.c_str());
        system(externalFileContentCommand.c_str());
        // call the getAppSize again to get the new size of the external storage owning app
        service->getAppSize(std::nullopt, packageNames, 0, InstalldNativeService::FLAG_USE_QUOTA,
                            externalStorageAppId, 0, ceDataInodes, codePaths,
                            &externalStorageSizeAfterAddingExternalFile);
        // check that the size before adding the file and after should be the same, as the app size
        // is not changed.
        for (size_t i = 0; i < externalStorageSize.size(); i++) {
            ASSERT_TRUE(externalStorageSize[i] == externalStorageSizeAfterAddingExternalFile[i]);
        }
        // remove the external file
        std::string removeCommand = StringPrintf("rm -f %s", externalFileLocation.c_str());
        system(removeCommand.c_str());
    }
}

TEST_F(ServiceTest, GetAppSizeWrongSizes) {
    int32_t externalStorageAppId = -1;
    std::vector<int64_t> externalStorageSize;

    std::vector<std::string> codePaths;
    std::vector<std::string> packageNames = {"package1", "package2"};
    std::vector<int64_t> ceDataInodes = {0};

    EXPECT_BINDER_FAIL(service->getAppSize(std::nullopt, packageNames, 0,
                                           InstalldNativeService::FLAG_USE_QUOTA,
                                           externalStorageAppId, 0, ceDataInodes, codePaths,
                                           &externalStorageSize));
}

TEST_F(ServiceTest, GetAppSize_NonPositiveAppId) {
    LOG(INFO) << "GetAppSize_NonPositiveAppId";
    int32_t appId = -1;
    int32_t pccId = kTestPccAppId;
    std::vector<int64_t> sizesWithoutQuota, sizesWithQuota;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {0};
    std::vector<std::string> codePaths = {"/data/app/com.foo"};

    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    // 2. Write PCC data (2MB each to PCC CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, appId, 0,
                                              ceDataInodes, codePaths, &sizesWithoutQuota));
    // Check that all app-related sizes are 0
    for (size_t i = 0; i < sizesWithoutQuota.size(); i++) {
        ASSERT_EQ(0, sizesWithoutQuota[i]);
    }

    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, appId, 0,
                                              ceDataInodes, codePaths, &sizesWithQuota));
    // Check that all app-related sizes are 0
    for (size_t i = 0; i < sizesWithQuota.size(); i++) {
        ASSERT_EQ(0, sizesWithQuota[i]);
    }

    // Cleanup
    service->destroyAppData(testUuid, "com.foo", kTestUserId, FLAG_STORAGE_CE | FLAG_STORAGE_DE, 0,
                            0);
}

TEST_F(ServiceTest, GetAppSize_NonPositiveAppIdWithPcc) {
    LOG(INFO) << "GetAppSize_NonPositiveAppIdWithPcc";
    int32_t appId = -1;
    int32_t pccId = kTestPccAppId;
    std::vector<int64_t> sizesWithoutQuota, sizesWithQuota;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {0};
    std::vector<std::string> codePaths = {"/data/app/com.foo"};

    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    // 2. Write PCC data (2MB each to PCC CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, appId, pccId,
                                              ceDataInodes, codePaths, &sizesWithoutQuota));

    // dataSize (sizes[1]) should be around 6MB (PCC data), NOT 9MB (app + PCC) or 3MB (app only).
    const int64_t kDataWrittenToPcc = 6 * 1024 * 1024;
    const int64_t kCacheDataWrittenToPcc = 2 * 1024 * 1024;
    const int64_t kBuffer = 0.5 * 1024 * 1024;
    EXPECT_GE(sizesWithoutQuota[1], kDataWrittenToPcc);
    EXPECT_LT(sizesWithoutQuota[1], kDataWrittenToPcc + kBuffer);
    EXPECT_GE(sizesWithoutQuota[2], kCacheDataWrittenToPcc);
    EXPECT_LT(sizesWithoutQuota[2], kCacheDataWrittenToPcc + kBuffer);

    // Other app-related sizes should be 0 (codeSize, cacheSize, etc.)
    for (size_t i = 0; i < sizesWithoutQuota.size(); i++) {
        if (i == 1 || i == 2) {
            continue;
        }
        EXPECT_EQ(0, sizesWithoutQuota[i]);
    }

    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, appId, pccId,
                                              ceDataInodes, codePaths, &sizesWithQuota));

    EXPECT_GE(sizesWithQuota[1], kDataWrittenToPcc);
    EXPECT_LT(sizesWithQuota[1], kDataWrittenToPcc + kBuffer);
    EXPECT_GE(sizesWithQuota[2], kCacheDataWrittenToPcc);
    EXPECT_LT(sizesWithQuota[2], kCacheDataWrittenToPcc + kBuffer);

    // Other app-related sizes should be 0
    for (size_t i = 0; i < sizesWithQuota.size(); i++) {
        if (i == 1 || i == 2) {
            continue;
        }
        EXPECT_EQ(0, sizesWithQuota[i]);
    }

    // Cleanup
    service->destroyAppData(testUuid, "com.foo", kTestUserId, FLAG_STORAGE_CE | FLAG_STORAGE_DE, 0,
                            0);
}

// TODO: b/479055375 - Write tests for PCC storage stats attribution where device supports Project
//  IDs
// PCC Storage Attribution Tests
TEST_F(ServiceTest, GetAppSize_ExcludesPccWhenPccIdZero) {
    LOG(INFO) << "GetAppSize_ExcludesPccWhenPccIdZero";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {result.ceDataInode};
    std::vector<std::string> codePaths = {};

    // Get initial size with pccId = 0 (should only count regular app data)
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, kTestAppId, 0,
                                              ceDataInodes, codePaths, &sizes1));

    // 2. Write PCC data (2MB each to PCC CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=2", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=2", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=2", pccCePath.c_str())
                   .c_str());

    // Get new size with pccId = 0
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, kTestAppId, 0,
                                              ceDataInodes, codePaths, &sizes2));

    // Verification: Sizes should be identical because PCC data is excluded when pccId = 0
    for (int i = 0; i < sizes1.size(); i++) {
        EXPECT_EQ(sizes1[i], sizes2[i]);
    }

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetAppSize_ExcludesPccWhenPccIdZeroUsingQuota) {
    LOG(INFO) << "GetAppSize_ExcludesPccWhenPccIdZeroUsingQuota";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {result.ceDataInode};
    std::vector<std::string> codePaths = {};

    // Get initial size with pccId = 0 (should only count regular app data)
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, kTestAppId, 0,
                                              ceDataInodes, codePaths, &sizes1));

    // 2. Write PCC data (2MB each to PCC CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=2", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=2", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=2", pccCePath.c_str())
                   .c_str());

    // Get new size with pccId = 0
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, kTestAppId, 0,
                                              ceDataInodes, codePaths, &sizes2));

    // Verification: Sizes should be identical because PCC data is excluded when pccId = 0
    for (int i = 0; i < sizes1.size(); i++) {
        EXPECT_EQ(sizes1[i], sizes2[i]);
    }

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetAppSize_IncludesPccWhenPccIdNonZero) {
    LOG(INFO) << "GetAppSize_IncludesPccWhenPccIdNonZero";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {result.ceDataInode};
    std::vector<std::string> codePaths = {};

    // Get initial size with pccId = kTestPccAppId
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, kTestAppId,
                                              kTestPccAppId, ceDataInodes, codePaths, &sizes1));

    // 2. Write app data (2MB each to CE, DE, and cache) to PCC path
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    // Get new size with pccId = kTestPccAppId
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId, 0, kTestAppId,
                                              kTestPccAppId, ceDataInodes, codePaths, &sizes2));

    verifyPccStatsIncluded(sizes1, sizes2, 9 * 1024 * 1024, 3 * 1024 * 1024);

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetAppSize_IncludesPccWhenPccIdNonZeroUsingQuota) {
    LOG(INFO) << "GetAppSize_IncludesPccWhenPccIdNonZeroUsingQuota";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<std::string> packageNames = {"com.foo"};
    std::vector<int64_t> ceDataInodes = {result.ceDataInode};
    std::vector<std::string> codePaths = {};

    // Get initial size with pccId = kTestPccAppId
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, kTestAppId,
                                              kTestPccAppId, ceDataInodes, codePaths, &sizes1));

    // 2. Write app data (1MB each to CE, DE, and cache) to PCC path
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    // Get new size with pccId = kTestPccAppId
    ASSERT_BINDER_SUCCESS(service->getAppSize(testUuid, packageNames, kTestUserId,
                                              InstalldNativeService::FLAG_USE_QUOTA, kTestAppId,
                                              kTestPccAppId, ceDataInodes, codePaths, &sizes2));

    verifyPccStatsIncluded(sizes1, sizes2, 9 * 1024 * 1024, 3 * 1024 * 1024);

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetUserSize_IncludesPccWhenPccIdNonZeroNotUsingQuota) {
    LOG(INFO) << "GetUserSize_IncludesPccWhenPccIdNonZeroNotUsingQuota";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<int32_t> appIds = {kTestAppId};
    std::vector<int32_t> pccIds = {kTestPccAppId};

    ASSERT_BINDER_SUCCESS(service->getUserSize(testUuid, kTestUserId, 0, appIds, pccIds, &sizes1));

    // 2. Write app data (1MB each to CE, DE, and cache) to PCC path
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    ASSERT_BINDER_SUCCESS(service->getUserSize(testUuid, kTestUserId, 0, appIds, pccIds, &sizes2));

    verifyPccStatsIncluded(sizes1, sizes2, 9 * 1024 * 1024, 3 * 1024 * 1024);

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetUserSize_IncludesPccWhenPccIdNonZeroUsingQuota) {
    LOG(INFO) << "GetUserSize_IncludesPccWhenPccIdNonZeroUsingQuota";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string cePath = get_full_path("user/0/com.foo");
    const std::string dePath = get_full_path("user_de/0/com.foo");
    const std::string pccCePath = get_full_path("user/0/com.foo-pcc");
    const std::string pccDePath = get_full_path("user_de/0/com.foo-pcc");

    // 1. Write initial app data (1MB each to CE, DE, and cache)
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", cePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=1M count=1", dePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=1M count=1", cePath.c_str())
                   .c_str());

    std::vector<int64_t> sizes1, sizes2;
    std::vector<int32_t> appIds = {kTestAppId};
    std::vector<int32_t> pccIds = {kTestPccAppId};

    ASSERT_BINDER_SUCCESS(service->getUserSize(testUuid, kTestUserId,
                                               InstalldNativeService::FLAG_USE_QUOTA, appIds,
                                               pccIds, &sizes1));

    // 2. Write app data (1MB each to CE, DE, and cache) to PCC path
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccCePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/file.txt bs=2M count=1", pccDePath.c_str()).c_str());
    system(StringPrintf("dd if=/dev/zero of=%s/cache/file.txt bs=2M count=1", pccCePath.c_str())
                   .c_str());

    ASSERT_BINDER_SUCCESS(service->getUserSize(testUuid, kTestUserId,
                                               InstalldNativeService::FLAG_USE_QUOTA, appIds,
                                               pccIds, &sizes2));

    // Verification: Sizes should be identical because PCC data is excluded when pccId is not passed
    verifyPccStatsIncluded(sizes1, sizes2, 9 * 1024 * 1024, 3 * 1024 * 1024);

    // Cleanup
    system(StringPrintf("rm -f %s/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", dePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", cePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccCePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/file.txt", pccDePath.c_str()).c_str());
    system(StringPrintf("rm -f %s/cache/file.txt", pccCePath.c_str()).c_str());
}

TEST_F(ServiceTest, GetUserSize_BinderFailsWhenArgListSizeMismatch) {
    LOG(INFO) << "GetUserSize_BinderFailsWhenArgListSizeMismatch";
    std::vector<int64_t> sizes1;
    std::vector<int32_t> appIds = {kTestAppId};
    std::vector<int32_t> pccIds = {}; // Empty pccIds

    ASSERT_BINDER_FAIL(service->getUserSize(testUuid, kTestUserId,
                                            InstalldNativeService::FLAG_USE_QUOTA, appIds, pccIds,
                                            &sizes1));

    binder::Status expect_status =
            service->getUserSize(testUuid, kTestUserId, InstalldNativeService::FLAG_USE_QUOTA,
                                 appIds, pccIds, &sizes1);
    ASSERT_TRUE(expect_status.exceptionCode() == binder::Status::EX_ILLEGAL_ARGUMENT);
    ASSERT_TRUE(expect_status.exceptionMessage() == "appIds and pccIds are not of the same length");
}

class FsverityTest : public ServiceTest {
protected:
    binder::Status createFsveritySetupAuthToken(const std::string& path, int open_mode,
                                                sp<IFsveritySetupAuthToken>* _aidl_return) {
        unique_fd ufd(open(path.c_str(), open_mode));
        EXPECT_GE(ufd.get(), 0) << "open failed: " << strerror(errno);
        ParcelFileDescriptor rfd(std::move(ufd));
        return service->createFsveritySetupAuthToken(std::move(rfd), kTestAppId, _aidl_return);
    }
};

TEST_F(FsverityTest, enableFsverity) {
    const std::string path = kTestPath + "/foo";
    create_with_content(path, kTestAppUid, kTestAppUid, 0600, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Expect to fs-verity setup to succeed
    sp<IFsveritySetupAuthToken> authToken;
    binder::Status status = createFsveritySetupAuthToken(path, O_RDWR, &authToken);
    EXPECT_TRUE(status.isOk());
    EXPECT_TRUE(authToken != nullptr);

    // Verity auth token works to enable fs-verity
    int32_t errno_local;
    status = service->enableFsverity(authToken, path, "fake.package.name", &errno_local);
    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(errno_local, 0);
}

TEST_F(FsverityTest, enableFsverity_nullAuthToken) {
    const std::string path = kTestPath + "/foo";
    create_with_content(path, kTestAppUid, kTestAppUid, 0600, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Verity null auth token fails
    sp<IFsveritySetupAuthToken> authToken;
    int32_t errno_local;
    binder::Status status =
            service->enableFsverity(authToken, path, "fake.package.name", &errno_local);
    EXPECT_FALSE(status.isOk());
}

TEST_F(FsverityTest, enableFsverity_differentFile) {
    const std::string path = kTestPath + "/foo";
    create_with_content(path, kTestAppUid, kTestAppUid, 0600, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Expect to fs-verity setup to succeed
    sp<IFsveritySetupAuthToken> authToken;
    binder::Status status = createFsveritySetupAuthToken(path, O_RDWR, &authToken);
    EXPECT_TRUE(status.isOk());
    EXPECT_TRUE(authToken != nullptr);

    // Verity auth token does not work for a different file
    const std::string anotherPath = kTestPath + "/bar";
    ASSERT_TRUE(android::base::WriteStringToFile("content", anotherPath));
    UniqueFile raii2(/*fd=*/-1, anotherPath, &unlink_path);
    int32_t errno_local;
    status = service->enableFsverity(authToken, anotherPath, "fake.package.name", &errno_local);
    EXPECT_TRUE(status.isOk());
    EXPECT_NE(errno_local, 0);
}

TEST_F(FsverityTest, enableFsverity_errnoBeforeAuthenticated) {
    const std::string path = kTestPath + "/foo";
    create_with_content(path, kTestAppUid, kTestAppUid, 0600, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Expect to fs-verity setup to succeed
    sp<IFsveritySetupAuthToken> authToken;
    binder::Status status = createFsveritySetupAuthToken(path, O_RDWR, &authToken);
    EXPECT_TRUE(status.isOk());
    EXPECT_TRUE(authToken != nullptr);

    // Verity errno before the fd authentication is constant (EPERM)
    int32_t errno_local;
    status = service->enableFsverity(authToken, path + "-non-exist", "fake.package.name",
                                     &errno_local);
    EXPECT_TRUE(status.isOk());
    EXPECT_EQ(errno_local, EPERM);
}

TEST_F(FsverityTest, createFsveritySetupAuthToken_ReadonlyFdDoesNotAuthenticate) {
    const std::string path = kTestPath + "/foo";
    create_with_content(path, kTestAppUid, kTestAppUid, 0600, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Expect the fs-verity setup to fail
    sp<IFsveritySetupAuthToken> authToken;
    binder::Status status = createFsveritySetupAuthToken(path, O_RDONLY, &authToken);
    EXPECT_FALSE(status.isOk());
}

TEST_F(FsverityTest, createFsveritySetupAuthToken_UnownedFile) {
    const std::string path = kTestPath + "/foo";
    // Simulate world-writable file owned by another app
    create_with_content(path, kTestAppUid + 1, kTestAppUid + 1, 0666, "content");
    UniqueFile raii(/*fd=*/-1, path, &unlink_path);

    // Expect the fs-verity setup to fail
    sp<IFsveritySetupAuthToken> authToken;
    binder::Status status = createFsveritySetupAuthToken(path, O_RDWR, &authToken);
    EXPECT_FALSE(status.isOk());
}

static bool mkdirs(const std::string& path, mode_t mode) {
    struct stat sb;
    if (stat(path.c_str(), &sb) != -1 && S_ISDIR(sb.st_mode)) {
        return true;
    }

    if (!mkdirs(android::base::Dirname(path), mode)) {
        return false;
    }

    if (::mkdir(path.c_str(), mode) != 0) {
        PLOG(DEBUG) << "Failed to create folder " << path;
        return false;
    }
    return true;
}

class AppDataSnapshotTest : public testing::Test {
private:
    std::string rollback_ce_base_dir;
    std::string rollback_de_base_dir;

protected:
    InstalldNativeService* service;

    std::string fake_package_ce_path;
    std::string fake_package_de_path;

    virtual void SetUp() {
        setenv("ANDROID_LOG_TAGS", "*:v", 1);
        android::base::InitLogging(nullptr);

        service = new InstalldNativeService();
        ASSERT_TRUE(mkdirs("/data/local/tmp/user/0", 0700));

        init_globals_from_data_and_root();

        rollback_ce_base_dir = create_data_misc_ce_rollback_base_path("TEST", 0);
        rollback_de_base_dir = create_data_misc_de_rollback_base_path("TEST", 0);

        fake_package_ce_path = create_data_user_ce_package_path("TEST", 0, "com.foo");
        fake_package_de_path = create_data_user_de_package_path("TEST", 0, "com.foo");

        ASSERT_TRUE(mkdirs(rollback_ce_base_dir, 0700));
        ASSERT_TRUE(mkdirs(rollback_de_base_dir, 0700));
        ASSERT_TRUE(mkdirs(fake_package_ce_path, 0700));
        ASSERT_TRUE(mkdirs(fake_package_de_path, 0700));
    }

    virtual void TearDown() {
        ASSERT_EQ(0, delete_dir_contents_and_dir(rollback_ce_base_dir, true));
        ASSERT_EQ(0, delete_dir_contents_and_dir(rollback_de_base_dir, true));
        ASSERT_EQ(0, delete_dir_contents(fake_package_ce_path, true));
        ASSERT_EQ(0, delete_dir_contents(fake_package_de_path, true));

        delete service;
        ASSERT_EQ(0, delete_dir_contents_and_dir("/data/local/tmp/user/0", true));
    }
};

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 37);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 37);

  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  // Request a snapshot of the CE content but not the DE content.
  int64_t ce_snapshot_inode;
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 37, FLAG_STORAGE_CE, &ce_snapshot_inode));
  struct stat buf;
  memset(&buf, 0, sizeof(buf));
  ASSERT_EQ(0, stat((rollback_ce_dir + "/com.foo").c_str(), &buf));
  ASSERT_EQ(ce_snapshot_inode, (int64_t) buf.st_ino);

  std::string ce_content, de_content;
  // At this point, we should have the CE content but not the DE content.
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_ce_dir + "/com.foo/file1", &ce_content, false /* follow_symlinks */));
  ASSERT_FALSE(android::base::ReadFileToString(
      rollback_de_dir + "/com.foo/file1", &de_content, false /* follow_symlinks */));
  ASSERT_EQ("TEST_CONTENT_CE", ce_content);

  // Modify the CE content, so we can assert later that it's reflected
  // in the snapshot.
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE_MODIFIED", fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  // Request a snapshot of the DE content but not the CE content.
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 37, FLAG_STORAGE_DE, &ce_snapshot_inode));
  // Only DE content snapshot was requested.
  ASSERT_EQ(ce_snapshot_inode, 0);

  // At this point, both the CE as well as the DE content should be fully
  // populated.
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_ce_dir + "/com.foo/file1", &ce_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_de_dir + "/com.foo/file1", &de_content, false /* follow_symlinks */));
  ASSERT_EQ("TEST_CONTENT_CE", ce_content);
  ASSERT_EQ("TEST_CONTENT_DE", de_content);

  // Modify the DE content, so we can assert later that it's reflected
  // in our final snapshot.
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE_MODIFIED", fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  // Request a snapshot of both the CE as well as the DE content.
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 37, FLAG_STORAGE_DE | FLAG_STORAGE_CE,
                                                 nullptr));

  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_ce_dir + "/com.foo/file1", &ce_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_de_dir + "/com.foo/file1", &de_content, false /* follow_symlinks */));
  ASSERT_EQ("TEST_CONTENT_CE_MODIFIED", ce_content);
  ASSERT_EQ("TEST_CONTENT_DE_MODIFIED", de_content);
}

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot_TwoSnapshotsWithTheSameId) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 67);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 67);

  auto another_fake_package_ce_path = create_data_user_ce_package_path("TEST", 0, "com.bar");
  auto another_fake_package_de_path = create_data_user_de_package_path("TEST", 0, "com.bar");

  // Since this test sets up data for another package, some bookkeeping is required.
  auto deleter = [&]() {
      ASSERT_EQ(0, delete_dir_contents_and_dir(another_fake_package_ce_path, true));
      ASSERT_EQ(0, delete_dir_contents_and_dir(another_fake_package_de_path, true));
  };
  auto scope_guard = android::base::make_scope_guard(deleter);

  ASSERT_TRUE(mkdirs(another_fake_package_ce_path, 0700));
  ASSERT_TRUE(mkdirs(another_fake_package_de_path, 0700));

  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "ANOTHER_TEST_CONTENT_CE", another_fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "ANOTHER_TEST_CONTENT_DE", another_fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  // Request snapshot for the package com.foo.
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 67, FLAG_STORAGE_DE | FLAG_STORAGE_CE,
                                                 nullptr));
  // Now request snapshot with the same id for the package com.bar
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.bar",
                                                 0, 67, FLAG_STORAGE_DE | FLAG_STORAGE_CE,
                                                 nullptr));

  // Check that both snapshots have correct data in them.
  std::string com_foo_ce_content, com_foo_de_content;
  std::string com_bar_ce_content, com_bar_de_content;
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_ce_dir + "/com.foo/file1", &com_foo_ce_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_de_dir + "/com.foo/file1", &com_foo_de_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_ce_dir + "/com.bar/file1", &com_bar_ce_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      rollback_de_dir + "/com.bar/file1", &com_bar_de_content, false /* follow_symlinks */));
  ASSERT_EQ("TEST_CONTENT_CE", com_foo_ce_content);
  ASSERT_EQ("TEST_CONTENT_DE", com_foo_de_content);
  ASSERT_EQ("ANOTHER_TEST_CONTENT_CE", com_bar_ce_content);
  ASSERT_EQ("ANOTHER_TEST_CONTENT_DE", com_bar_de_content);
}

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot_AppDataAbsent) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 73);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 73);

  // Similuating app data absence.
  ASSERT_EQ(0, delete_dir_contents_and_dir(fake_package_ce_path, true));
  ASSERT_EQ(0, delete_dir_contents_and_dir(fake_package_de_path, true));

  int64_t ce_snapshot_inode;
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 73, FLAG_STORAGE_CE, &ce_snapshot_inode));
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 73, FLAG_STORAGE_DE, nullptr));
  // No CE content snapshot was performed.
  ASSERT_EQ(ce_snapshot_inode, 0);

  // The snapshot calls must succeed but there should be no snapshot
  // created.
  struct stat sb;
  ASSERT_EQ(-1, stat((rollback_ce_dir + "/com.foo").c_str(), &sb));
  ASSERT_EQ(-1, stat((rollback_de_dir + "/com.foo").c_str(), &sb));
}

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot_ClearsExistingSnapshot) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_package_path("TEST", 0, 13, "com.foo");
  auto rollback_de_dir = create_data_misc_de_rollback_package_path("TEST", 0, 13, "com.foo");

  ASSERT_TRUE(mkdirs(rollback_ce_dir, 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir, 0700));

  // Simulate presence of an existing snapshot
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", rollback_ce_dir + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", rollback_de_dir + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  // Create app data.
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_2_CE", fake_package_ce_path + "/file2",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_2_DE", fake_package_de_path + "/file2",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 13, FLAG_STORAGE_DE | FLAG_STORAGE_CE,
                                                 nullptr));

  // Previous snapshot (with data for file1) must be cleared.
  struct stat sb;
  ASSERT_EQ(-1, stat((rollback_ce_dir + "/file1").c_str(), &sb));
  ASSERT_EQ(-1, stat((rollback_de_dir + "/file1").c_str(), &sb));
  // New snapshot (with data for file2) must be present.
  ASSERT_NE(-1, stat((rollback_ce_dir + "/file2").c_str(), &sb));
  ASSERT_NE(-1, stat((rollback_de_dir + "/file2").c_str(), &sb));
}

TEST_F(AppDataSnapshotTest, SnapshotAppData_WrongVolumeUuid) {
  // Setup rollback folders to make sure that fails due to wrong volumeUuid being
  // passed, not because of some other reason.
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 17);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 17);

  ASSERT_TRUE(mkdirs(rollback_ce_dir, 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir, 0700));

  EXPECT_BINDER_FAIL(service->snapshotAppData(std::make_optional<std::string>("FOO"), "com.foo", 0,
                                              17, FLAG_STORAGE_DE, nullptr));
}

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot_ClearsCache) {
  auto fake_package_ce_cache_path = fake_package_ce_path + "/cache";
  auto fake_package_ce_code_cache_path = fake_package_ce_path + "/code_cache";
  auto fake_package_de_cache_path = fake_package_de_path + "/cache";
  auto fake_package_de_code_cache_path = fake_package_de_path + "/code_cache";

  ASSERT_TRUE(mkdirs(fake_package_ce_cache_path, 0700));
  ASSERT_TRUE(mkdirs(fake_package_ce_code_cache_path, 0700));
  ASSERT_TRUE(mkdirs(fake_package_de_cache_path, 0700));
  ASSERT_TRUE(mkdirs(fake_package_de_code_cache_path, 0700));

  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_cache_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_code_cache_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_cache_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_code_cache_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo",
                                                 0, 23, FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                 nullptr));
  // The snapshot call must clear cache.
  struct stat sb;
  ASSERT_EQ(-1, stat((fake_package_ce_cache_path + "/file1").c_str(), &sb));
  ASSERT_EQ(-1, stat((fake_package_ce_code_cache_path + "/file1").c_str(), &sb));
  ASSERT_EQ(-1, stat((fake_package_de_cache_path + "/file1").c_str(), &sb));
  ASSERT_EQ(-1, stat((fake_package_de_code_cache_path + "/file1").c_str(), &sb));
}

TEST_F(AppDataSnapshotTest, RestoreAppDataSnapshot) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 239);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 239);

  ASSERT_TRUE(mkdirs(rollback_ce_dir, 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir, 0700));

  // Write contents to the rollback location. We'll write the same files to the
  // app data location and make sure the restore has overwritten them.
  ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo/", 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "DE_RESTORE_CONTENT", rollback_de_dir + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_BINDER_SUCCESS(service->restoreAppDataSnapshot(std::make_optional<std::string>("TEST"),
                                                        "com.foo", 10000, -1, "", 0, 239,
                                                        FLAG_STORAGE_DE | FLAG_STORAGE_CE));

  std::string ce_content, de_content;
  ASSERT_TRUE(android::base::ReadFileToString(
      fake_package_ce_path + "/file1", &ce_content, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::ReadFileToString(
      fake_package_de_path + "/file1", &de_content, false /* follow_symlinks */));
  ASSERT_EQ("CE_RESTORE_CONTENT", ce_content);
  ASSERT_EQ("DE_RESTORE_CONTENT", de_content);
}

TEST_F(AppDataSnapshotTest, CreateSnapshotThenDestroyIt) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 57);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 57);

  // Prepare data for snapshot.
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_CE", fake_package_ce_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "TEST_CONTENT_DE", fake_package_de_path + "/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  int64_t ce_snapshot_inode;
  // Request a snapshot of both the CE as well as the DE content.
  ASSERT_TRUE(service->snapshotAppData(std::make_optional<std::string>("TEST"), "com.foo", 0, 57,
                                       FLAG_STORAGE_DE | FLAG_STORAGE_CE, &ce_snapshot_inode)
                      .isOk());
  // Because CE data snapshot was requested, ce_snapshot_inode can't be null.
  ASSERT_NE(0, ce_snapshot_inode);
  // Check snapshot is there.
  struct stat sb;
  ASSERT_EQ(0, stat((rollback_ce_dir + "/com.foo").c_str(), &sb));
  ASSERT_EQ(0, stat((rollback_de_dir + "/com.foo").c_str(), &sb));


  ASSERT_TRUE(service->destroyAppDataSnapshot(std::make_optional<std::string>("TEST"),
          "com.foo", 0, ce_snapshot_inode, 57, FLAG_STORAGE_DE | FLAG_STORAGE_CE).isOk());
  // Check snapshot is deleted.
  ASSERT_EQ(-1, stat((rollback_ce_dir + "/com.foo").c_str(), &sb));
  ASSERT_EQ(-1, stat((rollback_de_dir + "/com.foo").c_str(), &sb));
}

TEST_F(AppDataSnapshotTest, DestroyAppDataSnapshot_CeSnapshotInodeIsZero) {
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 1543);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 1543);

  // Create a snapshot
  ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo/", 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "DE_RESTORE_CONTENT", rollback_de_dir + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_TRUE(service->destroyAppDataSnapshot(std::make_optional<std::string>("TEST"),
          "com.foo", 0, 0, 1543, FLAG_STORAGE_DE | FLAG_STORAGE_CE).isOk());

  // Check snapshot is deleted.
  struct stat sb;
  ASSERT_EQ(-1, stat((rollback_ce_dir + "/com.foo").c_str(), &sb));
  ASSERT_EQ(-1, stat((rollback_de_dir + "/com.foo").c_str(), &sb));

  // Check that deleting already deleted snapshot is no-op.
  ASSERT_TRUE(service->destroyAppDataSnapshot(std::make_optional<std::string>("TEST"),
          "com.foo", 0, 0, 1543, FLAG_STORAGE_DE | FLAG_STORAGE_CE).isOk());
}

TEST_F(AppDataSnapshotTest, DestroyAppDataSnapshot_WrongVolumeUuid) {
  // Setup rollback data to make sure that test fails due to wrong volumeUuid
  // being passed, not because of some other reason.
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 43);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 43);

  ASSERT_TRUE(mkdirs(rollback_ce_dir, 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir, 0700));

  ASSERT_FALSE(service->destroyAppDataSnapshot(std::make_optional<std::string>("BAR"),
          "com.foo", 0, 0, 43, FLAG_STORAGE_DE).isOk());
}

TEST_F(AppDataSnapshotTest, DestroyCeSnapshotsNotSpecified) {
  auto rollback_ce_dir_in_1 = create_data_misc_ce_rollback_path("TEST", 0, 1543);
  auto rollback_ce_dir_in_2 = create_data_misc_ce_rollback_path("TEST", 0, 77);
  auto rollback_ce_dir_out_1 = create_data_misc_ce_rollback_path("TEST", 0, 1500);
  auto rollback_ce_dir_out_2 = create_data_misc_ce_rollback_path("TEST", 0, 2);

  // Create snapshots
  ASSERT_TRUE(mkdirs(rollback_ce_dir_in_1 + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir_in_1 + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_TRUE(mkdirs(rollback_ce_dir_in_2 + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir_in_2 + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_TRUE(mkdirs(rollback_ce_dir_out_1 + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir_out_1 + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_TRUE(mkdirs(rollback_ce_dir_out_2 + "/com.foo/", 0700));
  ASSERT_TRUE(android::base::WriteStringToFile(
          "CE_RESTORE_CONTENT", rollback_ce_dir_out_2 + "/com.foo/file1",
          0700, 10000, 20000, false /* follow_symlinks */));

  ASSERT_TRUE(service->destroyCeSnapshotsNotSpecified(
          std::make_optional<std::string>("TEST"), 0, { 1543, 77 }).isOk());

  // Check only snapshots not specified are deleted.
  struct stat sb;
  ASSERT_EQ(0, stat((rollback_ce_dir_in_1 + "/com.foo").c_str(), &sb));
  ASSERT_EQ(0, stat((rollback_ce_dir_in_2 + "/com.foo").c_str(), &sb));
  ASSERT_EQ(-1, stat((rollback_ce_dir_out_1 + "/com.foo").c_str(), &sb));
  ASSERT_EQ(ENOENT, errno);
  ASSERT_EQ(-1, stat((rollback_ce_dir_out_2 + "/com.foo").c_str(), &sb));
  ASSERT_EQ(ENOENT, errno);
}

TEST_F(AppDataSnapshotTest, RestoreAppDataSnapshot_WrongVolumeUuid) {
  // Setup rollback data to make sure that fails due to wrong volumeUuid being
  // passed, not because of some other reason.
  auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 41);
  auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 41);

  ASSERT_TRUE(mkdirs(rollback_ce_dir, 0700));
  ASSERT_TRUE(mkdirs(rollback_de_dir, 0700));

  EXPECT_BINDER_FAIL(service->restoreAppDataSnapshot(std::make_optional<std::string>("BAR"),
                                                     "com.foo", 10000, -1, "", 0, 41,
                                                     FLAG_STORAGE_DE));
}

TEST_F(AppDataSnapshotTest, CreateAppDataSnapshot_WithPcc) {
    auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 37);
    auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 37);

    auto fake_package_pcc_ce_path = fake_package_ce_path + "-pcc";
    auto fake_package_pcc_de_path = fake_package_de_path + "-pcc";
    ASSERT_TRUE(mkdirs(fake_package_pcc_ce_path, 0700));
    ASSERT_TRUE(mkdirs(fake_package_pcc_de_path, 0700));

    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_CE", fake_package_ce_path + "/file1",
                                                 0700, 10000, 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_DE", fake_package_de_path + "/file1",
                                                 0700, 10000, 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_PCC_CE",
                                                 fake_package_pcc_ce_path + "/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_PCC_DE",
                                                 fake_package_pcc_de_path + "/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));

    ASSERT_BINDER_SUCCESS(service->snapshotAppData(std::make_optional<std::string>("TEST"),
                                                   "com.foo", 0, 37,
                                                   FLAG_STORAGE_CE | FLAG_STORAGE_DE, nullptr));

    std::string ce_content, de_content, pcc_ce_content, pcc_de_content;
    ASSERT_TRUE(android::base::ReadFileToString(rollback_ce_dir + "/com.foo/file1", &ce_content,
                                                false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(rollback_de_dir + "/com.foo/file1", &de_content,
                                                false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(rollback_ce_dir + "/com.foo-pcc/file1",
                                                &pcc_ce_content, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(rollback_de_dir + "/com.foo-pcc/file1",
                                                &pcc_de_content, false /* follow_symlinks */));
    ASSERT_EQ("TEST_CONTENT_CE", ce_content);
    ASSERT_EQ("TEST_CONTENT_DE", de_content);
    ASSERT_EQ("TEST_CONTENT_PCC_CE", pcc_ce_content);
    ASSERT_EQ("TEST_CONTENT_PCC_DE", pcc_de_content);
}

TEST_F(AppDataSnapshotTest, RestoreAppDataSnapshot_WithPcc) {
    auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 239);
    auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 239);

    ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo/", 0700));
    ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo/", 0700));
    ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo-pcc/", 0700));
    ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo-pcc/", 0700));

    ASSERT_TRUE(android::base::WriteStringToFile("CE_RESTORE_CONTENT",
                                                 rollback_ce_dir + "/com.foo/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("DE_RESTORE_CONTENT",
                                                 rollback_de_dir + "/com.foo/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("PCC_CE_RESTORE_CONTENT",
                                                 rollback_ce_dir + "/com.foo-pcc/file1", 0700,
                                                 10000, 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("PCC_DE_RESTORE_CONTENT",
                                                 rollback_de_dir + "/com.foo-pcc/file1", 0700,
                                                 10000, 20000, false /* follow_symlinks */));

    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_CE", fake_package_ce_path + "/file1",
                                                 0700, 10000, 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_CONTENT_DE", fake_package_de_path + "/file1",
                                                 0700, 10000, 20000, false /* follow_symlinks */));
    auto fake_package_pcc_ce_path = fake_package_ce_path + "-pcc";
    auto fake_package_pcc_de_path = fake_package_de_path + "-pcc";
    ASSERT_TRUE(mkdirs(fake_package_pcc_ce_path, 0700));
    ASSERT_TRUE(mkdirs(fake_package_pcc_de_path, 0700));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_PCC_CONTENT_CE",
                                                 fake_package_pcc_ce_path + "/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::WriteStringToFile("TEST_PCC_CONTENT_DE",
                                                 fake_package_pcc_de_path + "/file1", 0700, 10000,
                                                 20000, false /* follow_symlinks */));

    ASSERT_BINDER_SUCCESS(service->restoreAppDataSnapshot(std::make_optional<std::string>("TEST"),
                                                          "com.foo", 10000, 30000, "", 0, 239,
                                                          FLAG_STORAGE_DE | FLAG_STORAGE_CE));

    std::string ce_content, de_content, pcc_ce_content, pcc_de_content;
    ASSERT_TRUE(android::base::ReadFileToString(fake_package_ce_path + "/file1", &ce_content,
                                                false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(fake_package_de_path + "/file1", &de_content,
                                                false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(fake_package_pcc_ce_path + "/file1",
                                                &pcc_ce_content, false /* follow_symlinks */));
    ASSERT_TRUE(android::base::ReadFileToString(fake_package_pcc_de_path + "/file1",
                                                &pcc_de_content, false /* follow_symlinks */));
    ASSERT_EQ("CE_RESTORE_CONTENT", ce_content);
    ASSERT_EQ("DE_RESTORE_CONTENT", de_content);
    ASSERT_EQ("PCC_CE_RESTORE_CONTENT", pcc_ce_content);
    ASSERT_EQ("PCC_DE_RESTORE_CONTENT", pcc_de_content);
}

TEST_F(AppDataSnapshotTest, DestroyAppDataSnapshot_WithPcc) {
    auto rollback_ce_dir = create_data_misc_ce_rollback_path("TEST", 0, 57);
    auto rollback_de_dir = create_data_misc_de_rollback_path("TEST", 0, 57);
    ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo/", 0700));
    ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo/", 0700));
    ASSERT_TRUE(mkdirs(rollback_ce_dir + "/com.foo-pcc/", 0700));
    ASSERT_TRUE(mkdirs(rollback_de_dir + "/com.foo-pcc/", 0700));

    ASSERT_BINDER_SUCCESS(service->destroyAppDataSnapshot(std::make_optional<std::string>("TEST"),
                                                          "com.foo", 0, 0, 57,
                                                          FLAG_STORAGE_DE | FLAG_STORAGE_CE));

    struct stat sb;
    ASSERT_EQ(-1, stat((rollback_ce_dir + "/com.foo").c_str(), &sb));
    ASSERT_EQ(-1, stat((rollback_de_dir + "/com.foo").c_str(), &sb));
    ASSERT_EQ(-1, stat((rollback_ce_dir + "/com.foo-pcc").c_str(), &sb));
    ASSERT_EQ(-1, stat((rollback_de_dir + "/com.foo-pcc").c_str(), &sb));
}

class SdkSandboxDataTest : public testing::Test {
public:
    void CheckFileAccess(const std::string& path, uid_t uid, gid_t gid, mode_t mode) {
        const auto fullPath = "/data/local/tmp/" + path;
        ASSERT_TRUE(exists(fullPath.c_str())) << "For path: " << fullPath;
        struct stat st;
        ASSERT_EQ(0, stat(fullPath.c_str(), &st));
        ASSERT_EQ(uid, st.st_uid) << "For path: " << fullPath;
        ASSERT_EQ(gid, st.st_gid) << "For path: " << fullPath;
        ASSERT_EQ(mode, st.st_mode) << "For path: " << fullPath;
    }

    bool exists(const char* path) { return ::access(path, F_OK) == 0; }

    // Creates a default CreateAppDataArgs object
    android::os::CreateAppDataArgs createAppDataArgs(const std::string& packageName) {
        android::os::CreateAppDataArgs args;
        args.uuid = kTestUuid;
        args.packageName = packageName;
        args.userId = kTestUserId;
        args.appId = kTestAppId;
        args.seInfo = "default";
        args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE | FLAG_STORAGE_SDK;
        return args;
    }

    android::os::ReconcileSdkDataArgs reconcileSdkDataArgs(
            const std::string& packageName, const std::vector<std::string>& subDirNames) {
        android::os::ReconcileSdkDataArgs args;
        args.uuid = kTestUuid;
        args.packageName = packageName;
        for (const auto& subDirName : subDirNames) {
            args.subDirNames.push_back(subDirName);
        }
        args.userId = kTestUserId;
        args.appId = kTestAppId;
        args.previousAppId = -1;
        args.seInfo = "default";
        args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;
        return args;
    }

protected:
    InstalldNativeService* service;

    virtual void SetUp() {
        setenv("ANDROID_LOG_TAGS", "*:v", 1);
        android::base::InitLogging(nullptr);

        service = new InstalldNativeService();
        clearAppData();
        ASSERT_TRUE(mkdirs("/data/local/tmp/user/0", 0700));
        ASSERT_TRUE(mkdirs("/data/local/tmp/user_de/0", 0700));
        ASSERT_TRUE(mkdirs("/data/local/tmp/misc_ce/0/sdksandbox", 0700));
        ASSERT_TRUE(mkdirs("/data/local/tmp/misc_de/0/sdksandbox", 0700));

        init_globals_from_data_and_root();
    }

    virtual void TearDown() {
        delete service;
        clearAppData();
    }

private:
    void clearAppData() {
        ASSERT_EQ(0, delete_dir_contents_and_dir("/data/local/tmp/user", true));
        ASSERT_EQ(0, delete_dir_contents_and_dir("/data/local/tmp/user_de", true));
        ASSERT_EQ(0, delete_dir_contents_and_dir("/data/local/tmp/misc_ce", true));
        ASSERT_EQ(0, delete_dir_contents_and_dir("/data/local/tmp/misc_de", true));
    }
};

TEST_F(SdkSandboxDataTest, CreateAppData_CreatesSdkPackageData) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");

    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    const std::string fooCePath = "misc_ce/0/sdksandbox/com.foo";
    CheckFileAccess(fooCePath, kSystemUid, kSystemUid, S_IFDIR | 0751);

    const std::string fooDePath = "misc_de/0/sdksandbox/com.foo";
    CheckFileAccess(fooDePath, kSystemUid, kSystemUid, S_IFDIR | 0751);
}

TEST_F(SdkSandboxDataTest, CreateAppData_CreatesSdkPackageData_WithoutSdkFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
}

TEST_F(SdkSandboxDataTest, CreateAppData_CreatesSdkPackageData_WithoutSdkFlagDeletesExisting) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    ASSERT_TRUE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
    ASSERT_TRUE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));

    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
}

TEST_F(SdkSandboxDataTest, CreateAppData_CreatesSdkPackageData_WithoutDeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_SDK;

    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    // Only CE paths should exist
    CheckFileAccess("misc_ce/0/sdksandbox/com.foo", kSystemUid, kSystemUid, S_IFDIR | 0751);

    // DE paths should not exist
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
}

TEST_F(SdkSandboxDataTest, CreateAppData_CreatesSdkPackageData_WithoutCeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.flags = FLAG_STORAGE_DE | FLAG_STORAGE_SDK;

    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    // CE paths should not exist
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));

    // Only DE paths should exist
    CheckFileAccess("misc_de/0/sdksandbox/com.foo", kSystemUid, kSystemUid, S_IFDIR | 0751);
}

TEST_F(SdkSandboxDataTest, ReconcileSdkData) {
    android::os::ReconcileSdkDataArgs args =
            reconcileSdkDataArgs("com.foo", {"bar@random1", "baz@random2"});

    // Create the sdk data.
    ASSERT_BINDER_SUCCESS(service->reconcileSdkData(args));

    const std::string barCePath = "misc_ce/0/sdksandbox/com.foo/bar@random1";
    CheckFileAccess(barCePath, kTestSdkSandboxUid, kNobodyUid, S_IFDIR | S_ISGID | 0700);
    CheckFileAccess(barCePath + "/cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);
    CheckFileAccess(barCePath + "/code_cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);

    const std::string bazCePath = "misc_ce/0/sdksandbox/com.foo/baz@random2";
    CheckFileAccess(bazCePath, kTestSdkSandboxUid, kNobodyUid, S_IFDIR | S_ISGID | 0700);
    CheckFileAccess(bazCePath + "/cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);
    CheckFileAccess(bazCePath + "/code_cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);

    const std::string barDePath = "misc_de/0/sdksandbox/com.foo/bar@random1";
    CheckFileAccess(barDePath, kTestSdkSandboxUid, kNobodyUid, S_IFDIR | S_ISGID | 0700);
    CheckFileAccess(barDePath + "/cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);
    CheckFileAccess(barDePath + "/code_cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);

    const std::string bazDePath = "misc_de/0/sdksandbox/com.foo/baz@random2";
    CheckFileAccess(bazDePath, kTestSdkSandboxUid, kNobodyUid, S_IFDIR | S_ISGID | 0700);
    CheckFileAccess(bazDePath + "/cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);
    CheckFileAccess(bazDePath + "/code_cache", kTestSdkSandboxUid, kTestCacheGid,
                    S_IFDIR | S_ISGID | 0771);
}

TEST_F(SdkSandboxDataTest, ReconcileSdkData_ExtraCodeDirectoriesAreDeleted) {
    android::os::ReconcileSdkDataArgs args =
            reconcileSdkDataArgs("com.foo", {"bar@random1", "baz@random2"});

    // Create the sdksandbox data.
    ASSERT_BINDER_SUCCESS(service->reconcileSdkData(args));

    // Retry with different package name
    args.subDirNames[0] = "bar.diff@random1";

    // Create the sdksandbox data again
    ASSERT_BINDER_SUCCESS(service->reconcileSdkData(args));

    // New directoris should exist
    CheckFileAccess("misc_ce/0/sdksandbox/com.foo/bar.diff@random1", kTestSdkSandboxUid, kNobodyUid,
                    S_IFDIR | S_ISGID | 0700);
    CheckFileAccess("misc_ce/0/sdksandbox/com.foo/baz@random2", kTestSdkSandboxUid, kNobodyUid,
                    S_IFDIR | S_ISGID | 0700);
    // Directory for old unreferred sdksandbox package name should be removed
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo/bar@random1"));
}

class DestroyAppDataTest : public SdkSandboxDataTest {};

TEST_F(DestroyAppDataTest, DestroySdkSandboxDataDirectories_WithCeAndDeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.packageName = "com.foo";
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    // Destroy the app user data.
    ASSERT_BINDER_SUCCESS(service->destroyAppData(args.uuid, args.packageName, args.userId,
                                                  args.flags, result.ceDataInode,
                                                  result.pccCeDataInode));
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
}

TEST_F(DestroyAppDataTest, DestroySdkSandboxDataDirectories_WithoutDeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.packageName = "com.foo";
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    // Destroy the app user data.
    ASSERT_BINDER_SUCCESS(service->destroyAppData(args.uuid, args.packageName, args.userId,
                                                  FLAG_STORAGE_CE, result.ceDataInode,
                                                  result.pccCeDataInode));
    ASSERT_TRUE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
}

TEST_F(DestroyAppDataTest, DestroySdkSandboxDataDirectories_WithoutCeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.packageName = "com.foo";
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    // Destroy the app user data.
    ASSERT_BINDER_SUCCESS(service->destroyAppData(args.uuid, args.packageName, args.userId,
                                                  FLAG_STORAGE_DE, result.ceDataInode,
                                                  result.pccCeDataInode));
    ASSERT_TRUE(exists("/data/local/tmp/misc_ce/0/sdksandbox/com.foo"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox/com.foo"));
}

class ClearAppDataTest : public SdkSandboxDataTest {
public:
    void createTestSdkData(const std::string& packageName, std::vector<std::string> sdkNames) {
        const auto& cePackagePath = "/data/local/tmp/misc_ce/0/sdksandbox/" + packageName;
        const auto& dePackagePath = "/data/local/tmp/misc_de/0/sdksandbox/" + packageName;
        ASSERT_TRUE(mkdirs(cePackagePath, 0700));
        ASSERT_TRUE(mkdirs(dePackagePath, 0700));
        const std::vector<std::string> packagePaths = {cePackagePath, dePackagePath};
        for (const auto& packagePath : packagePaths) {
            for (auto sdkName : sdkNames) {
                ASSERT_TRUE(mkdirs(packagePath + "/" + sdkName + "/cache", 0700));
                ASSERT_TRUE(mkdirs(packagePath + "/" + sdkName + "/code_cache", 0700));
                std::ofstream{packagePath + "/" + sdkName + "/cache/cachedTestData.txt"};
                std::ofstream{packagePath + "/" + sdkName + "/code_cache/cachedTestData.txt"};
            }
        }
    }
};

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithCeAndClearCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data.
    ASSERT_BINDER_SUCCESS(service->clearAppData(kTestUuid, "com.foo", 0,
                                                FLAG_STORAGE_CE | FLAG_CLEAR_CACHE_ONLY, -1, -1));

    const std::string packagePath = kTestPath + "/misc_ce/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared/cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1/cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2/cache"));
}

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithCeAndClearCodeCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data.
    ASSERT_BINDER_SUCCESS(service->clearAppData(kTestUuid, "com.foo", 0,
                                                FLAG_STORAGE_CE | FLAG_CLEAR_CODE_CACHE_ONLY, -1,
                                                -1));

    const std::string packagePath = kTestPath + "/misc_ce/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared/code_cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1/code_cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2/code_cache"));
}

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithDeAndClearCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data
    ASSERT_BINDER_SUCCESS(
            service->clearAppData(kTestUuid, "com.foo", 0,
                                  FLAG_STORAGE_DE | (InstalldNativeService::FLAG_CLEAR_CACHE_ONLY),
                                  -1, -1));

    const std::string packagePath = kTestPath + "/misc_de/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared/cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1/cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2/cache"));
}

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithDeAndClearCodeCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data.
    ASSERT_BINDER_SUCCESS(service->clearAppData(kTestUuid, "com.foo", 0,
                                                FLAG_STORAGE_DE | FLAG_CLEAR_CODE_CACHE_ONLY, -1,
                                                -1));

    const std::string packagePath = kTestPath + "/misc_de/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared/code_cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1/code_cache"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2/code_cache"));
}

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithCeAndWithoutAnyCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data.
    ASSERT_BINDER_SUCCESS(service->clearAppData(kTestUuid, "com.foo", 0, FLAG_STORAGE_CE, -1, -1));

    const std::string packagePath = kTestPath + "/misc_ce/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2"));
}

TEST_F(ClearAppDataTest, ClearSdkSandboxDataDirectories_WithDeAndWithoutAnyCacheFlag) {
    createTestSdkData("com.foo", {"shared", "sdk1", "sdk2"});
    // Clear the app user data.
    ASSERT_BINDER_SUCCESS(service->clearAppData(kTestUuid, "com.foo", 0, FLAG_STORAGE_DE, -1, -1));

    const std::string packagePath = kTestPath + "/misc_de/0/sdksandbox/com.foo";
    ASSERT_TRUE(is_empty(packagePath + "/shared"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk1"));
    ASSERT_TRUE(is_empty(packagePath + "/sdk2"));
}

class DestroyUserDataTest : public SdkSandboxDataTest {};

TEST_F(DestroyUserDataTest, DestroySdkData_WithCeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.packageName = "com.foo";
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    // Destroy user data
    ASSERT_BINDER_SUCCESS(service->destroyUserData(args.uuid, args.userId, FLAG_STORAGE_CE));
    ASSERT_FALSE(exists("/data/local/tmp/misc_ce/0/sdksandbox"));
    ASSERT_TRUE(exists("/data/local/tmp/misc_de/0/sdksandbox"));
}

TEST_F(DestroyUserDataTest, DestroySdkData_WithDeFlag) {
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args = createAppDataArgs("com.foo");
    args.packageName = "com.foo";
    // Create the app user data.
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    // Destroy user data
    ASSERT_BINDER_SUCCESS(service->destroyUserData(args.uuid, args.userId, FLAG_STORAGE_DE));
    ASSERT_TRUE(exists("/data/local/tmp/misc_ce/0/sdksandbox"));
    ASSERT_FALSE(exists("/data/local/tmp/misc_de/0/sdksandbox"));
}

TEST_F(ServiceTest, CreateAppData_WithPcc) {
    LOG(INFO) << "CreateAppData_WithPcc";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId; // Specify the PCC UID
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    // Define all expected paths
    const std::string cePath = "user/0/com.foo";
    const std::string dePath = "user_de/0/com.foo";
    const std::string pccCePath = "user/0/com.foo-pcc";
    const std::string pccDePath = "user_de/0/com.foo-pcc";

    // Verify all four directories were created
    EXPECT_TRUE(exists(cePath));
    EXPECT_TRUE(exists(dePath));
    EXPECT_TRUE(exists(pccCePath));
    EXPECT_TRUE(exists(pccDePath));

    // Verify correct ownership
    EXPECT_EQ(kTestAppUid, stat_uid(cePath.c_str()));
    EXPECT_EQ(kTestAppUid, stat_uid(dePath.c_str()));
    EXPECT_EQ(kTestPccAppUid, stat_uid(pccCePath.c_str()));
    EXPECT_EQ(kTestPccAppUid, stat_uid(pccDePath.c_str()));

    // Verify cache subdirectories were also created
    EXPECT_TRUE(exists(cePath + "/cache"));
    EXPECT_TRUE(exists(pccCePath + "/cache"));

    // Verify cache GID for PCC
    EXPECT_EQ(kTestPccCacheGid, stat_gid((pccCePath + "/cache").c_str()));
}

TEST_F(ServiceTest, CreateAppData_WithPcc_InodeCheck) {
    LOG(INFO) << "CreateAppData_WithPcc_InodeCheck";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    // Verify PCC directories were created
    const std::string pccCePath = "user/0/com.foo-pcc";
    const std::string pccDePath = "user_de/0/com.foo-pcc";
    EXPECT_TRUE(exists(pccCePath));
    EXPECT_TRUE(exists(pccDePath));

    // Verify that the returned inode values are valid.
    EXPECT_GT(result.pccCeDataInode, 0);
    EXPECT_GT(result.pccDeDataInode, 0);

    // Verify that the returned inode values match the actual inodes on disk.
    struct stat pccCeStat;
    EXPECT_EQ(::stat(get_full_path(pccCePath).c_str(), &pccCeStat), 0);
    EXPECT_EQ(static_cast<int64_t>(pccCeStat.st_ino), result.pccCeDataInode);

    struct stat pccDeStat;
    EXPECT_EQ(::stat(get_full_path(pccDePath).c_str(), &pccDeStat), 0);
    EXPECT_EQ(static_cast<int64_t>(pccDeStat.st_ino), result.pccDeDataInode);
}

TEST_F(ServiceTest, CreateAppData_PccDowngrade) {
    LOG(INFO) << "CreateAppData_PccDowngrade";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId; // Start with PCC support
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    // Phase 1: Install the app with PCC support
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    EXPECT_TRUE(exists("user/0/com.foo-pcc"));
    EXPECT_TRUE(exists("user_de/0/com.foo-pcc"));

    // Phase 2: "Upgrade" the app to a version without PCC support
    args.pccId = -1; // INVALID_UID
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));

    // Verify main directories still exist
    EXPECT_TRUE(exists("user/0/com.foo"));
    EXPECT_TRUE(exists("user_de/0/com.foo"));

    // Verify PCC directories have been cleaned up
    EXPECT_FALSE(exists("user/0/com.foo-pcc"));
    EXPECT_FALSE(exists("user_de/0/com.foo-pcc"));
}

TEST_F(ServiceTest, ClearAppData_WithPcc) {
    LOG(INFO) << "ClearAppData_WithPcc";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    // Setup: Create app data and add dummy files
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    const std::string cePath = "user/0/com.foo";
    const std::string dePath = "user_de/0/com.foo";
    const std::string pccCePath = "user/0/com.foo-pcc";
    const std::string pccDePath = "user_de/0/com.foo-pcc";
    touch(cePath + "/dummy.txt", kTestAppUid, kTestAppUid, 0600);
    touch(dePath + "/dummy.txt", kTestAppUid, kTestAppUid, 0600);
    touch(pccCePath + "/dummy.txt", kTestPccAppUid, kTestPccAppUid, 0600);
    touch(pccDePath + "/dummy.txt", kTestPccAppUid, kTestPccAppUid, 0600);
    ASSERT_FALSE(is_empty(get_full_path(cePath)));
    ASSERT_FALSE(is_empty(get_full_path(pccCePath)));

    // Action: Clear app data
    ASSERT_BINDER_SUCCESS(service->clearAppData(testUuid, "com.foo", kTestUserId,
                                                FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                result.ceDataInode, result.pccCeDataInode));

    // Verification: Directories should still exist but be empty
    EXPECT_TRUE(exists(cePath));
    EXPECT_TRUE(exists(dePath));
    EXPECT_TRUE(exists(pccCePath));
    EXPECT_TRUE(exists(pccDePath));
    EXPECT_TRUE(is_empty(get_full_path(cePath)));
    EXPECT_TRUE(is_empty(get_full_path(dePath)));
    EXPECT_TRUE(is_empty(get_full_path(pccCePath)));
    EXPECT_TRUE(is_empty(get_full_path(pccDePath)));
}

TEST_F(ServiceTest, DestroyAppData_WithPcc) {
    LOG(INFO) << "DestroyAppData_WithPcc";
    android::os::CreateAppDataResult result;
    android::os::CreateAppDataArgs args;
    args.packageName = "com.foo";
    args.uuid = testUuid;
    args.userId = kTestUserId;
    args.appId = kTestAppId;
    args.pccId = kTestPccAppId;
    args.seInfo = "default";
    args.flags = FLAG_STORAGE_CE | FLAG_STORAGE_DE;

    // Setup: Create app data
    ASSERT_BINDER_SUCCESS(service->createAppData(args, &result));
    EXPECT_TRUE(exists("user/0/com.foo"));
    EXPECT_TRUE(exists("user_de/0/com.foo"));
    EXPECT_TRUE(exists("user/0/com.foo-pcc"));
    EXPECT_TRUE(exists("user_de/0/com.foo-pcc"));

    // Action: Destroy app data
    ASSERT_BINDER_SUCCESS(service->destroyAppData(testUuid, "com.foo", kTestUserId,
                                                  FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                  result.ceDataInode, result.pccCeDataInode));

    // Verification: All directories should be gone
    EXPECT_FALSE(exists("user/0/com.foo"));
    EXPECT_FALSE(exists("user_de/0/com.foo"));
    EXPECT_FALSE(exists("user/0/com.foo-pcc"));
    EXPECT_FALSE(exists("user_de/0/com.foo-pcc"));
}

class MoveCompleteAppTest : public ServiceTest {
protected:
    // Identifiers
    std::string fromUuid;
    std::string toUuid;
    std::string packageName;
    int32_t pccId;

    // Base Paths
    std::string sourceBase;
    std::string destBase;

    // Source Paths
    std::string fromCodePath;
    std::string fromCePath;
    std::string fromDePath;
    // Source PCC Paths
    std::string fromPccCePath;
    std::string fromPccDePath;

    // Destination Paths
    std::string toCodePath;
    std::string toCePath;
    std::string toDePath;
    // Destination PCC Paths
    std::string toPccCePath;
    std::string toPccDePath;

    void SetUp() override {
        ServiceTest::SetUp();

        // 1. Initialize Constants
        fromUuid = "TEST"; // Maps to /data/local/tmp
        toUuid = "TEST_2"; // Maps to /data/local/tmp/test_2
        packageName = "com.example.move";
        pccId = kTestPccAppId;

        sourceBase = "/data/local/tmp";
        destBase = "/data/local/tmp/test_2";

        // 2. Initialize Paths
        fromCodePath = sourceBase + "/from_code/com.example.move";
        fromCePath = create_data_user_ce_package_path(fromUuid.c_str(), kTestUserId,
                                                      packageName.c_str());
        fromDePath = create_data_user_de_package_path(fromUuid.c_str(), kTestUserId,
                                                      packageName.c_str());

        // Initialize Source PCC Paths (suffix "-pcc")
        std::string pccPackageName = packageName + "-pcc";
        fromPccCePath = create_data_user_ce_package_path(fromUuid.c_str(), kTestUserId,
                                                         pccPackageName.c_str());
        fromPccDePath = create_data_user_de_package_path(fromUuid.c_str(), kTestUserId,
                                                         pccPackageName.c_str());

        // Destination paths
        std::string toCodePathParent = create_data_app_path(toUuid.c_str());
        toCodePath = toCodePathParent + "/com.example.move";
        toCePath =
                create_data_user_ce_package_path(toUuid.c_str(), kTestUserId, packageName.c_str());
        toDePath =
                create_data_user_de_package_path(toUuid.c_str(), kTestUserId, packageName.c_str());

        // Initialize Destination PCC Paths
        toPccCePath = create_data_user_ce_package_path(toUuid.c_str(), kTestUserId,
                                                       pccPackageName.c_str());
        toPccDePath = create_data_user_de_package_path(toUuid.c_str(), kTestUserId,
                                                       pccPackageName.c_str());

        // 3. Create Volume Skeletons
        ASSERT_TRUE(mkdirs(destBase + "/user/0", 0711));
        ASSERT_TRUE(mkdirs(destBase + "/user_de/0", 0711));
        ASSERT_TRUE(mkdirs(destBase + "/app", 0711));
    }

    void TearDown() override {
        CleanupPaths();
        ServiceTest::TearDown();
    }

    void CleanupPaths() {
        delete_dir_contents_and_dir(fromCodePath, true);
        delete_dir_contents_and_dir(fromCePath, true);
        delete_dir_contents_and_dir(fromDePath, true);
        delete_dir_contents_and_dir(fromPccCePath, true);
        delete_dir_contents_and_dir(fromPccDePath, true);
        delete_dir_contents_and_dir(destBase, true); // Wipes entire Volume 2
    }

    void TouchAbsolute(const std::string& path, uid_t owner, gid_t group, mode_t mode) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT, mode);
        ASSERT_NE(fd, -1) << "Failed to create " << path << ": " << strerror(errno);
        EXPECT_EQ(::fchown(fd, owner, group), 0);
        EXPECT_EQ(::fchmod(fd, mode), 0);
        close(fd);
    }

    bool ExistsAbsolute(const std::string& path) { return ::access(path.c_str(), F_OK) == 0; }
};

TEST_F(MoveCompleteAppTest, Move_Success) {
    // 1. Create Source Data
    ASSERT_TRUE(mkdirs(fromCodePath, 0755));
    TouchAbsolute(fromCodePath + "/base.apk", kSystemUid, kSystemUid, 0644);

    ASSERT_TRUE(mkdirs(fromCePath, 0700));
    TouchAbsolute(fromCePath + "/ce_file.txt", kTestAppUid, kTestAppUid, 0600);

    ASSERT_TRUE(mkdirs(fromDePath, 0700));
    TouchAbsolute(fromDePath + "/de_file.txt", kTestAppUid, kTestAppUid, 0600);

    // 2. Perform Move (No PCC)
    auto status = service->moveCompleteApp(std::make_optional(fromUuid), std::make_optional(toUuid),
                                           packageName, kTestAppId,
                                           0, // pccId = 0 (No PCC)
                                           "default", 30, fromCodePath);

    ASSERT_TRUE(status.isOk()) << "moveCompleteApp failed: " << status.toString8().c_str();

    // 3. Verify Destination
    EXPECT_TRUE(ExistsAbsolute(toCodePath + "/base.apk"));
    EXPECT_TRUE(ExistsAbsolute(toCePath + "/ce_file.txt"));
    EXPECT_TRUE(ExistsAbsolute(toDePath + "/de_file.txt"));
}

TEST_F(MoveCompleteAppTest, Fail_SourceMissing) {
    // We intentionally do NOT create the source files here.
    auto status = service->moveCompleteApp(std::make_optional(fromUuid), std::make_optional(toUuid),
                                           packageName, kTestAppId,
                                           0, // pccId
                                           "default", 30, fromCodePath);

    EXPECT_FALSE(status.isOk());
}

TEST_F(MoveCompleteAppTest, Move_Success_WithPcc) {
    LOG(INFO) << "MoveCompleteAppTest_Move_Success_WithPcc";

    // 1. Create Source Code & App Data
    ASSERT_TRUE(mkdirs(fromCodePath, 0755));
    TouchAbsolute(fromCodePath + "/base.apk", kSystemUid, kSystemUid, 0644);

    ASSERT_TRUE(mkdirs(fromCePath, 0700));
    TouchAbsolute(fromCePath + "/app_ce.txt", kTestAppUid, kTestAppUid, 0600);
    ASSERT_TRUE(mkdirs(fromDePath, 0700));
    TouchAbsolute(fromDePath + "/app_de.txt", kTestAppUid, kTestAppUid, 0600);

    // 2. Create Source PCC Data
    ASSERT_TRUE(mkdirs(fromPccCePath, 0700));
    TouchAbsolute(fromPccCePath + "/pcc_ce.txt", kTestPccAppUid, kTestPccAppUid, 0600);

    ASSERT_TRUE(mkdirs(fromPccDePath, 0700));
    TouchAbsolute(fromPccDePath + "/pcc_de.txt", kTestPccAppUid, kTestPccAppUid, 0600);

    // 3. Perform Move with PCC ID
    auto status = service->moveCompleteApp(std::make_optional(fromUuid), std::make_optional(toUuid),
                                           packageName, kTestAppId,
                                           pccId, // Valid PCC ID
                                           "default", 30, fromCodePath);

    ASSERT_TRUE(status.isOk()) << "moveCompleteApp failed: " << status.toString8().c_str();

    // 4. Verify Destination App Data
    EXPECT_TRUE(ExistsAbsolute(toCodePath + "/base.apk"));
    EXPECT_TRUE(ExistsAbsolute(toCePath + "/app_ce.txt"));
    EXPECT_TRUE(ExistsAbsolute(toDePath + "/app_de.txt"));

    // 5. Verify Destination PCC Data
    EXPECT_TRUE(ExistsAbsolute(toPccCePath + "/pcc_ce.txt"));
    EXPECT_TRUE(ExistsAbsolute(toPccDePath + "/pcc_de.txt"));

    // 6. Verify Ownership of PCC Data (Should be owned by PCC UID)
    struct stat st;
    ASSERT_EQ(0, ::stat((toPccCePath + "/pcc_ce.txt").c_str(), &st));
    EXPECT_EQ(kTestPccAppUid, st.st_uid);
}

TEST_F(MoveCompleteAppTest, Move_Fail_RollsBackPreCreatedPcc) {
    // 1. Setup: Pre-create the Destination PCC directory manually.
    // Hacky but this allows us to verify that the cleanup/rollback logic actually runs.
    // If the rollback logic fails to run, this directory will remain.
    ASSERT_TRUE(mkdirs(toPccCePath, 0700));
    TouchAbsolute(toPccCePath + "/garbage.txt", kTestPccAppUid, kTestPccAppUid, 0600);

    // 2. Setup: Ensure Source APK is MISSING.
    // This guarantees that moveCompleteApp fails at the very first step (copying code),
    // forcing a jump to the 'fail' label immediately.
    delete_dir_contents_and_dir(fromCodePath, true);

    // 3. Perform Move
    auto status =
            service->moveCompleteApp(std::make_optional(fromUuid), std::make_optional(toUuid),
                                     packageName, kTestAppId, pccId, "default", 30, fromCodePath);

    // 4. Expect Failure (because APK was missing)
    EXPECT_FALSE(status.isOk());

    // 5. Verify Rollback
    // The directory we manually created must be gone.
    EXPECT_FALSE(ExistsAbsolute(toPccCePath));
}

class DestroyPccDirectoriesTest : public ServiceTest {
protected:
    std::string ce_pcc_path;
    std::string de_pcc_path;
    std::string ce_app_path;
    std::string de_app_path;

    void SetUp() override {
        ServiceTest::SetUp();
        ce_pcc_path = create_data_user_ce_package_path("TEST", 0, "com.foo-pcc");
        de_pcc_path = create_data_user_de_package_path("TEST", 0, "com.foo-pcc");
        ce_app_path = create_data_user_ce_package_path("TEST", 0, "com.foo");
        de_app_path = create_data_user_de_package_path("TEST", 0, "com.foo");
        CreatePccDirectories();
    }

    void TearDown() override {
        delete_dir_contents_and_dir(ce_pcc_path, true);
        delete_dir_contents_and_dir(de_pcc_path, true);
        delete_dir_contents_and_dir(ce_app_path, true);
        delete_dir_contents_and_dir(de_app_path, true);
        ServiceTest::TearDown();
    }

    void CreatePccDirectories() {
        ASSERT_TRUE(mkdirs(ce_pcc_path, 0700));
        ASSERT_TRUE(mkdirs(de_pcc_path, 0700));

        ASSERT_TRUE(android::base::WriteStringToFile("content", ce_pcc_path + "/file.txt", 0600,
                                                     kTestPccAppUid, kTestPccAppUid, false));
        ASSERT_TRUE(android::base::WriteStringToFile("content", de_pcc_path + "/file.txt", 0600,
                                                     kTestPccAppUid, kTestPccAppUid, false));
    }

    bool PathExists(const std::string& path) { return ::access(path.c_str(), F_OK) == 0; }
};

TEST_F(DestroyPccDirectoriesTest, DestroyPccDirectories_Success_CeAndDe) {
    LOG(INFO) << "DestroyPccDirectories_Success_CeAndDe";

    EXPECT_TRUE(PathExists(ce_pcc_path));
    EXPECT_TRUE(PathExists(de_pcc_path));

    ASSERT_BINDER_SUCCESS(service->destroyPccData(std::make_optional<std::string>("TEST"),
                                                  "com.foo", 0, FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                  0));

    EXPECT_FALSE(PathExists(ce_pcc_path));
    EXPECT_FALSE(PathExists(de_pcc_path));

    EXPECT_FALSE(exists_renamed_deleted_dir("/user/0"));
    EXPECT_FALSE(exists_renamed_deleted_dir("/user_de/0"));
}

TEST_F(DestroyPccDirectoriesTest, DestroyPccDirectories_Success_CeOnly) {
    LOG(INFO) << "DestroyPccDirectories_Success_CeOnly";

    ASSERT_BINDER_SUCCESS(service->destroyPccData(std::make_optional<std::string>("TEST"),
                                                  "com.foo", 0, FLAG_STORAGE_CE, 0));

    EXPECT_FALSE(PathExists(ce_pcc_path)); // CE gone
    EXPECT_TRUE(PathExists(de_pcc_path));  // DE remains
}

TEST_F(DestroyPccDirectoriesTest, DestroyPccDirectories_Success_DeOnly) {
    LOG(INFO) << "DestroyPccDirectories_Success_DeOnly";

    ASSERT_BINDER_SUCCESS(service->destroyPccData(std::make_optional<std::string>("TEST"),
                                                  "com.foo", 0, FLAG_STORAGE_DE, 0));

    EXPECT_TRUE(PathExists(ce_pcc_path));  // CE remains
    EXPECT_FALSE(PathExists(de_pcc_path)); // DE gone
}

TEST_F(DestroyPccDirectoriesTest, DestroyPccDirectories_DirectoriesDoNotExist) {
    LOG(INFO) << "DestroyPccDirectories_DirectoriesDoNotExist";

    ASSERT_BINDER_SUCCESS(service->destroyPccData(std::make_optional<std::string>("TEST"),
                                                  "com.foo", 0, FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                  0));
}

TEST_F(DestroyPccDirectoriesTest, DestroyPccDirectories_DoesNotAffectAppData) {
    LOG(INFO) << "DestroyPccDirectories_DoesNotAffectAppData";

    ASSERT_TRUE(mkdirs(ce_app_path, 0700));
    ASSERT_TRUE(mkdirs(de_app_path, 0700));

    ASSERT_BINDER_SUCCESS(service->destroyPccData(std::make_optional<std::string>("TEST"),
                                                  "com.foo", 0, FLAG_STORAGE_CE | FLAG_STORAGE_DE,
                                                  0));

    EXPECT_FALSE(PathExists(ce_pcc_path));
    EXPECT_FALSE(PathExists(de_pcc_path));
    EXPECT_TRUE(PathExists(ce_app_path));
    EXPECT_TRUE(PathExists(de_app_path));
}

class MockAppDataOperationCallback : public android::os::IInstalld::BnAppDataOperationCallback {
public:
    std::promise<void> mPromise;
    int mStatus = -1;
    std::string mMessage;
    std::vector<std::optional<std::string>> mFailedFiles;

    binder::Status onStatusChanged(
            int status, const std::optional<std::string>& message,
            const std::optional<std::vector<std::optional<std::string>>>& failedFiles) override {
        if (failedFiles) mFailedFiles = *failedFiles;
        if ((status == STATUS_SUCCESS || status == STATUS_FAILURE) && mStatus == -1) {
            mStatus = status;
            if (message) mMessage = *message;
            mPromise.set_value();
        }
        return binder::Status::ok();
    }
    void waitForCompletion() { mPromise.get_future().wait(); }
};

TEST_F(ServiceTest, CopyAppDataPath_Fail_Symlink) {
    const std::string fromPath = "user/0/from_copy";
    const std::string toPath = "user/0/to_copy";
    const std::string targetPath = "/data/local/tmp/symlink_target.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);
    unlink(targetPath.c_str());

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);

    // Create the target file and write some data to it
    {
        std::ofstream ofs(targetPath);
        ofs << "important data";
    }
    chmod(targetPath.c_str(), 0600);
    struct stat st_before;
    ASSERT_EQ(0, stat(targetPath.c_str(), &st_before));

    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);
    mkdir(toPath + "/from_copy", kTestAppUid, kTestAppUid, 0700);
    std::string symlinkPath = get_full_path(toPath) + "/from_copy/file.txt";
    ASSERT_EQ(0, symlink(targetPath.c_str(), symlinkPath.c_str()));

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();

    // Verify target file was not truncated
    struct stat st_after;
    ASSERT_EQ(0, stat(targetPath.c_str(), &st_after));
    EXPECT_EQ(st_before.st_size, st_after.st_size);

    // Verify it failed for that file
    bool found = false;
    for (const auto& f : callback->mFailedFiles) {
        if (f && *f == get_full_path(fromPath) + "/file.txt") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);

    unlink(targetPath.c_str());
}

TEST_F(ServiceTest, CopyAppDataPath_Fail_InvalidPath) {
    const std::string fromPath = "/system/bin/sh";
    const std::string toPath = "to_copy_invalid";

    auto callback = sp<MockAppDataOperationCallback>::make();

    // The call itself returns ok() (oneway), but callback receives failure.
    ASSERT_TRUE(service->copyAppDataPath(testUuid, fromPath, get_full_path(toPath), kTestUserId,
                                         kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
}

TEST_F(ServiceTest, CopyAppDataPath_AcceptNonLegacyPathForUser0_Internal) {
    auto callback = sp<MockAppDataOperationCallback>::make();
    // Use std::nullopt for uuid to test internal storage validation
    std::string fromPath = "/data/user/0/from_copy_internal";
    std::string toPath = "/data/user/0/to_copy_internal";

    ASSERT_TRUE(service->copyAppDataPath(std::nullopt, fromPath, toPath, kTestUserId, kTestAppId,
                                         "default", 0, multiuser_get_uid(kTestUserId, kTestAppId),
                                         callback)
                        .isOk());

    callback->waitForCompletion();

    // If validation passed, it would try to copy. Since /data/user/0/from_copy_internal doesn't
    // exist, copy_app_data will notify STATUS_FAILURE with "App data root does not exist: ...".
    // If validation failed, it would be "Path ... is not valid for user 0"
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_EQ(callback->mMessage, "App data root does not exist: /data/user/0/from_copy_internal");
}

TEST_F(ServiceTest, CopyAppDataPath_Success) {
    const std::string fromPath = "user/0/from_copy";
    const std::string toPath = "user/0/to_copy";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_copy"));
    EXPECT_TRUE(exists(toPath + "/from_copy/file.txt"));
    EXPECT_TRUE(exists(fromPath)); // Source should still exist
}

TEST_F(ServiceTest, CopyAppDataPath_TargetExists) {
    const std::string fromPath = "user/0/from_copy";
    const std::string toPath = "user/0/to_copy";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);

    // Pre-create target dir and a file that should be overwritten
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);
    mkdir(toPath + "/from_copy", kTestAppUid, kTestAppUid, 0700);
    touch(toPath + "/from_copy/file.txt", kTestAppUid, kTestAppUid, 0600);
    touch(toPath + "/from_copy/other.txt", kTestAppUid, kTestAppUid, 0600);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_copy"));
    EXPECT_TRUE(exists(toPath + "/from_copy/file.txt"));
    EXPECT_TRUE(exists(toPath + "/from_copy/other.txt")); // Should be preserved
    EXPECT_TRUE(exists(fromPath));
}

TEST_F(ServiceTest, CopyAppDataPath_Fail_NonNormalizedPath) {
    auto callback = sp<MockAppDataOperationCallback>::make();
    std::string fromPath = "/data/user/0/../system";
    std::string toPath = "/data/user/0/to_copy";

    // '..' is rejected immediately by Binder interface validation (shady path)
    ASSERT_FALSE(service->copyAppDataPath(std::nullopt, fromPath, toPath, kTestUserId, kTestAppId,
                                          "default", 0, multiuser_get_uid(kTestUserId, kTestAppId),
                                          callback)
                         .isOk());
}

TEST_F(ServiceTest, CopyAppDataPath_Fail_RedundantPath) {
    auto callback = sp<MockAppDataOperationCallback>::make();
    std::string fromPath = "/data/user/0//from_copy";
    std::string toPath = "/data/user/0/to_copy";

    // '//' passes Binder validation but should be rejected by internal normalization check
    ASSERT_TRUE(service->copyAppDataPath(std::nullopt, fromPath, toPath, kTestUserId, kTestAppId,
                                         "default", 0, multiuser_get_uid(kTestUserId, kTestAppId),
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
}

TEST_F(ServiceTest, MoveAppDataPath_Fail_NonNormalizedPath) {
    auto callback = sp<MockAppDataOperationCallback>::make();
    std::string fromPath = "/data/user/0/from_move";
    std::string toPath = "/data/user/0/./to_move";

    // '/./' passes Binder validation but should be rejected by internal normalization check
    ASSERT_TRUE(service->moveAppDataPath(std::nullopt, fromPath, toPath, kTestUserId, kTestAppId,
                                         "default", 0, multiuser_get_uid(kTestUserId, kTestAppId),
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
}

class MockAppDataOperationProgressCallback
      : public android::os::IInstalld::BnAppDataOperationCallback {
public:
    std::promise<void> mPromise;
    int mStatus = -1;
    std::vector<std::pair<int64_t, int64_t>> mProgress; // <filesProcessed, bytesProcessed>

    binder::Status onStatusChanged(
            int status, const std::optional<std::string>& message,
            const std::optional<std::vector<std::optional<std::string>>>& failedFiles) override {
        (void)message;
        (void)failedFiles;
        if (status == STATUS_SUCCESS || status == STATUS_FAILURE) {
            mStatus = status;
            mPromise.set_value();
        }
        return binder::Status::ok();
    }

    void waitForCompletion() { mPromise.get_future().wait(); }
};

TEST_F(ServiceTest, MoveAppDataPath_Success) {
    const std::string fromPath = "user/0/from_move";
    const std::string toPath = "user/0/to_move";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_move"));
    EXPECT_TRUE(exists(toPath + "/from_move/file.txt"));
    EXPECT_FALSE(exists(fromPath)); // Source should be gone
}

TEST_F(ServiceTest, MoveAppDataPath_TargetExists_EmptyDir) {
    const std::string fromPath = "user/0/from_move";
    const std::string toPath = "user/0/to_move";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);

    // Pre-create empty target dir
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);
    mkdir(toPath + "/from_move", kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_move"));
    EXPECT_TRUE(exists(toPath + "/from_move/file.txt"));
    EXPECT_FALSE(exists(fromPath));
}

TEST_F(ServiceTest, MoveAppDataPath_TargetExists_NotEmptyDir) {
    const std::string fromPath = "user/0/from_move";
    const std::string toPath = "user/0/to_move";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);

    // Pre-create non-empty target dir
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);
    mkdir(toPath + "/from_move", kTestAppUid, kTestAppUid, 0700);
    touch(toPath + "/from_move/existing.txt", kTestAppUid, kTestAppUid, 0600);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    // renameat() fails with ENOTEMPTY if target is a non-empty directory
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_TRUE(exists(fromPath)); // Source should still exist
}

TEST_F(ServiceTest, MoveAppDataPath_Fail_PermissionDenied) {
    const std::string fromPath = "user/0/from_permission_denied";
    const std::string toPath = "user/0/to_permission_denied";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source file owned by different app
    mkdir(fromPath, kTestAppUid + 1, kTestAppUid + 1, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid + 1, kTestAppUid + 1, 0600, "content");

    // Create dest dir owned by target app
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move file, but callerUid is kTestAppUid, while source is kTestAppUid + 1
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0, kTestAppUid,
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_THAT(callback->mMessage, testing::HasSubstr("PERMISSION_DENIED"));
    EXPECT_TRUE(exists(srcFile));
}

TEST_F(ServiceTest, CopyAppDataPath_FileToDir) {
    const std::string fromPath = "user/0/from_copy_file_dir";
    const std::string toPath = "user/0/to_copy_file_dir";
    const std::string toFile = toPath + "/file.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Create dest dir
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Copy file to directory
    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toFile));
    EXPECT_TRUE(exists(srcFile));
    EXPECT_EQ(kTestAppUid, stat_uid(toFile.c_str()));
}

TEST_F(ServiceTest, MoveAppDataPath_FileToDir) {
    const std::string fromPath = "user/0/from_move_file_dir";
    const std::string toPath = "user/0/to_move_file_dir";
    const std::string toFile = toPath + "/file.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Create dest dir
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move file to directory
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toFile));
    EXPECT_FALSE(exists(srcFile));
    EXPECT_EQ(kTestAppUid, stat_uid(toFile.c_str()));
}

TEST_F(ServiceTest, CopyAppDataPath_SubdirCreated) {
    const std::string fromPath = "user/0/from_copy_subdir";
    const std::string toRoot = "user/0/to_copy_root";
    const std::string toPath = toRoot + "/new_subdir";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toRoot), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Create target root (the package directory) but NOT the subdir
    mkdir(toRoot, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Copy file to non-existent subdir within existing root
    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath));
    EXPECT_TRUE(exists(toPath + "/file.txt"));
    EXPECT_EQ(kTestAppUid, stat_uid(toPath.c_str()));
}

TEST_F(ServiceTest, CopyAppDataPath_Fail_NoTargetRoot) {
    const std::string fromPath = "user/0/from_copy_no_root";
    const std::string toPath = "user/0/to_copy_no_root";
    const std::string toFile = toPath + "/file.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Note: we do NOT create toPath here.

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Copy file to non-existent root
    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_FALSE(exists(toPath));
    EXPECT_FALSE(exists(toFile));
}

TEST_F(ServiceTest, MoveAppDataPath_SubdirCreated) {
    const std::string fromPath = "user/0/from_move_subdir";
    const std::string toRoot = "user/0/to_move_root";
    const std::string toPath = toRoot + "/new_subdir";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toRoot), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Create target root but NOT the subdir
    mkdir(toRoot, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move file to non-existent subdir within existing root
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath));
    EXPECT_TRUE(exists(toPath + "/file.txt"));
    EXPECT_FALSE(exists(srcFile));
    EXPECT_EQ(kTestAppUid, stat_uid(toPath.c_str()));
}

TEST_F(ServiceTest, MoveAppDataPath_Fail_NoTargetRoot) {
    const std::string fromPath = "user/0/from_move_no_root";
    const std::string toPath = "user/0/to_move_no_root";
    const std::string toFile = toPath + "/file.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    const std::string srcFile = fromPath + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid, kTestAppUid, 0600, "content");

    // Note: we do NOT create toPath here.

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move file to non-existent root
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(srcFile), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_FALSE(exists(toPath));
    EXPECT_FALSE(exists(toFile));
    EXPECT_TRUE(exists(srcFile)); // Source should still exist
}

TEST_F(ServiceTest, MoveAppDataPath_DirToDir) {
    const std::string fromPath = "user/0/from_move_dir_dir";
    const std::string toPath = "user/0/to_move_dir_dir";
    const std::string nestedDir = toPath + "/from_move_dir_dir";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source dir with a file
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    touch(fromPath + "/file.txt", kTestAppUid, kTestAppUid, 0600);

    // Note: for 'Move INTO' semantics, we can pre-create toPath
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move directory into directory
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0,
                                         multiuser_get_uid(kTestUserId, kTestAppId), callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(nestedDir));
    EXPECT_TRUE(exists(nestedDir + "/file.txt"));
    EXPECT_FALSE(exists(fromPath));
    EXPECT_EQ(kTestAppUid, stat_uid(nestedDir.c_str()));
}

TEST_F(ServiceTest, CopyAppDataPath_Success_SecondaryUser) {
    const std::string fromPath = StringPrintf("user/%d/from_copy", kSecondaryUserId);
    const std::string toPath = StringPrintf("user/%d/to_copy", kSecondaryUserId);

    system(StringPrintf("mkdir -p %s",
                        get_full_path(StringPrintf("user/%d", kSecondaryUserId)).c_str())
                   .c_str());

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kSecondaryAppUid, kSecondaryAppUid, 0700);
    touch(fromPath + "/file.txt", kSecondaryAppUid, kSecondaryAppUid, 0600);
    mkdir(toPath, kSecondaryAppUid, kSecondaryAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kSecondaryUserId, kTestAppId, "default", 0,
                                         kSecondaryAppUid, callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_copy"));
    EXPECT_TRUE(exists(toPath + "/from_copy/file.txt"));
    EXPECT_EQ(kSecondaryAppUid, stat_uid((toPath + "/from_copy/file.txt").c_str()));
    EXPECT_TRUE(exists(fromPath));
}

TEST_F(ServiceTest, MoveAppDataPath_Success_SecondaryUser) {
    const std::string fromPath = StringPrintf("user/%d/from_move", kSecondaryUserId);
    const std::string toPath = StringPrintf("user/%d/to_move", kSecondaryUserId);

    system(StringPrintf("mkdir -p %s",
                        get_full_path(StringPrintf("user/%d", kSecondaryUserId)).c_str())
                   .c_str());

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    mkdir(fromPath, kSecondaryAppUid, kSecondaryAppUid, 0700);
    touch(fromPath + "/file.txt", kSecondaryAppUid, kSecondaryAppUid, 0600);
    mkdir(toPath, kSecondaryAppUid, kSecondaryAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kSecondaryUserId, kTestAppId, "default", 0,
                                         kSecondaryAppUid, callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_SUCCESS);
    EXPECT_TRUE(exists(toPath + "/from_move"));
    EXPECT_TRUE(exists(toPath + "/from_move/file.txt"));
    EXPECT_EQ(kSecondaryAppUid, stat_uid((toPath + "/from_move/file.txt").c_str()));
    EXPECT_FALSE(exists(fromPath));
}

TEST_F(ServiceTest, MoveAppDataPath_Fail_PermissionDenied_Recursive) {
    const std::string fromPath = "user/0/from_permission_denied_rec";
    const std::string toPath = "user/0/to_permission_denied_rec";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source dir owned by target app
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    // Create source SUBDIR owned by target app
    const std::string subDir = fromPath + "/subdir";
    mkdir(subDir, kTestAppUid, kTestAppUid, 0700);
    // Create source file INSIDE subdir owned by DIFFERENT app
    const std::string srcFile = subDir + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid + 1, kTestAppUid + 1, 0600, "content");

    // Create dest dir owned by target app
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move dir, but one nested file belongs to kTestAppUid + 1
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0, kTestAppUid,
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_THAT(callback->mMessage, testing::HasSubstr("PERMISSION_DENIED"));
    EXPECT_TRUE(exists(srcFile));
}

TEST_F(ServiceTest, CopyAppDataPath_Fail_PermissionDenied_Recursive) {
    const std::string fromPath = "user/0/from_copy_permission_denied_rec";
    const std::string toPath = "user/0/to_copy_permission_denied_rec";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);

    // Create source dir owned by target app
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);
    // Create source SUBDIR owned by target app
    const std::string subDir = fromPath + "/subdir";
    mkdir(subDir, kTestAppUid, kTestAppUid, 0700);
    // Create source file INSIDE subdir owned by DIFFERENT app
    const std::string srcFile = subDir + "/file.txt";
    create_with_content(get_full_path(srcFile), kTestAppUid + 1, kTestAppUid + 1, 0600, "content");

    // Create dest dir owned by target app
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Copy dir, but one nested file belongs to kTestAppUid + 1
    ASSERT_TRUE(service->copyAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0, kTestAppUid,
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_THAT(callback->mMessage, testing::HasSubstr("PERMISSION_DENIED"));
    EXPECT_TRUE(exists(srcFile));
}

TEST_F(ServiceTest, MoveAppDataPath_Fail_PermissionDenied_SymlinkOwner) {
    const std::string fromPath = "user/0/from_permission_denied_symlink";
    const std::string toPath = "user/0/to_permission_denied_symlink";
    const std::string targetPath = "/data/local/tmp/symlink_target.txt";

    delete_dir_contents_and_dir(get_full_path(fromPath), true);
    delete_dir_contents_and_dir(get_full_path(toPath), true);
    unlink(targetPath.c_str());

    // Create a target file
    create_with_content(targetPath, kTestAppUid, kTestAppUid, 0600, "target");

    // Create source dir owned by target app
    mkdir(fromPath, kTestAppUid, kTestAppUid, 0700);

    // Create a symlink owned by DIFFERENT app
    std::string symlinkPath = get_full_path(fromPath) + "/mysymlink";
    ASSERT_EQ(0, symlink(targetPath.c_str(), symlinkPath.c_str()));
    ASSERT_EQ(0, lchown(symlinkPath.c_str(), kTestAppUid + 1, kTestAppUid + 1));

    // Create dest dir owned by target app
    mkdir(toPath, kTestAppUid, kTestAppUid, 0700);

    auto callback = sp<MockAppDataOperationCallback>::make();

    // Move dir, but nested symlink belongs to kTestAppUid + 1
    ASSERT_TRUE(service->moveAppDataPath(testUuid, get_full_path(fromPath), get_full_path(toPath),
                                         kTestUserId, kTestAppId, "default", 0, kTestAppUid,
                                         callback)
                        .isOk());

    callback->waitForCompletion();
    EXPECT_EQ(callback->mStatus, IAppDataOperationCallback::STATUS_FAILURE);
    EXPECT_THAT(callback->mMessage, testing::HasSubstr("PERMISSION_DENIED"));

    struct stat st;
    EXPECT_EQ(0, lstat(symlinkPath.c_str(), &st));
    EXPECT_TRUE(S_ISLNK(st.st_mode));

    unlink(targetPath.c_str());
}

}  // namespace installd
}  // namespace android
