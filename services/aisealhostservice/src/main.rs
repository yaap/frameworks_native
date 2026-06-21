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

//! Service for managing VMs for trusted AI compute

mod config;
mod instance_data;
mod package_manager;
mod payload;
mod vsock_selinux;

use crate::config::{AiSealConfig, ExportedServiceWithOwner};
use crate::instance_data::{invalidate_current_vm, InstanceData};
use crate::package_manager::PackageManager;
use crate::payload::VmPayload;
use crate::vsock_selinux::connect_with_cid_port_context;
use aiseal_internal_service_aidl::aidl::com::android::internal::aiseal::IAiSealInternalService::{
    BnAiSealInternalService, IAiSealInternalService,
};
use aisealhostservice_aidl::aidl::android::aiseal::IAiSealHostService::{
    BnAiSealHostService, IAiSealHostService,
};
use android_os_permissions_aidl::aidl::android::os::IPermissionController::IPermissionController;
use android_system_virtualizationcommon::aidl::android::system::virtualizationcommon::{
    ICEStoreKEK::{BnCEStoreKEK, ICEStoreKEK},
    IGuestAgent::IGuestAgent,
};
use android_system_virtualizationservice::aidl::android::system::virtualizationservice::{
    CpuOptions::CpuOptions,
    CpuOptions::CpuTopology::CpuTopology,
    IVirtualizationService::IVirtualizationService,
    VirtualMachineAppConfig::{
        CustomConfig::CustomConfig, DebugLevel::DebugLevel, Payload::Payload,
        VirtualMachineAppConfig,
    },
    VirtualMachineConfig::VirtualMachineConfig,
};
use anyhow::{anyhow, bail, Context, Result};
use binder::{
    add_service, BinderFeatures, ExceptionCode, Interface, IntoBinderResult, ParcelFileDescriptor,
    ProcessState, Strong, ThreadState,
};
use log::{error, info, warn};
use rustutils::android::system_properties::PropertyWatcher;
use rustutils::android::{system_properties, users::AID_ROOT, users::AID_SYSTEM};
use std::collections::HashMap;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;
use vmclient::{DeathReason, ErrorCode, VmInstance, VmWaitError};

const LOG_TAG: &str = "AiSealHostService";
const AISEAL_VM_START_TIMEOUT: Duration = Duration::from_secs(244);

fn handle_vm_death(
    reason: DeathReason,
    virt_service: &dyn IVirtualizationService,
    instance_id: &[u8; 64],
    vm_dir: &str,
) -> Result<()> {
    match reason {
        DeathReason::PvmFirmwarePublicKeyMismatch
        | DeathReason::PvmFirmwareInstanceImageChanged
        | DeathReason::MicrodroidPayloadHasChanged
        | DeathReason::MicrodroidPayloadVerificationFailed => {
            error!("Unexpected error during VM verification: {reason:?}");
            // All verification errors should invalidate stored data
            invalidate_current_vm(instance_id, vm_dir, virt_service)
        }
        _ => {
            error!("Unexpected VM death: {reason:?}");
            // All other death reasons may be temporary, don't invalidate data
            // TODO: try to cleanup data on repeated failures
            Ok(())
        }
    }
}

fn is_shutting_down() -> Result<bool> {
    let shutdown_sysprop_opt = system_properties::read("sys.shutdown.requested")?;
    if let Some(shutdown_sysprop) = shutdown_sysprop_opt {
        return Ok(!shutdown_sysprop.is_empty());
    }
    Ok(false)
}

fn wait_for_boot_completed() -> Result<()> {
    let mut watcher = PropertyWatcher::new("sys.boot_completed")?;
    info!("Waiting for boot completed...");
    watcher.wait_for_value("1", None).context("Failed to wait for sys.boot_completed")?;
    info!("Boot completed, proceeding with startup");
    Ok(())
}

