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

#include <gtest/gtest.h>

#include <stddef.h>
#include <stdint.h>

#include "adbd_auth.h"

void start_wifi() {}
void stop_wifi() {}

class AdbAuthTest: public ::testing::Test {
   public:
    void SetUp() {
        AdbdAuthCallbacksV2 callbacks;
        callbacks.version = 2;
        callbacks.start_adbd_wifi = start_wifi;
        callbacks.stop_adbd_wifi = stop_wifi;
        context = adbd_auth_new(&callbacks);
    }

    void TearDown() {
      adbd_auth_delete(context);
    }

 protected:
  AdbdAuthContext* context;
};

TEST_F(AdbAuthTest, SendTcpPort) {
  adbd_auth_send_tls_server_port(context, 1);
}
