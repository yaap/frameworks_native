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

#![no_std]

//! Wrapper functions for NDK APIs with API level checks.

use core::ffi::c_char;

use binder_ndk_compat_bindgen::{
    Compat_AIBinder_Class_getFunctionName,
    Compat_AIBinder_Class_setTransactionCodeToFunctionNameMap, Compat_AIBinder_isSystemStable,
    Compat_AIBinder_isVendorStable, Compat_AIBinder_requiresVintfDeclaration,
};
use binder_ndk_sys::{AIBinder, AIBinder_Class};

/// Sets the transaction code to function name map for a binder class.
///
/// # Safety
///
/// The caller must guarantee that the `clazz` pointer is a valid, non-null
/// pointer to an `AIBinder_Class`. The `map` pointer must be a valid pointer to
/// an array of `size` C-style strings, where each string has a static lifetime.
pub unsafe fn set_transaction_code_to_function_name_map(
    clazz: *mut AIBinder_Class,
    map: *const *const c_char,
    size: usize,
) {
    // SAFETY: The caller guarantees that the pointers in `function_names`
    // have a 'static lifetime. The `class` pointer is valid and non-null.
    unsafe {
        Compat_AIBinder_Class_setTransactionCodeToFunctionNameMap(clazz, map, size);
    }
}

/// Get function name for give transaction code for AIBinder_Class
///
/// # Safety
///
/// The caller must guarantee that the `clazz` pointer is a valid, non-null
/// pointer to an `AIBinder_Class` and transaction code should be valid.
pub unsafe fn get_function_name(clazz: *const AIBinder_Class, code: u32) -> *const c_char {
    // SAFETY: The caller guarantees that the clazz and transaction code are valid
    unsafe { Compat_AIBinder_Class_getFunctionName(clazz, code) }
}

/// Returns true if the binder needs to be declared in the VINTF manifest.
///
/// # Safety
///
/// The caller must guarantee that the `binder` pointer is valid.
pub unsafe fn requires_vintf_declaration(binder: *const AIBinder) -> bool {
    // SAFETY: The caller guarantees that the binder pointer is valid.
    unsafe { Compat_AIBinder_requiresVintfDeclaration(binder) }
}

/// Returns true if the binder is vendor stable.
///
/// # Safety
///
/// The caller must guarantee that the `binder` pointer is valid.
pub unsafe fn is_vendor_stable(binder: *const AIBinder) -> bool {
    // SAFETY: The caller guarantees that the binder pointer is valid.
    unsafe { Compat_AIBinder_isVendorStable(binder) }
}

/// Returns true if the binder is system server only.
///
/// # Safety
///
/// The caller must guarantee that the `binder` pointer is valid.
pub unsafe fn is_system_stable(binder: *const AIBinder) -> bool {
    // SAFETY: The caller guarantees that the binder pointer is valid.
    unsafe { Compat_AIBinder_isSystemStable(binder) }
}