fn try_main() -> Result<()> {
    android_logger::init_once(
        android_logger::Config::default().with_tag(LOG_TAG).with_max_level(log::LevelFilter::Info),
    );

    info!("Starting AiSealHostService");

    let is_enabled = system_properties::read_bool("service.aiseal.enable", false)?;
    if !is_enabled {
        error!("service.aiseal.enable is not set to 1, aiseal disabled. exiting");
        std::process::exit(1);
    }

    wait_for_boot_completed()?;

    ProcessState::start_thread_pool();

    let virtmgr: vmclient::VirtualizationService =
        vmclient::VirtualizationService::new().context("Failed to spawn VirtualizationService")?;
    let virtualization_service =
        virtmgr.connect().context("Failed to connect to VirtualizationService")?;

    let pm = PackageManager::new()?;

    let aiseal_config = AiSealConfig::load(&pm)?;

    let instance_data =
        match InstanceData::load_existing(virtualization_service.as_ref(), &aiseal_config) {
            Ok(instance_data) => instance_data,
            Err(e) => {
                warn!("Failed to load existing VM data: {:?}", e);
                info!("Creating new VM");
                InstanceData::create(virtualization_service.as_ref(), &aiseal_config)
                    .context("Failed to create VM data")?
            }
        };
    let vm_dir = instance_data.vm_dir;
    let instance_id = instance_data.instance_id;

    let payload = VmPayload::load(&pm, virtualization_service.as_ref(), &vm_dir, &aiseal_config)?;

    let custom_config = Some(CustomConfig { ..Default::default() });

    info!(
        "Starting VM: debuggable={}, protected_vm={}",
        aiseal_config.debuggable, aiseal_config.protected_vm
    );

    let config = VirtualMachineConfig::AppConfig(VirtualMachineAppConfig {
        name: String::from("AiSeal"),
        osName: String::from("microdroid"),
        apk: Some(payload.config_apk),
        idsig: Some(payload.config_idsig),
        tenantApks: payload.tenant_apks,
        tenantIdsigs: payload.tenant_idsigs,
        instanceId: instance_data.instance_id,
        instanceImage: Some(instance_data.instance_image),
        encryptedStorageImage: Some(instance_data.encrypted_storage),
        payload: Payload::ConfigPath(aiseal_config.vm_payload_config_path.clone()),
        debugLevel: if aiseal_config.debuggable { DebugLevel::FULL } else { DebugLevel::NONE },
        protectedVm: aiseal_config.protected_vm,
        memoryMib: aiseal_config.memory_mib,
        cpuOptions: CpuOptions { cpuTopology: CpuTopology::CpuCount(1) },
        customConfig: custom_config,
        ..Default::default()
    });

    let instance: VmInstance = VmInstance::create(
        virtualization_service.as_ref(),
        &config,
        // TODO: protect logs from logcat
        /* console_fd */ None,
        /* console_in_fd */ None,
        /* log_fd */ None,
        /* dump_dt */ None,
    )
    .context("Failed to create VM")?;

    let callback = Box::new(Callback {});
    instance.start(Some(callback)).context("Failed to start VM")?;

    let ready = instance.wait_until_ready(AISEAL_VM_START_TIMEOUT);
    match ready {
        Ok(()) => {
            info!("VM ready");
        }
        Err(VmWaitError::TimedOut) => {
            bail!("VM timed out during startup");
        }
        Err(VmWaitError::Died { reason }) => {
            // Make sure we doesn't hold any file descriptors
            drop(instance);
            drop(config);
            handle_vm_death(reason, virtualization_service.as_ref(), &instance_id, &vm_dir)?;
            bail!("VM died during startup - reason {:?}", reason);
        }
        Err(VmWaitError::Finished) => {
            // The payload has (unexpectedly) finished, but the VM is still running. Give it
            // some time to shutdown to maximize our chances of getting useful logs.
            if let Some(reason) = instance.wait_for_death_with_timeout(Duration::from_secs(5)) {
                // Make sure we doesn't hold any file descriptors
                drop(instance);
                drop(config);
                handle_vm_death(reason, virtualization_service.as_ref(), &instance_id, &vm_dir)?;
                bail!("VM has unexpectedly finished - reason {:?}", reason);
            }
            bail!("VM has unexpectedly finished");
        }
    }

    let instance = Arc::new(instance);
    let host_service = AiSealHostService::new_binder(pm, instance.clone(), aiseal_config);
    add_service("aiseal_host", host_service.as_binder()).context("Registering host service")?;

    let internal_service = AiSealInternalService::new_binder(instance.clone());
    add_service("aiseal_internal", internal_service.as_binder())
        .context("Registering internal service")?;

    std::thread::spawn(move || {
        let reason = instance.wait_for_death();
        if is_shutting_down().unwrap_or(false) {
            info!("VM died during shutdown, ignoring");
            return;
        }
        error!("VM has unexpectedly died: {:?}", reason);
        if let Err(e) =
            handle_vm_death(reason, virtualization_service.as_ref(), &instance_id, &vm_dir)
        {
            error!("Failed to handle VM death: {:?}", e);
        }
        std::process::exit(1);
    });

    info!("Registered services, joining threadpool");
    ProcessState::join_thread_pool();

    info!("Exiting");

    Ok(())
}

