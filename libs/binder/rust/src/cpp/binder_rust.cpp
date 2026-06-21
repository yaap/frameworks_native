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

#include <android/binder_api_level_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_stability.h>

#include <string>

extern "C" {

void Compat_AIBinder_Class_setTransactionCodeToFunctionNameMap(AIBinder_Class* clazz,
                                                               const char* const* map,
                                                               size_t size) {
#if defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 36
    if (API_LEVEL_AT_LEAST(36)) {
        AIBinder_Class_setTransactionCodeToFunctionNameMap(clazz, map, size);
    }
#else
    (void)clazz;
    (void)map;
    (void)size;
#endif // defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 36
}

const char* Compat_AIBinder_Class_getFunctionName(const AIBinder_Class* clazz,
                                                  uint32_t transactionCode) {
#if defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 36
    if (API_LEVEL_AT_LEAST(36)) {
        return AIBinder_Class_getFunctionName(clazz, transactionCode);
    }
#else
    (void)clazz;
    (void)transactionCode;
#endif // defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 36
    return {};
}

bool Compat_AIBinder_requiresVintfDeclaration(const AIBinder* binder) {
#if defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    if (API_LEVEL_AT_LEAST(37)) {
        return AIBinder_requiresVintfDeclaration(const_cast<AIBinder*>(binder));
    }
#else
    (void)binder;
#endif // defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    return false;
}

bool Compat_AIBinder_isVendorStable(const AIBinder* binder) {
#if defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    if (API_LEVEL_AT_LEAST(37)) {
        return AIBinder_isVendorStable(const_cast<AIBinder*>(binder));
    }
#else
    (void)binder;
#endif // defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    return false;
}

bool Compat_AIBinder_isSystemStable(const AIBinder* binder) {
#if defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    if (API_LEVEL_AT_LEAST(37)) {
        return AIBinder_isSystemStable(const_cast<AIBinder*>(binder));
    }
#else
    (void)binder;
#endif // defined(__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__) || __ANDROID_API__ >= 37
    return false;
}

} // extern "C"