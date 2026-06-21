/*
 * Copyright (C) 2026 The Android Open Source Project
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

//! Native HID Service skeleton.

use android_hardware_hid::aidl::android::hardware::hid::IHidManager::BnHidManager;
use android_hardware_input_flags::hid_api;
use binder::BinderFeatures;
use hidservice::hid_manager::HidManager;

use anyhow::{bail, Result};

#[tokio::main]
async fn main() -> Result<()> {
    logger::init(
        logger::Config::default()
            .with_tag_on_device("hidservice")
            .with_max_level(log::LevelFilter::Debug),
    );

    log::info!("Starting Native HID Service");

    if !hid_api() {
        log::debug!("HID support not enabled on this device. Skipping native HID service.");
        bail!("Native HID service is disabled");
    }

    binder::register_lazy_service(
        "hid",
        BnHidManager::new_binder(HidManager {}, BinderFeatures::default()).as_binder(),
    )?;

    tokio::task::block_in_place(binder::ProcessState::join_thread_pool);
    Ok(())
}