fn main() {
    if let Err(e) = try_main() {
        error!("{:?}", e);
        std::process::exit(1)
    }
}
/// A callback for VM lifecycle events.
struct Callback {}
impl vmclient::VmCallback for Callback {
    fn on_payload_started(&self, cid: i32) {
        log::info!("VM payload started, cid = {}", cid);
    }

    fn on_payload_ready(&self, cid: i32) {
        log::info!("VM payload ready, cid = {}", cid);
    }

    fn on_payload_finished(&self, cid: i32, exit_code: i32) {
        log::warn!("VM payload finished, cid = {}, exit code = {}", cid, exit_code);
    }

    fn on_error(&self, cid: i32, error_code: ErrorCode, message: &str) {
        // TODO: handle all sorts of errors
        log::warn!("VM error, cid = {}, error code = {:?}, message = {}", cid, error_code, message);
    }

    fn on_died(&self, cid: i32, death_reason: DeathReason) {
        log::warn!("VM died, cid = {}, reason = {:?}", cid, death_reason);
    }
}

/// Checks if the calling process has a given permission.
fn check_permission(perm: &str) -> binder::Result<()> {
    let calling_pid = ThreadState::get_calling_pid();
    let calling_uid = ThreadState::get_calling_uid();
    // Root can do anything
    if calling_uid == AID_ROOT {
        return Ok(());
    }
    let perm_svc: Strong<dyn IPermissionController> = binder::wait_for_interface("permission")?;
    if perm_svc.checkPermission(perm, calling_pid, calling_uid as i32)? {
        Ok(())
    } else {
        Err(anyhow!("Caller does not have the {} permission", perm))
            .or_binder_exception(ExceptionCode::SECURITY)
    }
}

/// Checks if the calling process has the `MANAGE_AISEAL_VIRTUAL_MACHINE` permission.
fn check_manage_permission() -> binder::Result<()> {
    check_permission("android.permission.MANAGE_AISEAL_VIRTUAL_MACHINE")
}

/// Gets SELinux context of the calling process.
fn get_calling_sid() -> binder::Result<String> {
    ThreadState::with_calling_sid(move |opt_sid| -> Result<String> {
        let sid = opt_sid.context("Failed to get calling sid")?;
        Ok(sid.to_string_lossy().into_owned())
    })
    .or_binder_exception(ExceptionCode::SECURITY)
}

/// Extracts MLS level from SELinux context.
fn extract_mls_level(context: &str) -> binder::Result<String> {
    let fields: Vec<_> = context.split(':').collect();
    if fields.len() == 4 {
        Ok(fields[3].to_string())
    } else if fields.len() == 5 {
        Ok(format!("{}:{}", fields[3], fields[4]))
    } else {
        Err(anyhow!("invalid context {}", context)).or_binder_exception(ExceptionCode::SECURITY)
    }
}

/// Replaces MLS level in SELinux context.
fn replace_mls_level(context: &str, level: &str) -> binder::Result<String> {
    let fields: Vec<_> = context.split(':').collect();
    if fields.len() >= 4 && fields.len() <= 5 {
        Ok(format!("{}:{}:{}:{}", fields[0], fields[1], fields[2], level))
    } else {
        Err(anyhow!("invalid context {}", context)).or_binder_exception(ExceptionCode::SECURITY)
    }
}
/// Implementation of the `IAiSealHostService` AIDL interface.
struct AiSealHostService {
    pm: PackageManager,
    instance: Arc<VmInstance>,
    service_to_owner: HashMap<String, ExportedServiceWithOwner>,
}

// TODO: implement dump
impl Interface for AiSealHostService {}

impl AiSealHostService {
    /// Creates a new binder object for the `AiSealHostService`.
    fn new_binder(
        pm: PackageManager,
        instance: Arc<VmInstance>,
        config: AiSealConfig,
    ) -> Strong<dyn IAiSealHostService> {
        let service_to_owner = config.aiseal_payload_config.get_service_to_owner_map();
        BnAiSealHostService::new_binder(
            AiSealHostService { pm, instance, service_to_owner },
            BinderFeatures { set_requesting_sid: true, ..BinderFeatures::default() },
        )
    }
}

