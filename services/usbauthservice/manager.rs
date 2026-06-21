// Copyright (C) 2025 The Android Open Source Project
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

//! Manages USB device authorization policies and decisions.

use crate::authorization::Authorizer;
use crate::device_info::UsbDeviceInfoWithState;
use crate::parser::{Parser, PolicyLoadError};
use crate::rules::{
    Action, DeviceAttributes, DeviceId, InterfaceAttribute, InterfaceType, Policy, Rule,
};
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationStatus::UsbAuthorizationStatus;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use log::debug;
use regex::Regex;
use std::any::Any;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use thiserror::Error;
use tokio::sync::Notify;
use ueventd::device::Device;

/// Represents the possible errors that can occur in the `UsbDeviceAuthManager`.
#[derive(Error, Debug)]
pub enum Error {
    /// An I/O error occurred while interacting with the file system.
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    /// An error occurred during device authorization.
    #[error("Authorization error: {0}")]
    Authorization(#[from] crate::device_info::AuthorizationError),
    /// An error occurred while parsing the USB authorization policy.
    #[error("Policy parsing error: {0}")]
    Parse(#[from] PolicyLoadError),
    /// The specified device was not found.
    #[error("Device not found: {0}")]
    DeviceNotFound(String),
    /// Error while looking for the root mountpoint.
    #[error("Root mountpoint not found: {0}")]
    RootMountpointNotFound(String),
}

/// Path to the usbcore authorized_default parameter.
const USBCORE_AUTHORIZED_DEFAULT_PATH: &str = "module/usbcore/parameters/authorized_default";

/// Path to the USB devices directory.
const USB_DEVICES_PATH: &str = "bus/usb/devices/";

/// Relative path to the static USB authorization policy file.
const USB_AUTH_POLICY_CONF_RELATIVE_PATH: &str = "usb_auth/policy.conf";

/// Relative path to the interactive USB authorization policy file.
/// This requires a client to respond to policy directives.
const USB_AUTH_INTERACTIVE_POLICY_CONF_RELATIVE_PATH: &str = "usb_auth/interactive_policy.conf";

/// Relative path to the internal devices configuration file.
const USB_AUTH_INTERNAL_DEVICES_CONF_RELATIVE_PATH: &str = "usb_auth/internal_devices.conf";

/// Maximum number of retry attempts for processing a device that is pending.
const MAX_RETRY_ATTEMPTS: u8 = 5;

/// Interval for checking pending devices.
const PENDING_DEVICE_CHECK_INTERVAL: Duration = Duration::from_millis(100);

/// Relative path to /proc/mounts.
const PROC_MOUNTS_PATH: &str = "mounts";

/// When searching for root mountpoint, only consider block devices.
const BLOCK_DEVICE_PREFIX: &str = "/dev/block";

/// Relative path in /sys to block class device.
const SYS_BLOCK_CLASS_PREFIX: &str = "class/block";

/// Regex matching usb device sysfs paths.
const USB_PATH_REGEX: &str = r"^.*/devices/.*/usb[0-9]+";

/// Regex matching just the root hub segment.
const USB_ROOT_HUB_REGEX: &str = r"usb[0-9]+";

// Placeholder for the list of internal devices.
struct InternalDevices;

/// Callback for auth events if any are ready to be sent.
pub trait AuthEventsCallback: Any + Send {
    /// Send an event requesting user interaction asking to authorize this device.
    fn send_ask(&mut self, device: &UsbAuthDeviceInfo);

    /// Send an event requesting client to check prior authorization history for this device.
    fn send_allow_persisted(&mut self, device: &UsbAuthDeviceInfo);

    /// Send an event notifying the client of a device authorization decision.
    fn send_status_change(
        &mut self,
        device: &UsbAuthDeviceInfo,
        status: &UsbAuthorizationStatus,
        system_state: &UsbAuthorizationSystemState,
    );

    /// Cast self as Any trait -- necessary for downcasting.
    fn as_any(&self) -> &dyn Any;

    /// Compare this callback against another callback object.
    fn equals(&self, other: &dyn AuthEventsCallback) -> bool;
}

/// Manages the lists of USB devices based on their authorization state.
pub struct UsbDeviceAuthManager {
    /// The root directory for the system files. Typically /sys, but might be
    /// different for testing.
    root_sys_dir: PathBuf,
    /// The root directory for the etc files. Typically /etc, but might be
    /// different for testing.
    root_etc_dir: PathBuf,
    /// The root directory for /proc. Typically /proc, but might be different for testing.
    root_proc_dir: PathBuf,
    /// Used for processing authorization of device with policy rules.
    authorizer: Authorizer,
    /// Devices that have been processed and their authorization state determined.
    processed_devices: Vec<UsbDeviceInfoWithState>,
    /// Devices whose authorization was deferred pending a system state change.
    deferred_devices: Vec<UsbDeviceInfoWithState>,
    /// Devices that require user interaction for authorization.
    ask_devices: Vec<UsbDeviceInfoWithState>,
    /// Devices that are pending to be processed because their sysfs path did not exist.
    pending_devices: Vec<(Device, u8)>,
    /// Devices that requires looking up previous user interaction for authorization.
    allow_persisted_devices: Vec<UsbDeviceInfoWithState>,
    /// The current system authorization state.
    system_state: UsbAuthorizationSystemState,
    /// The static policy rules used for device authorization.
    policy: Policy,
    /// Registered callbacks for auth events.
    callbacks: Vec<Box<dyn AuthEventsCallback>>,
    /// Notifier to wake up the pending device processing loop.
    notify: Arc<Notify>,
}

impl UsbDeviceAuthManager {
    /// Returns a clone of the list of processed devices.
    pub fn processed_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.processed_devices.clone()
    }

    /// Returns a clone of the list of deferred devices.
    pub fn deferred_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.deferred_devices.clone()
    }

