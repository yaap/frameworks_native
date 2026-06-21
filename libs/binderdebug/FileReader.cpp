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
#include <binderdebug/FileReader.h>

namespace android {
namespace binderdebug {

bool FileReader::Open(const std::string& path) {
    if (ifs_.is_open()) {
        ifs_.close();
    }
    ifs_.open(path);
    return ifs_.is_open();
}

bool FileReader::IsOpen() const {
    return ifs_.is_open();
}

bool FileReader::GetLine(std::string& line) {
    return static_cast<bool>(std::getline(ifs_, line));
}

void FileReader::Close() {
    ifs_.close();
}

} // namespace binderdebug
} // namespace android
