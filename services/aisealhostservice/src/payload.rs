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

//! Manages payload APKs that are required for Virtualization Service to boot multi-tenant VM.

use crate::config::AiSealConfig;
use crate::package_manager::PackageManager;
use android_system_virtualizationservice::aidl::android::system::virtualizationservice::{
    IVirtualizationService::IVirtualizationService,
};
use anyhow::{anyhow, bail, Context, Result};
use binder::ParcelFileDescriptor;
use log::info;
use microdroid_payload_config::{TaskType, TenantConfig};
use std::{
    collections::HashSet,
    fs::{self, File},
    io::BufReader,
};
use zip::ZipArchive;

/// Holds file descriptors for all APKs and idsig files required to boot a VM.
pub(crate) struct VmPayload {
    /// File descriptor of the main config APK.
    pub(crate) config_apk: ParcelFileDescriptor,
    /// File descriptor of the idsig of the main config APK.
    pub(crate) config_idsig: ParcelFileDescriptor,
    /// File descriptors of tenant APKs.
    pub(crate) tenant_apks: Vec<ParcelFileDescriptor>,
    /// File descriptors of idsigs of tenant APKs.
    pub(crate) tenant_idsigs: Vec<ParcelFileDescriptor>,
}

impl VmPayload {
    /// Loads all payload APKs and their idsigs.
    pub(crate) fn load(
        pm: &PackageManager,
        vm: &dyn IVirtualizationService,
        vm_dir: &str,
        aiseal_config: &AiSealConfig,
    ) -> Result<VmPayload> {
        let vm_payload_config = &aiseal_config.vm_payload_config;
        let aiseal_payload_config = &aiseal_config.aiseal_payload_config;

        let aiseal_tenant_packages: HashSet<String> =
            aiseal_payload_config.tenants.iter().map(|t| t.name.clone()).collect();
        if aiseal_tenant_packages.len() != aiseal_payload_config.tenants.len() {
            bail!("AiSeal config contains duplicate tenants");
        }

        let vm_tenant_packages: HashSet<String> = vm_payload_config
            .tenants
            .iter()
            .map(|tenant| {
                match tenant {
                    TenantConfig::Apex(config) => &config.name,
                    TenantConfig::Apk(config) => &config.name,
                }
                .clone()
            })
            .collect();
        if vm_tenant_packages.len() != vm_payload_config.tenants.len() {
            bail!("VM config contains duplicate tenants");
        }

        if aiseal_tenant_packages != vm_tenant_packages {
            bail!("Mismatch between AiSeal config and VM tenancy config");
        }

        let mut tenant_apks: Vec<ParcelFileDescriptor> = vec![];
        let mut tenant_idsigs: Vec<ParcelFileDescriptor> = vec![];

        for (index, tenant) in vm_payload_config.tenants.iter().enumerate() {
            let tenant_config = match tenant {
                // We need to collect only APKs, all other tenant types are collected by AVF itself.
                TenantConfig::Apk(config) => config,
                _ => continue,
            };
            let tenant_apk_name = &tenant_config.name;
            if let Some(tenant_task) = tenant_config.task.as_ref() {
                let tenant_apk = match tenant_task.type_ {
                    TaskType::Executable => {
                        find_payload_apk(pm, tenant_apk_name, None, &aiseal_config.abis)
                    }
                    TaskType::MicrodroidLauncher => find_payload_apk(
                        pm,
                        tenant_apk_name,
                        Some(&tenant_task.command),
                        &aiseal_config.abis,
                    ),
                }?;
                let tenant_apk = fs::File::open(&tenant_apk).with_context(|| {
                    format!("Failed to open tenant APK: {tenant_apk_name}:{tenant_apk}")
                })?;
                let tenant_apk = ParcelFileDescriptor::new(tenant_apk);
                let tenant_idsig =
                    prepare_idsig(vm, &tenant_apk, &format!("{vm_dir}/tenant_idsig_{index}"))?;
                tenant_apks.push(tenant_apk);
                tenant_idsigs.push(tenant_idsig);
            }
        }

        let config_apk_path = &aiseal_config.payload_config_package_path;
        let config_apk = ParcelFileDescriptor::new(
            fs::File::open(config_apk_path)
                .with_context(|| format!("Failed to open VM config APK: {config_apk_path}"))?,
        );
        let config_idsig = prepare_idsig(vm, &config_apk, &format!("{vm_dir}/config_idsig"))?;
        Ok(VmPayload { config_apk, config_idsig, tenant_apks, tenant_idsigs })
    }
}

fn find_payload_apk(
    pm: &PackageManager,
    package_name: &str,
    payload_binary_name: Option<&str>,
    abis: &[String],
) -> Result<String> {
    let package_info = pm.get_package_info(package_name)?;

    if let Some(payload_binary_name) = payload_binary_name {
        let split_apk_paths: Option<Vec<Option<String>>> = package_info.splitSourceDirs;
        if let Some(split_apk_paths) = split_apk_paths {
            if !abis.is_empty() {
                let library_paths: Vec<_> =
                    abis.iter().map(|abi| format!("lib/{}/{}", abi, payload_binary_name)).collect();

                for apk_path in &split_apk_paths {
                    if let Some(apk_path) = apk_path.as_ref() {
                        let file = File::open(apk_path)
                            .with_context(|| format!("Failed to open APK path: {apk_path}"))?;
                        let zip = ZipArchive::new(BufReader::new(file))
                            .with_context(|| format!("Failed to scan split APK: {apk_path}"))?;

                        if library_paths
                            .iter()
                            .any(|name| zip.file_names().any(|zip_name| zip_name == name))
                        {
                            info!("Found payload in {apk_path}");
                            return Ok(apk_path.clone());
                        }
                    }
                }
            }
        }
    }

    // Return the path to the base APK
    package_info.sourceDir.ok_or(anyhow!("Package info for {package_name} doesn't have sourceDir"))
}

fn prepare_idsig(
    service: &dyn IVirtualizationService,
    apk_fd: &ParcelFileDescriptor,
    idsig_path: &str,
) -> Result<ParcelFileDescriptor> {
    // Create or update idsig file via VirtualizationService
    let idsig = fs::OpenOptions::new()
        .create(true)
        .truncate(true)
        .read(true)
        .write(true)
        .open(idsig_path)
        .with_context(|| format!("Failed to create idsig file: {idsig_path}"))?;
    let idsig = ParcelFileDescriptor::new(idsig);
    service
        .createOrUpdateIdsigFile(apk_fd, &idsig)
        .with_context(|| format!("Failed to update idsig file: {idsig_path}"))?;

    // Open idsig as read-only
    let idsig = fs::File::open(idsig_path)
        .with_context(|| format!("Failed to open idsig file: {idsig_path}"))?;
    let idsig = ParcelFileDescriptor::new(idsig);
    Ok(idsig)
}
