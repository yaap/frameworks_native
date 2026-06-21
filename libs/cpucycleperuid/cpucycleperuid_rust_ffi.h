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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

bool rust_is_rapl_available();
bool rust_is_bpf_program_present();
bool rust_start_tracking();
void rust_stop_tracking();
bool rust_is_tracking();

// Returns the current package power in microjoules, or -1 on error.
int64_t rust_read_package_power();

// Returns the last recorded TSC value, or -1 on error.
int64_t rust_read_last_recorded_cycle();

// Returns the accumulated desync count, or -1 on error.
int64_t rust_read_desync_count();

// Writes UID/value pairs into the provided buffer.
// Returns the number of elements (uint64_t) written.
// Returns 0 if an error occurs or buffer is null/too small.
size_t rust_read_uid_cpu_cycles(uint64_t *buffer, size_t buffer_len);

// Writes UID/value pairs into the provided buffer.
// Returns the number of elements (uint64_t) written.
// Returns 0 if an error occurs or buffer is null/too small.
size_t rust_read_uid_power_delta(uint64_t *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
