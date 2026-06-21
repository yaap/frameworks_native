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

//! This crate provides a usb watcher that listens to ueventd events and logs usb device add/remove events.

use binder::ProcessState;
use log::{debug, error};
use rustutils::android::system_properties;
use std::sync::{Arc, Mutex};
use tokio_stream::{Stream, StreamExt};
use ueventd::device::Device;
use ueventd::device_node::watcher::Watcher;
use ueventd::event::{DeviceEvent, EventType};

use usbauthservice_core::manager::UsbDeviceAuthManager;
use usbauthservice_core::service::UsbAuthServiceImpl;

async fn handle_device_events(
    mut event_stream: impl Stream<Item = DeviceEvent> + Unpin,
    device_manager: Arc<Mutex<UsbDeviceAuthManager>>,
) {
    while let Some(event) = event_stream.next().await {
        if !is_device_usb(&event.device) {
            continue;
        }
        match event.event_type {
            EventType::Add => {
                handle_add_device(&event.device, device_manager.clone()).await;
            }
            EventType::Remove => {
                handle_remove_device(&event.device, device_manager.clone()).await;
            }
            EventType::Change => {
                // Not handled for now.
            }
        }
    }
}

/// Handles the addition of a new USB device.
async fn handle_add_device(device: &Device, device_manager: Arc<Mutex<UsbDeviceAuthManager>>) {
    debug!("USB device added: {:?}", device.name());
    let mut manager = device_manager.lock().unwrap();
    if let Err(e) = manager.add_usb_device(device) {
        error!("Failed to add device {:?}: {}", device.syspath(), e);
    }
}

/// Handles the removal of a USB device.
async fn handle_remove_device(device: &Device, device_manager: Arc<Mutex<UsbDeviceAuthManager>>) {
    debug!("USB device removed: {:?}", device.name());
    let mut manager = device_manager.lock().unwrap();
    if let Err(e) = manager.remove_usb_device(device) {
        error!("Failed to remove device {:?}: {}", device.syspath(), e);
    }
}

fn is_device_usb(device: &Device) -> bool {
    device.subsystem() == Some("usb".to_string())
}

// Read-only property designating whether the image is debuggable.
const RO_DEBUGGABLE: &str = "ro.debuggable";

