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

#include <string>

#include <SkM44.h>
#include <SkRect.h>

struct SkRect;
class SkM44;
namespace android {
struct ShmemPaint;
struct IPCRenderBufferOp;
std::string rectToString(const SkRect& rect);
std::string skmatrixToString(const SkM44& matrix);
std::string shmemPaintToString(const ShmemPaint& paint);
std::string opToString(IPCRenderBufferOp* op);
std::string opTypeToString(uint32_t type);
} // namespace android
