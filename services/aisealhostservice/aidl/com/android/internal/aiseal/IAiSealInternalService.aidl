/*
 * Copyright 2025 The Android Open Source Project
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
package com.android.internal.aiseal;

/**
 * An internal service system_server can communicate with.
 */
interface IAiSealInternalService {

    /**
     * Called when CE storage of the given {@code userId} is unlocked.
     * At this point a CE storage of this user in the aiseal VM can also be unlocked.
     */
    void onUserUnlocking(int userId, String kekFile);

    /**
     * Called when given {@code userId} is stopped.
     * At this point the CE storage of this user in the aiseal VM should be locked.
     */
    void onUserStopped(int userId);

    /**
     * Called when given {@code userId} is removed
     * At this point the CE storage of this user in the aiseal VM should be removed.
     */
    void onUserRemoved(int userId);
}