/// Main function of the usb_auth crate.
#[tokio::main(flavor = "multi_thread", worker_threads = 2)]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    logger::init(
        logger::Config::default()
            .with_tag_on_device("usbauthservice")
            .with_max_level(log::LevelFilter::Debug),
    );
    debug!("UsbAuth service is starting...");
    let use_interactive_policy = usb_flags_lib_rust::enable_usb_host_authorization();
    let debuggable = system_properties::read_bool(RO_DEBUGGABLE, false).unwrap_or(false);
    let device_manager =
        Arc::new(Mutex::new(UsbDeviceAuthManager::new(use_interactive_policy, debuggable)?));

    let (mut watcher, event_stream) = Watcher::new().await?;

    tokio::spawn(async move {
        watcher.run_event_loop().await;
    });
    tokio::spawn(handle_device_events(event_stream, device_manager.clone()));

    tokio::spawn(UsbDeviceAuthManager::pending_devices_worker(device_manager.clone()));

    let service =
        UsbAuthServiceImpl::new_binder(device_manager).expect("Failed to create binder service");
    let service_name = "usb_auth"; // Must match Context.USB_AUTH_MANAGER_SERVICE
    binder::add_service(service_name, service).expect("Failed to register usb_auth service");
    debug!("Successfully registered service '{}'", service_name);
    debug!("UsbAuth service is ready.");
    // Join the thread pool to keep the service alive.
    ProcessState::join_thread_pool();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::fs;
    use tempfile::tempdir;
    use ueventd::device::Device;
    use ueventd::mock_sysfs::{MockSysfs, SysfsFile};
    use usbauthservice_core::manager::UsbDeviceAuthManager;

    // Creates a mock sysfs with a single USB device for testing.
    fn create_mock_sysfs_with_device(
        name: &'static str,
        id_vendor: &'static str,
        id_product: &'static str,
        if_class: &'static str,
    ) -> MockSysfs {
        let vendor_id = u16::from_str_radix(id_vendor, 16).unwrap();
        let product_id = u16::from_str_radix(id_product, 16).unwrap();
        let class_id = u16::from_str_radix(if_class, 16).unwrap() as u8;

        let mut descriptors = vec![
            0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x40, // Device Descriptor header
        ];
        descriptors.extend_from_slice(&vendor_id.to_le_bytes());
        descriptors.extend_from_slice(&product_id.to_le_bytes());
        descriptors.extend_from_slice(&[0x00, 0x02, 0x01, 0x02, 0x03, 0x01]); // bcdDevice, iManufacturer, iProduct, iSerial, bNumConfigs

        // Configuration Descriptor
        descriptors.extend_from_slice(&[0x09, 0x02, 0x19, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32]);

        // Interface Descriptor
        descriptors.extend_from_slice(&[0x09, 0x04, 0x00, 0x00, 0x00, class_id, 0x00, 0x00, 0x00]);

        let interface_name = format!("{}:1.0", name);
        let leaked_interface_name = Box::leak(interface_name.into_boxed_str());
        let device_files = HashMap::from([
            ("idVendor", SysfsFile::RegularFile(id_vendor)),
            ("idProduct", SysfsFile::RegularFile(id_product)),
            ("bDeviceClass", SysfsFile::RegularFile("00")),
            ("bcdDevice", SysfsFile::RegularFile("0200")),
            ("bDeviceSubClass", SysfsFile::RegularFile("00")),
            ("bDeviceProtocol", SysfsFile::RegularFile("00")),
            ("serial", SysfsFile::RegularFile("12345678")),
            ("manufacturer", SysfsFile::RegularFile("Google")),
            ("product", SysfsFile::RegularFile("Mock USB Device")),
            ("bInterfaceClass", SysfsFile::RegularFile(if_class)),
            ("bInterfaceSubClass", SysfsFile::RegularFile("00")),
            ("bInterfaceProtocol", SysfsFile::RegularFile("00")),
            ("bInterfaceNumber", SysfsFile::RegularFile("00")),
            (
                leaked_interface_name,
                SysfsFile::Dir(HashMap::from([
                    ("bInterfaceClass", SysfsFile::RegularFile(if_class)),
                    ("bInterfaceSubClass", SysfsFile::RegularFile("00")),
                    ("bInterfaceProtocol", SysfsFile::RegularFile("00")),
                    ("bInterfaceNumber", SysfsFile::RegularFile("00")),
                ])),
            ),
            ("subsystem", SysfsFile::Symlink("../../../../bus/usb")),
            ("uevent", SysfsFile::RegularFile("DEVTYPE=usb_device\nSUBSYSTEM=usb")),
        ]);

        let mock = MockSysfs::new(SysfsFile::Dir(HashMap::from([
            (
                "module/usbcore/parameters",
                SysfsFile::Dir(HashMap::from([(
                    "authorized_default",
                    SysfsFile::RegularFile("1"),
                )])),
            ),
            (
                "bus",
                SysfsFile::Dir(HashMap::from([(
                    "usb",
                    SysfsFile::Dir(HashMap::from([(
                        "devices",
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
                    )])),
                )])),
            ),
        ])))
        .unwrap();

        let descriptors_path = mock.path().join(format!("bus/usb/devices/{}/descriptors", name));
        fs::write(descriptors_path, descriptors).unwrap();

        mock
    }

    // The tests for UsbDeviceAuthManager::with_paths and test_get_device_authorization_flags are now in manager module tests.

    #[tokio::test]
    async fn test_handle_add_device_allow() {
        let mock_sys = create_mock_sysfs_with_device("1-1", "1234", "5678", "03"); // HID
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
        let manager = Arc::new(Mutex::new(manager));

        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();

        handle_add_device(&device, manager.clone()).await;

        let locked_manager = manager.lock().unwrap();
        assert_eq!(locked_manager.processed_devices().len(), 1);
        assert!(locked_manager.processed_devices()[0].authorized);
        assert_eq!(locked_manager.deferred_devices().len(), 0);
        assert_eq!(locked_manager.ask_devices().len(), 0);
    }

    #[tokio::test]
    async fn test_handle_add_device_deny() {
        let mock_sys = create_mock_sysfs_with_device("1-1", "1234", "5678", "08"); // Mass Storage
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
        let manager = Arc::new(Mutex::new(manager));

        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();

        handle_add_device(&device, manager.clone()).await;

        let locked_manager = manager.lock().unwrap();
        assert_eq!(locked_manager.processed_devices().len(), 1);
        assert!(!locked_manager.processed_devices()[0].authorized);
        assert_eq!(locked_manager.deferred_devices().len(), 0);
        assert_eq!(locked_manager.ask_devices().len(), 0);
    }

    #[tokio::test]
    async fn test_handle_add_device_defer() {
        let mock_sys = create_mock_sysfs_with_device("1-1", "1234", "5678", "08"); // Mass Storage
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        fs::write(&policy_file, "defer with-interface any-of { 08:*:* }").unwrap();

        let manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();
        let manager = Arc::new(Mutex::new(manager));

        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();

        handle_add_device(&device, manager.clone()).await;

        let locked_manager = manager.lock().unwrap();
        assert_eq!(locked_manager.processed_devices().len(), 0);
        assert_eq!(locked_manager.deferred_devices().len(), 1);
        assert!(locked_manager.deferred_devices()[0].is_deferred);
        assert_eq!(locked_manager.ask_devices().len(), 0);
    }

    #[tokio::test]
    async fn test_handle_add_device_ask() {
        let mock_sys = create_mock_sysfs_with_device("1-1", "1234", "5678", "08"); // Mass Storage
        let mock_etc = tempdir().unwrap();
        let mock_proc = tempdir().unwrap();
        let policy_dir = mock_etc.path().join("usb_auth");
        fs::create_dir(&policy_dir).unwrap();
        let policy_file = policy_dir.join("policy.conf");
        fs::write(&policy_file, "ask with-interface any-of { 08:*:* }").unwrap();

        let manager = UsbDeviceAuthManager::with_paths(
            mock_sys.path(),
            mock_etc.path(),
            mock_proc.path(),
            false,
            false,
        )
        .unwrap();
        let manager = Arc::new(Mutex::new(manager));

        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();

        handle_add_device(&device, manager.clone()).await;

        let locked_manager = manager.lock().unwrap();
        assert_eq!(locked_manager.processed_devices().len(), 0);
        assert_eq!(locked_manager.deferred_devices().len(), 0);
        assert_eq!(locked_manager.ask_devices().len(), 1);
    }

    #[tokio::test]
    async fn test_handle_remove_device() {
        let mock_sys = create_mock_sysfs_with_device("1-1", "1234", "5678", "03"); // HID
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
        let manager = Arc::new(Mutex::new(manager));

        let device_path = mock_sys.path().join("bus/usb/devices/1-1");
        let device = Device::with_root_and_syspath(mock_sys.path(), &device_path).unwrap();

        // Add the device
        handle_add_device(&device, manager.clone()).await;
        {
            let locked_manager = manager.lock().unwrap();
            assert_eq!(locked_manager.processed_devices().len(), 1);
        }

        // Remove the device
        handle_remove_device(&device, manager.clone()).await;
        let locked_manager = manager.lock().unwrap();
        assert_eq!(locked_manager.processed_devices().len(), 0);
        assert_eq!(locked_manager.deferred_devices().len(), 0);
        assert_eq!(locked_manager.ask_devices().len(), 0);
    }
}