impl IAiSealHostService for AiSealHostService {
    /// Connects to a vsock service provided by a tenant in the VM.
    fn connectService(&self, name: &str) -> binder::Result<ParcelFileDescriptor> {
        check_manage_permission()?;
        // TODO: vm state checks
        let service_description = &self
            .service_to_owner
            .get(name)
            .context("Service not found")
            .or_binder_exception(ExceptionCode::ILLEGAL_ARGUMENT)?;

        let calling_uid = ThreadState::get_calling_uid();
        // system_server and root are allowed to call any service
        if calling_uid != AID_ROOT && calling_uid != AID_SYSTEM {
            // TODO: validate owner signature?
            // TODO: validate package version
            let calling_package = self
                .pm
                .get_calling_package()
                .context("Failed to get calling package")
                .or_binder_exception(ExceptionCode::ILLEGAL_STATE)?;

            if calling_package != service_description.owner {
                Err(format!("Service {name} doesn't belong to {calling_package}"))
                    .or_binder_exception(ExceptionCode::SECURITY)?;
            }
        }
        // TODO: validate_vsock_port(port)?;
        let port = service_description.service.port as u32;
        // let vsock_pfd = self.instance.vm.connectVsock(port)?;
        let calling_context = get_calling_sid()?;
        // TODO: use getsockcreatecon
        let vsock_context = "u:r:aisealhostservice:s0";
        info!("calling_context: {calling_context}, vsock_context: {vsock_context}");
        // TODO: use context_range_get
        let calling_mls_level = extract_mls_level(&calling_context)?;
        // TODO: use context_range_set
        let vsock_new_context = replace_mls_level(vsock_context, &calling_mls_level)?;
        info!("vsock_new_context: {vsock_new_context}");
        connect_with_cid_port_context(self.instance.cid() as u32, port, &vsock_new_context)
            .or_binder_exception(ExceptionCode::SECURITY)
    }
}

/// Implementation of the `IAiSealInternalService` AIDL interface.
struct AiSealInternalService {
    instance: Arc<VmInstance>,
}

impl Interface for AiSealInternalService {}

impl AiSealInternalService {
    fn new_binder(instance: Arc<VmInstance>) -> Strong<dyn IAiSealInternalService> {
        BnAiSealInternalService::new_binder(
            AiSealInternalService { instance },
            BinderFeatures::default(),
        )
    }

    fn get_guest_agent(&self) -> binder::Result<Strong<dyn IGuestAgent>> {
        let Some(guest_agent) = self.instance.vm.getGuestAgent()? else {
            return Err(anyhow!("No guest agent"))
                .or_binder_exception(ExceptionCode::ILLEGAL_STATE);
        };
        Ok(guest_agent)
    }
}

impl IAiSealInternalService for AiSealInternalService {
    fn onUserUnlocking(&self, user_id: i32, kek_file: &str) -> binder::Result<()> {
        info!("onUserUnlocking {user_id}");
        let kek = CEStoreKEK::new_binder(kek_file);
        self.get_guest_agent()?.userUnlocked(user_id, &kek)
    }

    fn onUserStopped(&self, user_id: i32) -> binder::Result<()> {
        info!("onUserStopped {user_id}");
        self.get_guest_agent()?.userLocked(user_id)
    }

    fn onUserRemoved(&self, user_id: i32) -> binder::Result<()> {
        info!("onUserRemoved {user_id}");
        self.get_guest_agent()?.userRemoved(user_id)
    }
}

struct CEStoreKEK {
    kek_file: String,
}

impl Interface for CEStoreKEK {}

impl CEStoreKEK {
    fn new_binder(kek_file: &str) -> Strong<dyn ICEStoreKEK> {
        BnCEStoreKEK::new_binder(
            CEStoreKEK { kek_file: kek_file.to_owned() },
            BinderFeatures::default(),
        )
    }
}

impl ICEStoreKEK for CEStoreKEK {
    fn getKEK(&self) -> binder::Result<std::option::Option<Vec<u8>>> {
        match fs::read(&self.kek_file) {
            Ok(data) => Ok(Some(data)),
            Err(_) => Ok(None),
        }
    }

    fn onKEKCreated(&self, key: &[u8]) -> binder::Result<()> {
        if let Err(e) = fs::create_dir_all(Path::new(&self.kek_file).parent().unwrap()) {
            return Err(format!("Can't create directories for {}: {}", self.kek_file, e))
                .or_binder_exception(ExceptionCode::ILLEGAL_STATE);
        }

        let mut f = match File::create(&self.kek_file) {
            Ok(f) => f,
            Err(e) => {
                return Err(format!("Can't create kek file {}: {}", self.kek_file, e))
                    .or_binder_exception(ExceptionCode::ILLEGAL_STATE);
            }
        };

        if let Err(e) = f.write_all(key) {
            return Err(format!("Can't write kek file {}: {}", self.kek_file, e))
                .or_binder_exception(ExceptionCode::ILLEGAL_STATE);
        }

        if let Err(e) = f.sync_all() {
            return Err(format!("Can't sync kek file {}: {}", self.kek_file, e))
                .or_binder_exception(ExceptionCode::ILLEGAL_STATE);
        }
        Ok(())
    }
}
