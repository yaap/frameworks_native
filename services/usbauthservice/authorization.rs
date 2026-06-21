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

//! This module implements the core logic for USB device authorization.
//! It evaluates devices against a given policy and system state to determine
//! the appropriate action, such as allowing, denying, or deferring access.

use crate::device_info::AuthorizationError;
use crate::device_info::UsbDeviceInfoWithState;
use crate::rules::{Action, DeviceId, InterfaceType, Policy, Rule};
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use log::{debug, error};
use std::collections::HashSet;
use std::fs;
use std::path::Path;

/// Handles all authorization activity for the system.
pub struct Authorizer {
    /// List of internal devices to compare against (on top of sysfs).
    internal_devices: HashSet<String>,
}

impl Authorizer {
    /// Construct an |Authorizer| with an empty list of internal devices.
    pub fn new() -> Self {
        Self { internal_devices: HashSet::new() }
    }

    /// Add a sysfs path to the set of internal devices.
    pub fn add_internal_device(&mut self, sysfs_path: String) {
        self.internal_devices.insert(sysfs_path);
    }

    /// Authorizes or deauthorizes a device by writing to its 'authorized' sysfs node.
    pub fn authorize_device_via_sysfs(
        &self,
        syspath: &str,
        authorize: bool,
    ) -> Result<(), AuthorizationError> {
        debug!("AuthorizeDeviceViaSysfs called for device {} with level {}", syspath, authorize);
        let auth_path = Path::new(syspath).join("authorized");

        if !auth_path.exists() {
            return Err(AuthorizationError::AuthorizationPathNotFound(
                auth_path.display().to_string(),
            ));
        }

        let access_level = if authorize { "1" } else { "0" };
        fs::write(&auth_path, access_level)
            .map_err(|source| AuthorizationError::Io { syspath: syspath.to_string(), source })
    }

    /// Determines the authorization action for a given device based on the policy and system state.
    ///
    /// It iterates through the rules applicable to the current system state, and the first
    /// matching rule determines the action. If no rules match, the device is denied.
    pub fn authorize_device(
        &self,
        device_with_state: &UsbDeviceInfoWithState,
        policy: &Policy,
        state: UsbAuthorizationSystemState,
    ) -> Action {
        if let Some(rules) = policy.rules_by_state.get(&state) {
            for rule in rules {
                if self.device_matches_rule(device_with_state, rule) {
                    debug!(
                        "Device {:?} {:?} by policy",
                        &device_with_state.info.productName, rule.action
                    );
                    debug!("USB device info with state: {:#?}", device_with_state);
                    match rule.action {
                        // For these actions, apply the authorized value right away.
                        // In the case of Defer, we will keep denying it until it is approved (as
                        // devices should be DENIED_AND_DEFERRED).
                        Action::Allow | Action::Defer | Action::Deny | Action::Remove => {
                            let authorize = rule.action == Action::Allow;

                            if let Err(e) = self.authorize_device_via_sysfs(
                                &device_with_state.info.syspath,
                                authorize,
                            ) {
                                error!(
                                    "Failed to set authorization for device {}: {}",
                                    device_with_state.info.syspath, e
                                );
                            }
                        }

                        // Do nothing as a client needs to tell us what action to take.
                        Action::AllowPersisted | Action::Ask => (),
                    }

                    return rule.action.clone();
                }
            }
        }

        // If no matching policy is found, use the default rule if it exists. Otherwise, deny the
        // device.
        let default_action = if let Some(default_rule) = &policy.default_rule {
            default_rule.action.clone()
        } else {
            Action::Deny
        };

        let authorize = default_action == Action::Allow;
        if let Err(e) = self.authorize_device_via_sysfs(&device_with_state.info.syspath, authorize)
        {
            error!(
                "Failed to set authorization for device {}: {}",
                device_with_state.info.syspath, e
            );
        }
        default_action
    }

    fn device_matches_rule(&self, device_with_state: &UsbDeviceInfoWithState, rule: &Rule) -> bool {
        let Some(attributes) = &rule.attributes else {
            return true; // No attributes means it matches all devices.
        };

        if let Some(name) = &attributes.name {
            if device_with_state.info.productName != *name {
                return false;
            }
        }

        if let Some(serial_rule) = &attributes.serial {
            if device_with_state.info.serialNumber != *serial_rule {
                return false;
            }
        }

        if let Some(rule_device_id) = &attributes.with_id {
            if *rule_device_id
                != (DeviceId {
                    vendor_id: Some(device_with_state.info.vendorId as u16),
                    product_id: Some(device_with_state.info.productId as u16),
                })
            {
                return false;
            }
        }

        if attributes.internal_device == Some(true)
            && !self.is_device_internal(&device_with_state.info)
        {
            return false;
        }

        // Using `with_interface` from the unwrapped `attributes`
        if let Some(interface_attr) = &attributes.with_interface {
            if !interface_attr.matches_device_interfaces(&device_with_state.interfaces) {
                // Also check against device class attributes if interface doesn't match
                if !interface_attr.matches_device_interfaces(&[InterfaceType {
                    class: device_with_state.info.bDeviceClass as u8,
                    subclass: Some(device_with_state.info.bDeviceSubClass as u8),
                    protocol: Some(device_with_state.info.bDeviceProtocol as u8),
                }]) {
                    return false;
                }
            }
        }

        true
    }

