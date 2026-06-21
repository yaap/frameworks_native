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

#include <procpartition/apexcache.h>

#include <android-base/logging.h>
#include <android/apex/IApexService.h>
#include <binder/IServiceManager.h>

namespace android {
namespace apexcache {

ApexCache* ApexCache::getInstance() {
    static ApexCache *instance = new ApexCache();
    return instance;
}

ApexCache::ApexCache() {}

const std::vector<apex::ApexInfo>& ApexCache::getCache(bool invalidate) {
    if (invalidate) {
        cache.clear();
    }
    if (cache.empty()) {
        sp<IServiceManager> sm = android::defaultServiceManager();
        sp<IBinder> binder = sm->waitForService(String16("apexservice"));
        sp<apex::IApexService> service =
            android::interface_cast<apex::IApexService>(binder);
        android::binder::Status status = service->getActivePackages(&cache);
        if (!status.isOk()) {
            LOG(ERROR) << "Failed to get active packages: "
                       << status.exceptionMessage().c_str();
        }
    }
    return cache;
}

} // namespace apexcache
} // namespace android
