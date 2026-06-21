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

//! AiSeal specific configuration

use crate::package_manager::PackageManager;
use anyhow::{anyhow, bail, Context, Result};
use log::info;
use microdroid_payload_config::VmPayloadConfig;
use rustutils::android::system_properties;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{BufReader, Read, Seek};
use zip::ZipArchive;

const ABILIST_PROPERTY: &str = "ro.product.cpu.abilist";
const DEBUGGABLE_PROPERTY: &str = "ro.debuggable";
const TENANT_CONFIG_PACKAGE_PROPERTY: &str = "service.aiseal.tenant_config_package";
const TENANT_CONFIG_PATH_PROPERTY: &str = "service.aiseal.tenant_config_path";
const AISEAL_CONFIG_PATH_PROPERTY: &str = "service.aiseal.aiseal_config_path";
const AISEAL_PROTECTED_VM_FLAG: &str = "service.aiseal.protected_vm";
const AISEAL_MEMORY_BYTES: &str = "service.aiseal.memory_bytes";
const AISEAL_ENCRYPTED_STORAGE_BYTES: &str = "service.aiseal.encrypted_storage_bytes";
const AISEAL_PROTECTED_VM_FLAG_DEFAULT: bool = true;
const AISEAL_DEBUGGABLE_DEFAULT: bool = false;
const AISEAL_MEMORY_BYTES_DEFAULT: i64 = 300 * 1024 * 1024;
const AISEAL_ENCRYPTED_STORAGE_BYTES_DEFAULT: i64 = 16 * 1024 * 1024 * 1024;

/// Full AiSeal configuration.
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct AiSealConfig {
    /// Is VM debuggable.
    pub(crate) debuggable: bool,
    /// Is VM protected.
    pub(crate) protected_vm: bool,
    /// VM memory size in MiB.
    pub(crate) memory_mib: i32,
    /// VM encrypted storage size in bytes.
    pub(crate) encrypted_storage_bytes: i64,
    /// Supported ABIs.
    pub(crate) abis: Vec<String>,
    /// Name of the package with the payload config.
    pub(crate) payload_config_package_name: String,
    /// Path to the APK with the payload config.
    pub(crate) payload_config_package_path: String,
    /// VM payload configuration, shared with AVF.
    pub(crate) vm_payload_config: VmPayloadConfig,
    /// Path to the VM payload config inside config APK.
    pub(crate) vm_payload_config_path: String,
    /// AiSeal specific payload configuration.
    pub(crate) aiseal_payload_config: AiSealPayloadConfig,
}

/// AiSeal-specific payload configuration, stored inside the payload config APK.
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub(crate) struct AiSealPayloadConfig {
    /// Version of the config
    #[serde(default)]
    pub(crate) version: i32,

    /// List of tenants in the VM
    #[serde(default)]
    pub(crate) tenants: Vec<AiSealTenant>,
}

/// AiSeal-specific tenant configuration.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub(crate) struct AiSealTenant {
    /// Package name of the tenant.
    pub(crate) name: String,

    /// List of services exported by the tenant to its host application.
    #[serde(default)]
    #[serde(alias = "host_services")]
    pub(crate) exported_services: Vec<ExportedService>,
}

/// Configuration of service provided by VM tenant to its host application.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub(crate) struct ExportedService {
    /// Name of the service.
    pub(crate) name: String,

    /// Vsock port that is used to serve a service.
    pub(crate) port: i32,
}

/// Exported service together with its owner package name.
pub(crate) struct ExportedServiceWithOwner {
    /// Package name of the tenant that owns a service.
    pub(crate) owner: String,
    /// Exported service configuration.
    pub(crate) service: ExportedService,
}

impl AiSealPayloadConfig {
    /// Returns a map from a service name to its owner.
    pub(crate) fn get_service_to_owner_map(&self) -> HashMap<String, ExportedServiceWithOwner> {
        self.tenants
            .iter()
            .flat_map(|tenant| {
                tenant.exported_services.iter().map(move |service| {
                    (
                        service.name.clone(),
                        ExportedServiceWithOwner {
                            owner: tenant.name.clone(),
                            service: service.clone(),
                        },
                    )
                })
            })
            .collect()
    }
}

