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

//! This module provides structures and functions for managing USB device information
//! and authorization state. It includes a representation of a USB device that combines
//! sysfs information with its current authorization status.

use crate::rules::InterfaceType;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use thiserror::Error;
use ueventd::device::Device;

/// Custom error type for device information parsing.
#[derive(Error, Debug)]
pub enum DeviceInfoError {
    /// An I/O error occurred, typically when reading sysfs attributes.
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    /// An error occurred while parsing an integer value from a sysfs attribute.
    #[error("Failed to parse integer for attribute '{attribute}' with value '{value}': {source}")]
    ParseInt {
        /// The name of the attribute that caused the parsing error.
        attribute: String,
        /// The string value that failed to parse.
        value: String,
        /// The underlying `ParseIntError`.
        #[source]
        source: std::num::ParseIntError,
    },
    /// A sysfs attribute was found but its value was malformed or unexpected.
    #[error("Malformed attribute '{attribute}' with value '{value}'")]
    MalformedAttribute {
        /// The name of the attribute that was malformed.
        attribute: String,
        /// The malformed string value.
        value: String,
    },
    /// Failed to parse descriptors file.
    #[error("Failed to parse descriptors: {0}")]
    DescriptorParseError(String),
}

/// Custom error type for device authorization.
#[derive(Error, Debug)]
pub enum AuthorizationError {
    /// An I/O error occurred during authorization (e.g., writing to sysfs).
    #[error("I/O error during authorization for device '{syspath}': {source}")]
    Io {
        /// The sysfs path of the device that caused the I/O error.
        syspath: String,
        /// The underlying `std::io::Error`.
        #[source]
        source: std::io::Error,
    },
    /// The authorization sysfs path does not exist for the device.
    #[error("Authorization path '{0}' does not exist.")]
    AuthorizationPathNotFound(String),
}

/// Helper struct to hold parsed descriptor info
#[derive(Default)]
struct ParsedDescriptorInfo {
    vendor_id: i32,
    product_id: i32,
    bcd_device: i32,
    device_class: i8,
    device_sub_class: i8,
    device_protocol: i8,
    interfaces: Vec<ParsedInterfaceInfo>,
}

#[derive(Default)]
struct ParsedInterfaceInfo {
    interface_class: u8,
    interface_sub_class: u8,
    interface_protocol: u8,
    interface_number: i8,
}

/// Represents a USB device with its authorization state.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct UsbDeviceInfoWithState {
    /// The core USB device information, typically from sysfs attributes.
    pub info: UsbAuthDeviceInfo,
    /// Parsed interface information.
    pub interfaces: Vec<InterfaceType>,
    /// Current authorization status (mutable). True if authorized, false otherwise.
    pub authorized: bool,
    /// Whether the device is currently in the defer list (mutable).
    /// A deferred device is one where an authorization decision was not made and will need to be
    /// re-evaluated when the system conditions change. Defer can be the result of both an explicit
    /// "Defer" action as well as an Ask request returning a negative result.
    pub is_deferred: bool,
}

