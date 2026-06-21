// Copyright 2025, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Manages VM storage directory and image files.

use crate::config::AiSealConfig;
use android_system_virtualizationservice::aidl::android::system::virtualizationservice::{
    IVirtualizationService::IVirtualizationService, PartitionType::PartitionType,
};
use anyhow::{anyhow, Context, Result};
use binder::ParcelFileDescriptor;
use log::warn;
use std::{fs, path::Path};

const AISEAL_DEVICE_ENCRYPTED_DIR: &str = "/data/system/aiseal";
const AISEAL_INSTANCE_IMAGE_SIZE: i64 = 10 * 1024 * 1024;

fn get_instance_id_path() -> String {
    format!("{AISEAL_DEVICE_ENCRYPTED_DIR}/instance_id")
}

fn get_vm_dir() -> String {
    // TODO: generate based on instance ID
    format!("{AISEAL_DEVICE_ENCRYPTED_DIR}/vm")
}

fn get_instance_image_path(vm_dir: &str) -> String {
    format!("{vm_dir}/instance.img")
}

fn get_encrypted_storage_path(vm_dir: &str) -> String {
    format!("{vm_dir}/storage.img")
}

/// Holds information about files required to boot the VM instance.
pub(crate) struct InstanceData {
    /// Unique VM instance ID.
    pub(crate) instance_id: [u8; 64],
    /// Path to the VM directory.
    pub(crate) vm_dir: String,
    /// File descriptor for the instance image.
    pub(crate) instance_image: ParcelFileDescriptor,
    /// File descriptor for the encrypted storage image.
    pub(crate) encrypted_storage: ParcelFileDescriptor,
}

impl InstanceData {
    /// Loads an existing VM instance.
    pub(crate) fn load_existing(
        virt_service: &dyn IVirtualizationService,
        aiseal_config: &AiSealConfig,
    ) -> Result<InstanceData> {
        let instance_id_path = get_instance_id_path();
        let instance_id = load_instance_id(&instance_id_path)
            .with_context(|| format!("Failed to read instance id from {instance_id_path}"))?;

        let vm_dir = get_vm_dir();

        let instance_image_path = get_instance_image_path(&vm_dir);
        let instance_image = load_image(&instance_image_path)
            .with_context(|| format!("Failed to open instance image from {instance_image_path}"))?;

        let encrypted_storage_path = get_encrypted_storage_path(&vm_dir);
        let encrypted_storage = load_image(&encrypted_storage_path).with_context(|| {
            format!("Failed to open encrypted storage from {encrypted_storage_path}")
        })?;

        // Update storage size to the current size from configuration, if needed
        virt_service
            .setEncryptedStorageSize(&encrypted_storage, aiseal_config.encrypted_storage_bytes)?;

        Ok(InstanceData { instance_id, vm_dir, instance_image, encrypted_storage })
    }

    /// Creates a new VM instance.
    pub(crate) fn create(
        virt_service: &dyn IVirtualizationService,
        aiseal_config: &AiSealConfig,
    ) -> Result<InstanceData> {
        let instance_id: [u8; 64] =
            virt_service.allocateInstanceId().context("Failed to allocate instance id")?;
        let instance_id_path = get_instance_id_path();
        write_instance_id(&instance_id, &instance_id_path)
            .with_context(|| format!("Failed to write instance id to {instance_id_path}"))?;

        let vm_dir = get_vm_dir();
        create_vm_dir(&vm_dir)?;

        let instance_image_path = get_instance_image_path(&vm_dir);
        let instance_image = create_image(
            virt_service,
            &instance_image_path,
            AISEAL_INSTANCE_IMAGE_SIZE,
            PartitionType::ANDROID_VM_INSTANCE,
        )
        .with_context(|| format!("Failed to create instance image in {instance_image_path}"))?;

        let encrypted_storage_path = get_encrypted_storage_path(&vm_dir);
        let encrypted_storage = create_image(
            virt_service,
            &encrypted_storage_path,
            aiseal_config.encrypted_storage_bytes,
            PartitionType::ENCRYPTEDSTORE,
        )
        .with_context(|| {
            format!("Failed to create encrypted storage in {encrypted_storage_path}")
        })?;

        Ok(InstanceData { instance_id, vm_dir, instance_image, encrypted_storage })
    }
}

/// Removes all data related to the current VM instance.
pub(crate) fn invalidate_current_vm(
    instance_id: &[u8; 64],
    vm_dir: &str,
    virt_service: &dyn IVirtualizationService,
) -> Result<()> {
    // We have to delete instance id file first to make sure it is not reused
    let instance_id_path = get_instance_id_path();
    fs::remove_file(&instance_id_path)
        .with_context(|| format!("Failed to remove instance id file at {instance_id_path}"))?;
    delete_vm(vm_dir, instance_id, virt_service)
}

fn create_vm_dir(vm_dir: &str) -> Result<()> {
    let path = Path::new(vm_dir);
    if !path.exists() {
        fs::create_dir_all(path)?;
    }
    Ok(())
}

fn delete_vm(
    vm_dir: &str,
    instance_id: &[u8; 64],
    virt_service: &dyn IVirtualizationService,
) -> Result<()> {
    // Try to delete instance id from virt service.
    // We still need to delete VM directory even if it fails, so the error is only logged.
    if let Err(e) = virt_service.removeVmInstance(instance_id) {
        warn!("Failed to remove instance id from virtualization service: {:?}", e);
    }

    let path = Path::new(vm_dir);
    if path.exists() {
        fs::remove_dir_all(path)?;
    }
    Ok(())
}

fn load_instance_id(path: &str) -> Result<[u8; 64]> {
    let instance_id = fs::read(path)?;
    instance_id.try_into().map_err(|_| anyhow!("Invalid instance id"))
}

fn write_instance_id(instance_id: &[u8; 64], path: &str) -> Result<()> {
    fs::write(path, instance_id).context("Failed to write instance id")
}

fn load_image(path: &str) -> Result<ParcelFileDescriptor> {
    let image = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .context("Failed to open image file")?;
    let image = ParcelFileDescriptor::new(image);
    Ok(image)
}

fn create_image(
    virt_service: &dyn IVirtualizationService,
    path: &str,
    size: i64,
    partition_type: PartitionType,
) -> Result<ParcelFileDescriptor> {
    let image = fs::OpenOptions::new()
        .create(true)
        .truncate(true)
        .read(true)
        .write(true)
        .open(path)
        .context("Failed to create image file")?;
    let image = ParcelFileDescriptor::new(image);
    virt_service.initializeWritablePartition(&image, size, partition_type)?;
    Ok(image)
}