    /// Returns a clone of the list of devices requiring user interaction.
    pub fn ask_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.ask_devices.clone()
    }

    /// Returns a clone of the list of devices requiring lookup of previous user response.
    pub fn allow_persisted_devices(&self) -> Vec<UsbDeviceInfoWithState> {
        self.allow_persisted_devices.clone()
    }

    /// Returns a reference to the current system state for USB authorization.
    pub fn system_state(&self) -> &UsbAuthorizationSystemState {
        &self.system_state
    }

    /// Returns a reference to the static policy rules for USB device authorization.
    pub fn policy(&self) -> &Policy {
        &self.policy
    }

    /// Returns a list of authorized devices from the processed devices list.
    pub fn get_authorized_devices(&self) -> Vec<UsbAuthDeviceInfo> {
        self.processed_devices.iter().filter(|d| d.authorized).map(|d| d.info.clone()).collect()
    }

    /// Returns the authorization status of a device.
    pub fn get_authorization_status(&self, device_syspath: &str) -> UsbAuthorizationStatus {
        if self.processed_devices.iter().any(|d| d.info.syspath == device_syspath && d.authorized) {
            return UsbAuthorizationStatus::AUTHORIZED;
        }
        UsbAuthorizationStatus::DENIED
    }

    /// Creates a new `UsbDeviceAuthManager` and performs initial setup.
    pub fn new(use_interactive_policy: bool, debuggable: bool) -> Result<Self, Error> {
        Self::with_paths("/sys", "/etc", "/proc", use_interactive_policy, debuggable)
    }

    /// Creates a new `UsbDeviceAuthManager` with specified root directories and performs initial setup.
    /// This function is useful for testing with mock file systems.
    pub fn with_paths<P: AsRef<Path>>(
        root_sys_dir_path: P,
        root_etc_dir_path: P,
        root_proc_dir_path: P,
        use_interactive_policy: bool,
        debuggable: bool,
    ) -> Result<Self, Error> {
        let mut manager = Self {
            authorizer: Authorizer::new(),
            processed_devices: Vec::new(),
            deferred_devices: Vec::new(),
            ask_devices: Vec::new(),
            pending_devices: Vec::new(),
            allow_persisted_devices: Vec::new(),
            system_state: UsbAuthorizationSystemState::BOOTED,
            root_sys_dir: root_sys_dir_path.as_ref().to_path_buf(),
            root_etc_dir: root_etc_dir_path.as_ref().to_path_buf(),
            root_proc_dir: root_proc_dir_path.as_ref().to_path_buf(),
            policy: create_default_policy(),
            callbacks: Vec::new(),
            notify: Arc::new(Notify::new()),
        };
        debug!("Setting initial USB authorization state to deny all devices.");
        manager.set_default_to_deny_for_new_devices()?;

        let mut parser = Parser::new(debuggable);
        if use_interactive_policy {
            debug!("Attempting to use interactive policy");
            if manager.load_interactive_policy(&mut parser).is_err() {
                debug!("Interactive policy failed. Falling back to static policy.");
                manager.load_static_policy(&mut parser)?;
            }
        } else {
            debug!("Loading static policy");
            manager.load_static_policy(&mut parser)?;
        }
        debug!("Loading internal devices list");
        manager.load_internal_devices()?;
        debug!("Protecting boot disk from authorization if USB");
        manager.protect_boot_disk();
        debug!("System state set to Booted");
        debug!("Binder service will be set up in main.");
        Ok(manager)
    }

    /// Loads the list of internal devices from a file or ACPI.
    fn load_internal_devices(&mut self) -> Result<(), Error> {
        debug!("Loading internal devices list");
        let internal_devices_file_path =
            self.root_etc_dir.join(USB_AUTH_INTERNAL_DEVICES_CONF_RELATIVE_PATH);
        if internal_devices_file_path.exists() {
            let _ = load_internal_devices_from_file(&internal_devices_file_path)?;
        } else {
            debug!("Internal devices file not found. Assuming no internal devices.");
        }
        Ok(())
    }

    // Find the root mount from /proc/mounts. We look only at blocks that are in /dev/block and
    // find something matching /system (preferred) or /.
    fn find_root_mount(&mut self) -> Result<PathBuf, Error> {
        let file = fs::File::open(self.root_proc_dir.join(PROC_MOUNTS_PATH))
            .map_err(|e| Error::RootMountpointNotFound(format!("Couldn't open mounts: {:?}", e)))?;
        let reader = BufReader::new(file);

        let mut block_lines: Vec<(String, String)> = vec![];

        // Read all lines and find ones starting with "/dev/block".
        for line in reader.lines().map_while(Result::ok) {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2 && parts[0].starts_with(BLOCK_DEVICE_PREFIX) {
                block_lines.push((parts[0].into(), parts[1].into()));
            }
        }

        // Find one that contains /system or /.
        if let Some(p) = block_lines.iter().position(|(_, m)| *m == "/system") {
            return Ok(PathBuf::from(block_lines[p].0.clone()));
        } else if let Some(p) = block_lines.iter().position(|(_, m)| *m == "/") {
            return Ok(PathBuf::from(block_lines[p].0.clone()));
        }

        Err(Error::RootMountpointNotFound("No /system or / mountpoints found.".to_string()))
    }

    fn find_usb_path_from(
        &mut self,
        target_path: PathBuf,
        usb_regex: &Regex,
    ) -> Result<PathBuf, Error> {
        let slaves_dir = target_path.join("slaves");
        if slaves_dir.exists() {
            for entry in fs::read_dir(slaves_dir).map_err(Error::Io)?.filter_map(|e| e.ok()) {
                if let Ok(path) = self.find_usb_path_from(entry.path(), usb_regex) {
                    return Ok(path);
                }
            }
        }

        let canonical = fs::canonicalize(&target_path)
            .map_err(|e| Error::RootMountpointNotFound(e.to_string()))?;

        if usb_regex.is_match(&canonical.to_string_lossy()) {
            Ok(canonical.to_owned())
        } else {
            Err(Error::RootMountpointNotFound(format!(
                "No usb mount for {}: {}",
                target_path.display(),
                canonical.display()
            )))
        }
    }

    fn extract_usb_path(&mut self, resolved_path: &Path, root_hub_regex: &Regex) -> String {
        let mut segments = Vec::new();
        let mut hub_found = false;

        for component in resolved_path.components() {
            let segment = component.as_os_str().to_string_lossy();
            // If we've already found the root hub, break at first segment with `:`. This will
            // be the interface path.
            if hub_found {
                if segment.contains(':') {
                    break;
                }
            } else if root_hub_regex.is_match(&segment) {
                hub_found = true;
            }

            // Push all segments found into the list so far
            segments.push(segment);
        }

        let mut result = PathBuf::new();
        for s in segments {
            result.push(s.to_string());
        }

        result.to_string_lossy().into()
    }

    fn find_usb_path_for_mount(&mut self, mount: PathBuf) -> Result<String, Error> {
        if !mount.starts_with(BLOCK_DEVICE_PREFIX) {
            return Err(Error::RootMountpointNotFound(format!(
                "Not a block device path: {}",
                mount.display()
            )));
        }

        let suffix = mount
            .strip_prefix(BLOCK_DEVICE_PREFIX)
            .map_err(|e| Error::RootMountpointNotFound(e.to_string()))?;

        let usb_regex = Regex::new(USB_PATH_REGEX)
            .map_err(|_| Error::RootMountpointNotFound("Regex error".into()))?;
        let root_hub_regex = Regex::new(USB_ROOT_HUB_REGEX)
            .map_err(|_| Error::RootMountpointNotFound("Regex error on root hub".into()))?;

        let path = self.find_usb_path_from(
            self.root_sys_dir.join(SYS_BLOCK_CLASS_PREFIX).join(suffix),
            &usb_regex,
        )?;

        Ok(self.extract_usb_path(&path, &root_hub_regex))
    }

    /// If the boot disk (i.e. which mounts root) is an external USB device, mark it as an internal
    /// device in the authorizer so we don't mistakenly de-authorize it. This is necessary for
    /// booting from external disk.
    fn protect_boot_disk(&mut self) -> Option<String> {
        let root_mount = match self.find_root_mount() {
            Ok(p) => p,
            Err(e) => {
                // When no root mountpoint is found, assume /dev/block/sda as the external boot
                // disk (we do not support external NVMe at boot).
                debug!("No root mountpoint found: {:?}", e);
                PathBuf::from("/dev/block/sda")
            }
        };

        match self.find_usb_path_for_mount(root_mount) {
            Ok(usb_path) => {
                debug!("Root mountpoint is on external USB at {}", usb_path);
                self.authorizer.add_internal_device(usb_path.clone());

                Some(usb_path)
            }
            Err(e) => {
                debug!("No USB path for root mountpoint: {:?}", e);
                None
            }
        }
    }

    /// Loads the interactive USB policy from a file or falls back to the static policy.
    fn load_interactive_policy(&mut self, parser: &mut Parser) -> Result<(), Error> {
        debug!("Loading interactive USB policy");
        let policy_file_path =
            self.root_etc_dir.join(USB_AUTH_INTERACTIVE_POLICY_CONF_RELATIVE_PATH);
        parser.parse_from_file(&policy_file_path)?;
        self.policy = parser.policy().clone();

        Ok(())
    }

    /// Loads the static USB policy from a file or creates a default one if the file does not exist.
    fn load_static_policy(&mut self, parser: &mut Parser) -> Result<(), Error> {
        debug!("Loading static USB policy");
        let policy_file_path = self.root_etc_dir.join(USB_AUTH_POLICY_CONF_RELATIVE_PATH);
        if policy_file_path.exists() {
            debug!("Policy file found at {:?}. Loading static policy.", policy_file_path);
            parser.parse_from_file(&policy_file_path)?;
            self.policy = parser.policy().clone();
        } else {
            debug!("Policy file not found. Creating default policy to allow HID, HUB and ethernet devices.");
            self.policy = create_default_policy();
        }
        Ok(())
    }

    /// Iterates through existing USB devices and sets their 'authorized_default' to '0' (deny).
    /// This is called during the initial setup of the UsbDeviceAuthManager.
    fn set_default_to_deny_for_new_devices(&mut self) -> Result<(), Error> {
        debug!("Setting default to deny for new devices");
        self.set_module_default_to_deny()?;
        debug!("Setting default to deny for existing USB devices");
        let usb_devices_path = self.root_sys_dir.join(USB_DEVICES_PATH);
        for path in fs::read_dir(usb_devices_path)?.filter_map(|e| e.ok().map(|entry| entry.path()))
        {
            let name = if let Some(name) = path.file_name().and_then(|s| s.to_str()) {
                name
            } else {
                continue;
            };
            // Filter for device directories like "usbX"
            if !name.starts_with("usb")
                || name.len() <= 3
                || !name[3..].chars().all(|c| c.is_ascii_digit())
            {
                continue;
            }
            let auth_path = path.join("authorized_default");
            if auth_path.exists() {
                fs::write(&auth_path, "0").map_err(|e| {
                    log::error!(
                        "Failed to set authorized_default for device {}: {}",
                        auth_path.display(),
                        e
                    );
                    e
                })?;
            }
        }
        Ok(())
    }

    /// Sets the global usbcore authorized_default parameter to '0' (deny).
    fn set_module_default_to_deny(&self) -> Result<(), Error> {
        let path = self.root_sys_dir.join(USBCORE_AUTHORIZED_DEFAULT_PATH);
        if let Err(e) = fs::write(&path, "0") {
            log::error!(
                "Failed to set usbcore authorized_default parameter at {}: {}",
                path.display(),
                e
            );
            return Err(Error::Io(e));
        }
        Ok(())
    }

    /// Re-evaluates deferred devices when the system state changes.
    fn reevaluate_deferred_devices(&mut self) {
        let deferred = std::mem::take(&mut self.deferred_devices);
        for device in deferred {
            self.process_usb_device(device);
        }
    }

    /// Re-evaluates devices requiring user interaction when the system state changes.
    fn reevaluate_ask_devices(&mut self) {
        let ask = std::mem::take(&mut self.ask_devices);
        for device in ask {
            self.process_usb_device(device);
        }
    }

    fn reevaluate_allow_persisted_devices(&mut self) {
        let allow_persisted = std::mem::take(&mut self.allow_persisted_devices);
        for device in allow_persisted {
            self.process_usb_device(device);
        }
    }

    /// Re-evaluates authorized devices when the system state changes.
    fn reevaluate_authorized_devices(&mut self) {
        let processed = std::mem::take(&mut self.processed_devices);
        for device in processed {
            self.process_usb_device(device);
        }
    }

    /// Handles system state changes by re-evaluating all device lists.
    pub fn handle_system_state_change(&mut self, system_state: UsbAuthorizationSystemState) {
        if system_state == self.system_state {
            return;
        }
        self.system_state = system_state;
        // If the system state changes to booted, re-evaluate authorized devices. This is done
        // because the system state is set to booted  when the user logs out of the system.
        if system_state == UsbAuthorizationSystemState::BOOTED {
            self.reevaluate_authorized_devices();
        }
        self.reevaluate_deferred_devices();
        self.reevaluate_ask_devices();
        self.reevaluate_allow_persisted_devices();
    }

    /// Processes a newly added USB device, determines its authorization state, and adds it to the
    /// appropriate list within the `UsbDeviceManager`.
    pub fn process_usb_device(&mut self, mut device_with_state: UsbDeviceInfoWithState) {
        let action =
            self.authorizer.authorize_device(&device_with_state, &self.policy, self.system_state);
        device_with_state.authorized = action == Action::Allow;
        device_with_state.is_deferred = action == Action::Defer;
        match action {
            Action::Defer => {
                self.deferred_devices.push(device_with_state.clone());

                // We also send callbacks when deferring and we notify the client
                // of the underlying authorization state.
                for cb in &mut self.callbacks {
                    let status = if device_with_state.authorized {
                        UsbAuthorizationStatus::AUTHORIZED
                    } else {
                        UsbAuthorizationStatus::DENIED_AND_DEFERRED
                    };

                    cb.send_status_change(&device_with_state.info, &status, &self.system_state);
                }
            }
            Action::Ask => {
                self.ask_devices.push(device_with_state.clone());
                for cb in &mut self.callbacks {
                    cb.send_ask(&device_with_state.info);
                }
            }
            Action::AllowPersisted => {
                self.allow_persisted_devices.push(device_with_state.clone());
                for cb in &mut self.callbacks {
                    cb.send_allow_persisted(&device_with_state.info);
                }
            }
            _ => {
                self.processed_devices.push(device_with_state.clone());
                for cb in &mut self.callbacks {
                    let status = if device_with_state.authorized {
                        UsbAuthorizationStatus::AUTHORIZED
                    } else {
                        UsbAuthorizationStatus::DENIED
                    };

                    cb.send_status_change(&device_with_state.info, &status, &self.system_state);
                }
            }
        }
    }

    fn set_authorized_and_send_status_change(
        &mut self,
        mut device_with_state: UsbDeviceInfoWithState,
        status: UsbAuthorizationStatus,
    ) -> Result<(), Error> {
        let authorized: bool = status == UsbAuthorizationStatus::AUTHORIZED;
        self.authorizer.authorize_device_via_sysfs(&device_with_state.info.syspath, authorized)?;
        device_with_state.authorized = authorized;

        for cb in &mut self.callbacks {
            cb.send_status_change(&device_with_state.info, &status, &self.system_state);
        }

        match status {
            UsbAuthorizationStatus::DENIED_AND_DEFERRED => {
                self.deferred_devices.push(device_with_state)
            }
            _ => self.processed_devices.push(device_with_state),
        }
        Ok(())
    }

    /// Updates the authorization status of a device that is awaiting user authorization or
    /// one that is already processed.
    ///
    /// If the device is found in `ask_devices` or `processed_devices`, the authorization is set
    /// and the device is moved into the `processed_devices` list. We allow already processed
    /// devices to update status for more complex UI scenarios (i.e. revoke authorization if user
    /// does not finish logging in with new keyboard) and for testing.
    ///
    /// If the device is found in `allow_persisted_devices` list, it is moved to the
    /// `deferred_devices` list with its authorization status updated.
    ///
    /// # Returns
    ///
    /// * `Ok(())` if the device was found and updated.
    /// * `Err(Error::DeviceNotFound)` if the device was not found in any list.
    pub fn update_authorization_status(
        &mut self,
        device_syspath: &str,
        authorized: bool,
    ) -> Result<(), Error> {
        let mut status = if authorized {
            UsbAuthorizationStatus::AUTHORIZED
        } else {
            UsbAuthorizationStatus::DENIED
        };
        if let Some(pos) = self.ask_devices.iter().position(|d| d.info.syspath == device_syspath) {
            let device_with_state = self.ask_devices.remove(pos);
            self.set_authorized_and_send_status_change(device_with_state, status)
        } else if let Some(pos) =
            self.allow_persisted_devices.iter().position(|d| d.info.syspath == device_syspath)
        {
            let device_with_state = self.allow_persisted_devices.remove(pos);
            // Defer devices that were denied by `allow-persist`. They may be enabled by a
            // later rule.
            if !authorized {
                status = UsbAuthorizationStatus::DENIED_AND_DEFERRED;
            }
            self.set_authorized_and_send_status_change(device_with_state, status)
        } else if let Some(pos) =
            self.processed_devices.iter().position(|d| d.info.syspath == device_syspath)
        {
            let device_with_state = self.processed_devices.remove(pos);
            self.set_authorized_and_send_status_change(device_with_state, status)
        } else {
            Err(Error::DeviceNotFound(device_syspath.to_string()))
        }
    }

    /// Adds a new USB device to the manager.
    ///
    /// This function converts the provided `Device` into a `UsbDeviceInfoWithState`,
    /// processes it to determine its authorization state, and adds it to the
    /// appropriate internal list (`processed_devices`, `deferred_devices`, or `ask_devices`).
    ///
    /// # Arguments
    /// * `device` - A reference to the `Device` object representing the newly added USB device.
    ///
    /// # Returns
    /// * `Ok(())` if the device was successfully added and processed.
    /// * `Err(Error::DeviceNotFound)` if there was an issue getting device info from the `Device` object.
    pub fn add_usb_device(&mut self, device: &Device) -> Result<(), Error> {
        debug!("Add USB device: {:?}", device.name());
        if !device.syspath().exists() {
            debug!(
                "Device syspath {:?} does not exist, adding to pending list with 0 retries",
                device.syspath()
            );
            self.pending_devices.push((device.clone(), 0));
            self.notify.notify_one();
            return Ok(());
        }

        self.handle_device(device)
    }

    /// Helper function to process the result of `UsbDeviceInfoWithState::from_device`.
    fn handle_device(&mut self, device: &Device) -> Result<(), Error> {
        match UsbDeviceInfoWithState::from_device(device) {
            Ok(device_with_state) => {
                self.process_usb_device(device_with_state);
                Ok(())
            }
            Err(e) => {
                debug!("Failed to get UsbDeviceInfoWithState: {}", e);
                Err(Error::DeviceNotFound(device.syspath().display().to_string()))
            }
        }
    }

    /// Processes pending devices that were added to the pending list because their sysfs path
    /// did not exist.
    fn process_pending_devices(&mut self) -> bool {
        // Returns true if there are still pending devices after this process.
        let mut devices_to_process = Vec::new();

        self.pending_devices.retain_mut(|(device, retries)| {
            *retries += 1;
            if *retries > MAX_RETRY_ATTEMPTS {
                debug!(
                    "Giving up on pending device {:?} after {} attempts.",
                    device.syspath(),
                    MAX_RETRY_ATTEMPTS
                );
                return false;
            }
            if !device.syspath().exists() {
                return true; // Keep in pending for retry
            }

            devices_to_process.push(device.clone());
            false
        });

        for device in devices_to_process {
            // Ignore errors for now as they are logged in handle_device
            let _ = self.handle_device(&device);
        }
        !self.pending_devices.is_empty()
    }

    /// Periodically processes pending USB devices upon notification.
    ///
    /// This function waits for a notification from the manager, then enters a loop
    /// to process any pending devices. If devices remain pending (e.g., waiting for
    /// sysfs paths to appear), it sleeps for a short interval before retrying.
    pub async fn pending_devices_worker(manager: Arc<Mutex<Self>>) {
        let notify = manager.lock().unwrap().notify.clone();
        loop {
            notify.notified().await;
            loop {
                let has_pending = manager.lock().unwrap().process_pending_devices();
                if has_pending {
                    tokio::time::sleep(PENDING_DEVICE_CHECK_INTERVAL).await;
                } else {
                    break;
                }
            }
        }
    }

    /// Removes a USB device from all internal lists (`processed_devices`, `deferred_devices`,
    /// and `ask_devices`).
    ///
    /// This function is typically called when a USB device is disconnected from the system.
    ///
    /// # Arguments
    /// * `device` - The `Device` object representing the USB device to be removed.
    pub fn remove_usb_device(&mut self, device: &Device) -> Result<(), Error> {
        if let Some(device_syspath) = device.syspath().to_str() {
            self.pending_devices.retain(|(d, _)| d.syspath() != device.syspath());
            self.deferred_devices.retain(|d| d.info.syspath != device_syspath);
            self.ask_devices.retain(|d| d.info.syspath != device_syspath);
            self.allow_persisted_devices.retain(|d| d.info.syspath != device_syspath);

            // When removing an already processed device, also send an authorization denied
            // callback. This helps to avoid a race on the client side where authorization status
            // from this callback may come out-of-order with device removal.
            if let Some(pos) =
                self.processed_devices.iter().position(|d| d.info.syspath == device_syspath)
            {
                let device_with_state = self.processed_devices.remove(pos);
                if device_with_state.authorized {
                    let status = UsbAuthorizationStatus::DENIED;
                    for cb in &mut self.callbacks {
                        cb.send_status_change(&device_with_state.info, &status, &self.system_state);
                    }
                }
            }
            Ok(())
        } else {
            debug!("Failed to get syspath for device: {:?}", device.name());
            Err(Error::DeviceNotFound(device.syspath().display().to_string()))
        }
    }

    /// Registers callbacks for authorization events.
    ///
    /// # Arguments
    /// * `callback` - Boxed object that implements the necessary callback functions.
    ///
    /// # Returns
    /// * True if the callback was unique and registered.
    /// * False if the callback was already registered previously.
    pub fn register_callback(&mut self, callback: Box<dyn AuthEventsCallback>) -> bool {
        let unique = !self.callbacks.iter().any(|v| v.equals(callback.as_ref()));
        if unique {
            self.callbacks.push(callback);
        }

        unique
    }

    /// Unregisters a callback if it was previously registered.
    pub fn unregister_callback(&mut self, callback: Box<dyn AuthEventsCallback>) {
        self.callbacks.retain(|v| !v.equals(callback.as_ref()))
    }
}