impl UsbDeviceInfoWithState {
    /// Creates a `UsbDeviceInfoWithState` from a `ueventd::device::Device`.
    ///
    /// This function reads various sysfs attributes to populate the `UsbAuthDeviceInfo`
    /// and initializes `authorized` and `is_deferred` to `false`.
    /// It returns an error if essential sysfs attributes cannot be read or parsed.
    ///
    /// For simple devices with a single interface, the interface info is part of the
    /// the `UsbAuthDeviceInfo` data. However, for composite devices, `UsbAuthDeviceInfo` will
    /// have empty interface information and the full interface list will be provided for the
    /// first configuration listed.
    #[allow(dead_code)]
    pub fn from_device(device: &Device) -> Result<Self, DeviceInfoError> {
        let serial_number = Self::get_string_sysattr(device, "serial");
        let manufacturer = Self::get_string_sysattr(device, "manufacturer");
        let product_name = Self::get_string_sysattr(device, "product");
        let busnum = Self::get_u16_sysattr(device, "busnum")?;
        let devnum = Self::get_u16_sysattr(device, "devnum")?;

        // Read and parse the descriptors file
        let descriptors_path = device.syspath().join("descriptors");
        let descriptors = std::fs::read(&descriptors_path)?;
        let parsed_info = Self::parse_descriptors(&descriptors)?;

        let interfaces = parsed_info
            .interfaces
            .iter()
            .map(|i| InterfaceType {
                class: i.interface_class,
                subclass: Some(i.interface_sub_class),
                protocol: Some(i.interface_protocol),
            })
            .collect::<Vec<InterfaceType>>();

        let device = if parsed_info.interfaces.is_empty() || parsed_info.interfaces.len() > 1 {
            Self {
                info: UsbAuthDeviceInfo {
                    syspath: device.syspath().to_string_lossy().to_string(),
                    busNumber: busnum as i32,
                    deviceNumber: devnum as i32,
                    vendorId: parsed_info.vendor_id,
                    productId: parsed_info.product_id,
                    deviceClass: parsed_info.device_class,
                    serialNumber: serial_number.clone(),
                    manufacturer: manufacturer.clone(),
                    productName: product_name.clone(),
                    bcdDevice: parsed_info.bcd_device,
                    bDeviceClass: parsed_info.device_class,
                    bDeviceSubClass: parsed_info.device_sub_class,
                    bDeviceProtocol: parsed_info.device_protocol,
                    bInterfaceClass: 0,
                    bInterfaceSubClass: 0,
                    bInterfaceProtocol: 0,
                    bInterfaceNumber: 0,
                },
                interfaces,
                authorized: false,
                is_deferred: false,
            }
        } else {
            let interface = &parsed_info.interfaces[0];
            Self {
                info: UsbAuthDeviceInfo {
                    syspath: device.syspath().to_string_lossy().to_string(),
                    busNumber: busnum as i32,
                    deviceNumber: devnum as i32,
                    vendorId: parsed_info.vendor_id,
                    productId: parsed_info.product_id,
                    deviceClass: parsed_info.device_class,
                    serialNumber: serial_number.clone(),
                    manufacturer: manufacturer.clone(),
                    productName: product_name.clone(),
                    bcdDevice: parsed_info.bcd_device,
                    bDeviceClass: parsed_info.device_class,
                    bDeviceSubClass: parsed_info.device_sub_class,
                    bDeviceProtocol: parsed_info.device_protocol,
                    bInterfaceClass: interface.interface_class as i8,
                    bInterfaceSubClass: interface.interface_sub_class as i8,
                    bInterfaceProtocol: interface.interface_protocol as i8,
                    bInterfaceNumber: interface.interface_number,
                },
                interfaces,
                authorized: false,
                is_deferred: false,
            }
        };

        Ok(device)
    }

    /// Helper to get a sysfs attribute as a string, defaulting to "" if missing.
    fn get_string_sysattr(device: &Device, attr_name: &str) -> String {
        device.sysattrs().get(attr_name).ok().unwrap_or_default().to_string()
    }

    /// Helper to get and parse a sysfs attribute as a u16 from decimal, defaulting to 0 if missing.
    /// Returns an error if the attribute is present but malformed.
    fn get_u16_sysattr(device: &Device, attr_name: &str) -> Result<u16, DeviceInfoError> {
        let Ok(s) = device.sysattrs().get(attr_name) else {
            return Ok(0);
        };
        s.trim().parse::<u16>().map_err(|e| DeviceInfoError::ParseInt {
            attribute: attr_name.to_string(),
            value: s.to_string(),
            source: e,
        })
    }