    pub(crate) fn is_device_internal(&self, device: &UsbAuthDeviceInfo) -> bool {
        if self.internal_devices.contains(&device.syspath) {
            return true;
        }

        // Check sysfs as back-up if we don't have it in internals list.
        let removable_path = Path::new(&device.syspath).join("removable");
        if let Ok(content) = fs::read_to_string(removable_path) {
            return content.trim() == "fixed";
        }
        false
    }
}

impl Default for Authorizer {
    fn default() -> Self {
        Authorizer::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rules::{DeviceAttributes, InterfaceAttribute};
    use std::collections::HashMap;

    fn create_test_device(
        class: i8,
        subclass: i8,
        protocol: i8,
        product_name: &str,
    ) -> UsbAuthDeviceInfo {
        UsbAuthDeviceInfo {
            syspath: "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1".to_string(),
            productName: product_name.to_string(),
            bInterfaceClass: class,
            bInterfaceSubClass: subclass,
            bInterfaceProtocol: protocol,
            ..Default::default()
        }
    }

    fn create_internal_test_device(
        class: i8,
        subclass: i8,
        protocol: i8,
        product_name: &str,
    ) -> UsbAuthDeviceInfo {
        UsbAuthDeviceInfo {
            syspath: "/sys/devices/pci0000:00/0000:00:14.0/usb2/2-3".to_string(),
            productName: product_name.to_string(),
            bInterfaceClass: class,
            bInterfaceSubClass: subclass,
            bInterfaceProtocol: protocol,
            ..Default::default()
        }
    }

    fn create_test_device_with_state(
        class: i8,
        subclass: i8,
        protocol: i8,
        product_name: &str,
    ) -> UsbDeviceInfoWithState {
        UsbDeviceInfoWithState {
            info: create_test_device(class, subclass, protocol, product_name),
            interfaces: vec![InterfaceType {
                class: class as u8,
                subclass: Some(subclass as u8),
                protocol: Some(protocol as u8),
            }],
            authorized: false,
            is_deferred: false,
        }
    }

    fn create_authorizer() -> Authorizer {
        let internal_device = create_internal_test_device(0, 0, 0, "Fake");
        let mut authorizer = Authorizer::new();
        authorizer.add_internal_device(internal_device.syspath);

        authorizer
    }

    #[test]
    fn test_device_matches_rule_interface() {
        let authorizer = create_authorizer();
        let device = create_test_device_with_state(0x08, 0x06, 0x50, "USB Stick");

        let matching_rule = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                internal_device: None,
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![InterfaceType { class: 0x08, subclass: Some(0x06), protocol: None }],
                )),
                ..Default::default()
            }),
            condition: None,
        };
        assert!(authorizer.device_matches_rule(&device, &matching_rule));

        let non_matching_rule = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                internal_device: None,
                with_interface: Some(InterfaceAttribute::new(
                    None,
                    vec![InterfaceType { class: 0x03, subclass: None, protocol: None }],
                )),
                ..Default::default()
            }),
            condition: None,
        };
        assert!(!authorizer.device_matches_rule(&device, &non_matching_rule));
    }

    #[test]
    fn test_authorize_device() {
        let authorizer = create_authorizer();
        let hid_device = create_test_device_with_state(0x03, 0, 0, "Mouse");
        let storage_device = create_test_device_with_state(0x08, 0, 0, "Storage");

        let rules = vec![
            Rule {
                action: Action::Allow,
                attributes: Some(DeviceAttributes {
                    internal_device: None,
                    with_interface: Some(InterfaceAttribute::new(
                        None,
                        vec![InterfaceType { class: 0x03, subclass: None, protocol: None }],
                    )),
                    ..Default::default()
                }),
                condition: None,
            },
            Rule {
                action: Action::Ask,
                attributes: Some(DeviceAttributes {
                    internal_device: None,
                    with_interface: Some(InterfaceAttribute::new(
                        None,
                        vec![InterfaceType { class: 0x08, subclass: None, protocol: None }],
                    )),
                    ..Default::default()
                }),
                condition: None,
            },
        ];

        let policy = Policy {
            all_rules: rules.clone(),
            rules_by_state: HashMap::from([(UsbAuthorizationSystemState::BOOTED, rules)]),
            default_rule: None,
        };

        assert_eq!(
            authorizer.authorize_device(&hid_device, &policy, UsbAuthorizationSystemState::BOOTED),
            Action::Allow
        );
        assert_eq!(
            authorizer.authorize_device(
                &storage_device,
                &policy,
                UsbAuthorizationSystemState::BOOTED
            ),
            Action::Ask
        );

        let other_device = create_test_device_with_state(-1, 0, 0, "Other");
        assert_eq!(
            authorizer.authorize_device(
                &other_device,
                &policy,
                UsbAuthorizationSystemState::BOOTED
            ),
            Action::Deny
        );

        assert_eq!(
            authorizer.authorize_device(
                &hid_device,
                &policy,
                UsbAuthorizationSystemState::SCREEN_LOCKED
            ),
            Action::Deny
        );
    }

    #[test]
    fn test_check_internal() {
        let authorizer = create_authorizer();
        let internal_device = create_internal_test_device(0, 0, 0, "Internal");
        let external_device = create_test_device(0, 0, 0, "External");

        assert!(authorizer.is_device_internal(&internal_device));
        assert!(!authorizer.is_device_internal(&external_device));
    }
}