/// Loads the list of internal devices from the given file path.
fn load_internal_devices_from_file(_path: &Path) -> Result<InternalDevices, Error> {
    // TODO: Implement device list parsing from the file.
    Ok(InternalDevices)
}

/// Creates a default policy allowing HID, HUB, Ethernet devices, and a specific Realtek USB Ethernet adapter.
fn create_default_policy() -> Policy {
    let mut policy = Policy::new();
    policy
        .add_rule(Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![
                        InterfaceType { class: 0x03, subclass: None, protocol: None }, // HID
                        InterfaceType { class: 0x09, subclass: None, protocol: None }, // HUB
                        // Communications and CDC Control (Ethernet)
                        InterfaceType { class: 0x02, subclass: None, protocol: None },
                    ],
                )),
                ..Default::default()
            }),
            condition: None, // No specific condition for this rule.
        })
        .expect("Failed to add default rule for HID, HUB, and Ethernet devices");
    policy
        .add_rule(Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_id: Some(DeviceId { vendor_id: Some(0x0bda), product_id: Some(0x8153) }),
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![InterfaceType { class: 0xff, subclass: None, protocol: None }],
                )),
                ..Default::default()
            }),
            condition: None, // No specific condition for this rule.
        })
        .expect("Failed to add default rule for Realtek USB Ethernet adapter");
    policy
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rules::Action;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
    use std::collections::HashMap;
    use std::fs;
    use tempfile::tempdir;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    fn init_logger() {
        let _ = env_logger::try_init();
    }

    fn create_mock_sysfs_for_init() -> MockSysfs {
        let mock = MockSysfs::new(SysfsFile::Dir(HashMap::from([
            (
                "module/usbcore/parameters",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "bus/usb/devices",
                SysfsFile::Dir(HashMap::from([
                    (
                        "usb1",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                    (
                        "usb2",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                    (
                        "1-1",
                        SysfsFile::Dir(HashMap::from([(
                            "authorized_default",
                            SysfsFile::RegularFile("1"),
                        )])),
                    ),
                ])),
            ),
            (
                "class/block",
                SysfsFile::Dir(HashMap::from([
                    (
                        "dm-7",
                        SysfsFile::Dir(HashMap::from([(
                            "slaves",
                            SysfsFile::Dir(HashMap::from([(
                                "dm-0",
                                SysfsFile::Symlink("../../dm-0"),
                            )])),
                        )])),
                    ),
                    (
                        "dm-0",
                        SysfsFile::Dir(HashMap::from([(
                            "slaves",
                            SysfsFile::Dir(HashMap::from([(
                                "sda3",
                                SysfsFile::Symlink("../../sda3"),
                            )])),
                        )])),
                    ),
                    (
                        "sda3",
                        SysfsFile::Symlink(
                            "../../devices/pci0000:00/0000:00:0d.0/\
                                                usb2/2-3/2-3:1.0/host0/\
                                                target0:0:0/0:0:0:0/block/sda/sda3",
                        ),
                    ),
                ])),
            ),
        ])))
        .unwrap();

        // Separately create the pci path -- MockSysfs has issues.
        let full_path = mock.path().join(
            "devices/pci0000:00/0000:00:0d.0/usb2/2-3/2-3:1.0/\
                host0/target0:0:0/0:0:0:0/block/sda/sda3",
        );
        let _ = fs::create_dir_all(&full_path);

        mock
    }

    fn create_boot_usb_device(root_path: &Path) -> UsbDeviceInfoWithState {
        UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo {
                syspath: root_path
                    .join("devices/pci0000:00/0000:00:0d.0/usb2/2-3")
                    .to_string_lossy()
                    .to_string(),
                ..Default::default()
            },
            interfaces: vec![],
            authorized: false,
            is_deferred: false,
        }
    }

    // Create proc structure using MockSysfs (no difference for testing).
    fn create_mock_proc_for_init() -> MockSysfs {
        // proc mount contents from a real system
        let proc_mount_contents = r#"
tmpfs /dev tmpfs rw,seclabel,nosuid,relatime,mode=755 0 0
devpts /dev/pts devpts rw,seclabel,relatime,mode=600,ptmxmode=000 0 0
proc /proc proc rw,relatime,gid=3009,hidepid=invisible 0 0
sysfs /sys sysfs rw,seclabel,relatime 0 0
selinuxfs /sys/fs/selinux selinuxfs rw,relatime 0 0
tmpfs /mnt tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755,gid=1000 0 0
/dev/block/sda17 /metadata ext4 rw,seclabel,nosuid,nodev,noatime,nodioread_nolock,discard,nodelalloc,commit=1,data=journal 0 0
/dev/block/dm-7 / erofs ro,seclabel,nodev,relatime,user_xattr,acl,cache_strategy=readaround 0 0
/dev/block/dm-8 /vendor erofs ro,seclabel,relatime,user_xattr,acl,cache_strategy=readaround 0 0                                                                                  /dev/block/dm-9 /system_dlkm erofs ro,seclabel,relatime,user_xattr,acl,cache_strategy=readaround 0 0
/dev/block/dm-10 /product erofs ro,seclabel,relatime,user_xattr,acl,cache_strategy=readaround 0 0                                                                                /dev/block/dm-11 /system_ext erofs ro,seclabel,relatime,user_xattr,acl,cache_strategy=readaround 0 0
/dev/block/dm-12 /vendor_dlkm erofs ro,seclabel,relatime,user_xattr,acl,cache_strategy=readaround 0 0
tmpfs /apex tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
tmpfs /linkerconfig tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
tmpfs /mnt/installer tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755,gid=1000 0 0
tmpfs /mnt/androidwritable tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755,gid=1000 0 0
none /dev/blkio cgroup rw,nosuid,nodev,noexec,relatime,blkio 0 0
none /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime,memory_recursiveprot 0 0
none /dev/cpuctl cgroup rw,nosuid,nodev,noexec,relatime,cpu 0 0
none /dev/cpuset cgroup rw,nosuid,nodev,noexec,relatime,cpuset,noprefix,cpuset_v2_mode 0 0
tmpfs /linkerconfig tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
tracefs /sys/kernel/tracing tracefs rw,seclabel,relatime 0 0
        "#;
        MockSysfs::new(SysfsFile::Dir(HashMap::from([(
            "mounts",
            SysfsFile::RegularFile(proc_mount_contents),
        )])))
        .unwrap()
    }

    #[test]
    fn with_paths_sets_deny_and_creates_default_policy() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();

        let authorized_default_path = mock_sys.path().join(USBCORE_AUTHORIZED_DEFAULT_PATH);
        assert_eq!(fs::read_to_string(authorized_default_path).unwrap(), "0");

        let usb1_auth_path = mock_sys.path().join("bus/usb/devices/usb1/authorized_default");
        assert_eq!(fs::read_to_string(usb1_auth_path).unwrap(), "0");
        let usb2_auth_path = mock_sys.path().join("bus/usb/devices/usb2/authorized_default");
        assert_eq!(fs::read_to_string(usb2_auth_path).unwrap(), "0");

        assert!(!manager.policy.all_rules.is_empty());
        assert_eq!(manager.policy.all_rules.len(), 2);
    }

    #[test]
    fn with_paths_loads_static_policy() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        fs::write(&policy_file, "allow with-interface any-of { 03:*:* }").unwrap();

        let manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();

        assert_eq!(manager.policy.all_rules.len(), 1);
        assert_eq!(manager.policy.all_rules[0].action, Action::Allow);
    }

    #[test]
    fn test_get_authorized_devices() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let mut manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();

        manager.processed_devices.push(UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "authorized".to_string(), ..Default::default() },
            interfaces: vec![],
            authorized: true,
            is_deferred: false,
        });
        manager.processed_devices.push(UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "unauthorized".to_string(), ..Default::default() },
            interfaces: vec![],
            authorized: false,
            is_deferred: false,
        });

        let authorized_devices = manager.get_authorized_devices();
        assert_eq!(authorized_devices.len(), 1);
        assert_eq!(authorized_devices[0].syspath, "authorized");
    }

    #[test]
    fn test_process_usb_device_moves_to_correct_list() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        // Defer everything in BOOTED state
        fs::write(&policy_file, "defer when Booted").unwrap();

        let mut manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();
        assert_eq!(manager.policy.all_rules.len(), 1);

        let device = UsbDeviceInfoWithState {
            info: UsbAuthDeviceInfo { syspath: "test_device".to_string(), ..Default::default() },
            interfaces: vec![],
            authorized: false,
            is_deferred: false,
        };

        manager.handle_system_state_change(UsbAuthorizationSystemState::BOOTED);
        manager.process_usb_device(device);

        assert!(manager.processed_devices().is_empty());
        assert_eq!(manager.deferred_devices().len(), 1);
        assert!(manager.ask_devices().is_empty());
        assert_eq!(manager.deferred_devices()[0].info.syspath, "test_device");
    }

    #[test]
    fn test_protect_boot_disk() {
        init_logger();
        let mock_sys = create_mock_sysfs_for_init();
        let mock_etc = tempdir().unwrap();
        let mock_proc = create_mock_proc_for_init();
        let mut manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();

        let path = manager.protect_boot_disk();
        let device = create_boot_usb_device(mock_sys.path());

        assert!(path.is_some());
        if let Some(p) = path {
            assert_eq!(p, device.info.syspath);
        }

        // Boot device should now be in authorizer's internal devices list.
        assert!(manager.authorizer.is_device_internal(&device.info));
    }
}