    /// Parses the raw descriptors bytes to extract device info.
    fn parse_descriptors(descriptors: &[u8]) -> Result<ParsedDescriptorInfo, DeviceInfoError> {
        let mut info = ParsedDescriptorInfo::default();
        let mut i = 0;
        let len = descriptors.len();
        let mut device_found = false;

        const TYPE_DEVICE: u8 = 0x01;
        const TYPE_CONFIGURATION: u8 = 0x02;
        const TYPE_INTERFACE: u8 = 0x04;

        // TODO(b/432527670) - Instead of accessing the raw slice by indices, re-write by defining
        // the structs (as #[repr(packed)]) and parsing into each of them.
        while i < len {
            if i + 2 > len {
                return Err(DeviceInfoError::DescriptorParseError(format!(
                    "Not enough bytes for header at byte 0x{:x}",
                    i
                )));
            }
            let b_length = descriptors[i] as usize;
            let b_descriptor_type = descriptors[i + 1];

            if b_length < 2 || i + b_length > len {
                return Err(DeviceInfoError::DescriptorParseError(format!(
                    "Invalid length specified at byte 0x{:x}: {}",
                    i, b_length
                )));
            }

            match b_descriptor_type {
                TYPE_DEVICE => {
                    // Device Descriptor
                    if b_length >= 18 {
                        device_found = true;

                        info.device_class = descriptors[i + 4] as i8;
                        info.device_sub_class = descriptors[i + 5] as i8;
                        info.device_protocol = descriptors[i + 6] as i8;

                        info.vendor_id =
                            i32::from_le_bytes([descriptors[i + 8], descriptors[i + 9], 0, 0]);
                        info.product_id =
                            i32::from_le_bytes([descriptors[i + 10], descriptors[i + 11], 0, 0]);
                        info.bcd_device =
                            i32::from_le_bytes([descriptors[i + 12], descriptors[i + 13], 0, 0]);
                    }
                }
                TYPE_CONFIGURATION => {
                    // Descriptors group interface definitions per configuration.
                    //
                    // If we have already parsed interfaces, then we have done so for the first
                    // available configuration with interfaces.
                    //
                    // To simplify our logic, we ignore subsequent configurations and only look at
                    // the first configuration available with interfaces.
                    if !info.interfaces.is_empty() {
                        break;
                    }
                }
                TYPE_INTERFACE => {
                    // Interface Descriptor
                    if b_length >= 9 {
                        info.interfaces.push(ParsedInterfaceInfo {
                            interface_number: descriptors[i + 2] as i8,
                            interface_class: descriptors[i + 5],
                            interface_sub_class: descriptors[i + 6],
                            interface_protocol: descriptors[i + 7],
                        });
                    }
                }
                _ => {}
            }

            i += b_length;
        }

        if !device_found {
            Err(DeviceInfoError::DescriptorParseError(
                "Descriptor is missing device descriptor".into(),
            ))
        } else {
            Ok(info)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use ueventd::device::Device;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};

    // Creates a mock sysfs with a single USB device for testing.
    fn create_mock_sysfs_with_device(name: &'static str, descriptors_data: &[u8]) -> MockSysfs {
        let interface_name = format!("{}:1.0", name);
        let leaked_interface_name = Box::leak(interface_name.into_boxed_str());
        let device_files = HashMap::from([
            ("busnum", SysfsFile::RegularFile("1")),
            ("devnum", SysfsFile::RegularFile("2")),
            ("serial", SysfsFile::RegularFile("12345")),
            ("manufacturer", SysfsFile::RegularFile("Google")),
            ("product", SysfsFile::RegularFile("Mock USB Device")),
            (
                leaked_interface_name,
                SysfsFile::Dir(HashMap::from([
                    // Interface attributes are now read from descriptors, so these sysfs files are ignored by from_device
                ])),
            ),
            ("subsystem", SysfsFile::Symlink("../../../../bus/usb")),
            ("uevent", SysfsFile::RegularFile("DEVTYPE=usb_device\nSUBSYSTEM=usb")),
        ]);

        let mock = MockSysfs::new(SysfsFile::Dir(HashMap::from([(
            "bus/usb/devices",
            SysfsFile::Dir(HashMap::from([
                (
                    "usb1",
                    SysfsFile::Dir(HashMap::from([(
                        "authorized_default",
                        SysfsFile::RegularFile("1"),
                    )])),
                ),
                (name, SysfsFile::Dir(device_files)),
            ])),
        )])))
        .unwrap();

        // Manual write of descriptors file since MockSysfs only supports strings
        let descriptors_path = mock.path().join(format!("bus/usb/devices/{}/descriptors", name));
        std::fs::write(descriptors_path, descriptors_data).unwrap();

        mock
    }

    #[test]
    fn test_from_device_descriptors() -> Result<(), DeviceInfoError> {
        // Device Descriptor: 18 bytes, type 01
        // 12 01 00 02 09 00 00 40 d1 18 e7 4e 23 02 01 02 03 01
        // Vendor: 18d1, Product: 4ee7, Class: 09, Sub: 00, Proto: 00

        // Config Descriptor: 9 bytes, type 02
        // 09 02 ...

        // Interface Descriptor: 9 bytes, type 04
        // 09 04 00 00 02 03 01 02 00
        // Num: 00, Alt: 00, Endpoints: 02, Class: 03, Sub: 01, Proto: 02

        let descriptors: [u8; 36] = [
            0x12, 0x01, 0x00, 0x02, 0x09, 0x00, 0x00, 0x40, 0xd1, 0x18, 0xe7, 0x4e, 0x23, 0x02,
            0x01, 0x02, 0x03, 0x01, 0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32, 0x09,
            0x04, 0x00, 0x00, 0x02, 0x03, 0x01, 0x02, 0x00,
        ];

        let mock_sys = create_mock_sysfs_with_device("1-1", &descriptors);
        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();
        let device_info = UsbDeviceInfoWithState::from_device(&device)?;

        assert_eq!(device_info.info.busNumber, 1);
        assert_eq!(device_info.info.deviceNumber, 2);
        assert_eq!(device_info.info.vendorId, 0x18d1);
        assert_eq!(device_info.info.productId, 0x4ee7);
        assert_eq!(device_info.info.deviceClass, 0x09);
        assert_eq!(device_info.info.serialNumber, "12345");
        assert_eq!(device_info.info.manufacturer, "Google");
        assert_eq!(device_info.info.productName, "Mock USB Device");
        assert_eq!(device_info.info.bcdDevice, 0x0223);

        assert_eq!(device_info.info.bInterfaceClass, 0x03);
        assert_eq!(device_info.info.bInterfaceSubClass, 0x01);
        assert_eq!(device_info.info.bInterfaceProtocol, 0x02);
        assert_eq!(device_info.info.bInterfaceNumber, 0x00);

        Ok(())
    }
}