impl AiSealConfig {
    /// Loads AiSeal configuration from system properties and payload config APK.
    pub(crate) fn load(pm: &PackageManager) -> Result<AiSealConfig> {
        let debuggable = get_debuggable()?;
        let protected_vm = get_protected_vm_flag()?;
        let memory_bytes = get_memory_bytes()?;
        let memory_mib = bytes_to_memory_mib(memory_bytes)?;
        let encrypted_storage_bytes = get_encrypted_storage_bytes()?;
        let abis = get_abis()?;
        let config_package = find_payload_config_package()?;
        let vm_payload_config_path = find_payload_config_path()?;
        let aiseal_payload_config_path = find_aiseal_payload_config_path()?;

        info!("Loading payload config from {config_package}");

        let config_package_info = pm
            .get_package_info(&config_package)
            .with_context(|| format!("Failed to get config APK info: {config_package}"))?;
        let config_apk_path = config_package_info
            .sourceDir
            .ok_or(anyhow!("Failed to get config APK path: {config_package}"))?;
        let mut config_apk_zip = ZipArchive::new(BufReader::new(
            fs::File::open(&config_apk_path)
                .with_context(|| format!("Failed to open config APK: {config_apk_path}"))?,
        ))
        .with_context(|| format!("Failed to unzip config APK: {config_apk_path}"))?;

        let vm_payload_config: VmPayloadConfig =
            get_config(&mut config_apk_zip, &vm_payload_config_path).with_context(|| {
                format!(
                "Failed to load VM payload config from {config_package}:{vm_payload_config_path}",
            )
            })?;
        let aiseal_payload_config: AiSealPayloadConfig =
            get_config(&mut config_apk_zip, &aiseal_payload_config_path).with_context(|| {
                format!(
                "Failed to load AiSeal config from {config_package}:{aiseal_payload_config_path}",
            )
            })?;

        Ok(AiSealConfig {
            debuggable,
            protected_vm,
            memory_mib,
            encrypted_storage_bytes,
            abis,
            payload_config_package_name: config_package,
            payload_config_package_path: config_apk_path,
            vm_payload_config,
            vm_payload_config_path,
            aiseal_payload_config,
        })
    }
}

fn get_debuggable() -> Result<bool> {
    system_properties::read_bool(DEBUGGABLE_PROPERTY, AISEAL_DEBUGGABLE_DEFAULT)
        .with_context(|| format!("Failed to get debuggable property {DEBUGGABLE_PROPERTY}"))
}

fn get_protected_vm_flag() -> Result<bool> {
    system_properties::read_bool(AISEAL_PROTECTED_VM_FLAG, AISEAL_PROTECTED_VM_FLAG_DEFAULT)
        .with_context(|| format!("Failed to get protected VM flag {AISEAL_PROTECTED_VM_FLAG}"))
}

fn parse_and_check_non_negative(s: &str) -> Result<i64> {
    let result = s.parse::<i64>().with_context(|| format!("Failed to parse integer: {s}"))?;
    if result < 0 {
        bail!("Negative value is not allowed: {result}")
    }
    Ok(result)
}

fn get_memory_bytes() -> Result<i64> {
    {
        match system_properties::read(AISEAL_MEMORY_BYTES)? {
            Some(s) => parse_and_check_non_negative(&s),
            None => Ok(AISEAL_MEMORY_BYTES_DEFAULT),
        }
    }
    .with_context(|| format!("Failed to get memory bytes {AISEAL_MEMORY_BYTES}"))
}

fn get_encrypted_storage_bytes() -> Result<i64> {
    {
        match system_properties::read(AISEAL_ENCRYPTED_STORAGE_BYTES)? {
            Some(s) => parse_and_check_non_negative(&s),
            None => Ok(AISEAL_ENCRYPTED_STORAGE_BYTES_DEFAULT),
        }
    }
    .with_context(|| {
        format!("Failed to get encrypted storage bytes {AISEAL_ENCRYPTED_STORAGE_BYTES}")
    })
}

fn bytes_to_memory_mib(bytes: i64) -> Result<i32> {
    const ONE_MIB: i64 = 1024 * 1024;
    if bytes < 0 {
        bail!("Memory bytes must be non-negative: {bytes}")
    }
    let result: i64 = (bytes + ONE_MIB - 1) / ONE_MIB;
    result
        .try_into()
        .with_context(|| format!("memory value {} MiB is too large to fit in i32", result))
}

fn get_abis() -> Result<Vec<String>> {
    let value = system_properties::read(ABILIST_PROPERTY)
        .with_context(|| format!("Failed to get list of ABIs {ABILIST_PROPERTY}"))?
        .with_context(|| format!("List of ABIs {ABILIST_PROPERTY} is not set"))?;
    Ok(value.trim().split(',').map(|s| s.to_string()).collect())
}

fn find_payload_config_package() -> Result<String> {
    system_properties::read(TENANT_CONFIG_PACKAGE_PROPERTY)
        .with_context(|| {
            format!("Failed to get tenant config package {TENANT_CONFIG_PACKAGE_PROPERTY}")
        })?
        .with_context(|| {
            format!("Tenant config package {TENANT_CONFIG_PACKAGE_PROPERTY} is not set")
        })
}

fn find_payload_config_path() -> Result<String> {
    system_properties::read(TENANT_CONFIG_PATH_PROPERTY)
        .with_context(|| format!("Failed to get tenant config path {TENANT_CONFIG_PATH_PROPERTY}"))?
        .with_context(|| format!("Tenant config path {TENANT_CONFIG_PATH_PROPERTY} is not set"))
}

fn find_aiseal_payload_config_path() -> Result<String> {
    system_properties::read(AISEAL_CONFIG_PATH_PROPERTY)
        .with_context(|| format!("Failed to get AiSeal config path {AISEAL_CONFIG_PATH_PROPERTY}"))?
        .with_context(|| format!("AiSeal config path {AISEAL_CONFIG_PATH_PROPERTY} is not set"))
}

fn get_config<T: for<'de> Deserialize<'de>, R: Read + Seek>(
    config_apk_zip: &mut ZipArchive<R>,
    config_path: &str,
) -> Result<T> {
    let config_file = config_apk_zip.by_name(config_path).context("Failed to read config")?;
    serde_json::from_reader(config_file).context("Failed to parse config")
}
