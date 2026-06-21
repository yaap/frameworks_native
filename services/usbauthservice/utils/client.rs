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

//! A debug client to interact with the UsbAuthService.
use android_hardware_usb_auth::aidl::android::hardware::usb::IUsbAuthManager::IUsbAuthManager;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthDeviceInfo::UsbAuthDeviceInfo;
use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use binder::{get_interface, ProcessState};
use clap::{Parser, Subcommand, ValueEnum};
use log::info;

/// A debug client to interact with the UsbAuthService.
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize logging for the client.
    logger::init(
        logger::Config::default()
            .with_tag_on_device("usbauthservice_client")
            .with_max_level(log::LevelFilter::Debug),
    );

    let cli = Cli::parse();

    info!("Starting UsbAuth client...");

    // Start the binder thread pool. This is necessary for the client to be a well-behaved binder citizen.
    ProcessState::start_thread_pool();

    let service_name = "usb_auth";
    info!("Attempting to connect to service: {}", service_name);

    // Get a binder proxy for the UsbAuthManager service.
    let service = get_interface::<dyn IUsbAuthManager>(service_name)
        .expect("Failed to get usb_auth_manager service");

    info!("Successfully connected to service.");

    match cli.command {
        Commands::SetState { state } => {
            let system_state: UsbAuthorizationSystemState = state.into();
            // Set the system state from the command-line argument.
            info!("Setting system state to {:?}...", system_state);
            service.setSystemState(system_state)?;
            info!("Successfully set system state.");
        }
        Commands::ShowLists { list } => {
            show_device_lists(service.as_ref(), &list).await?;
        }
    }
    Ok(())
}

#[derive(Parser, Debug)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// Set the system state.
    SetState {
        /// The system state to set.
        #[arg(value_enum)]
        state: SystemStateArg,
    },
    /// Show a device list.
    ShowLists {
        /// The type of device list to show.
        #[arg(value_enum)]
        list: ListType,
    },
}

#[derive(ValueEnum, Copy, Clone, Debug, PartialEq, Eq)]
enum SystemStateArg {
    Booted,
    LoggedIn,
    ScreenLocked,
    SetUp,
}

impl From<SystemStateArg> for UsbAuthorizationSystemState {
    fn from(arg: SystemStateArg) -> Self {
        match arg {
            SystemStateArg::Booted => UsbAuthorizationSystemState::BOOTED,
            SystemStateArg::LoggedIn => UsbAuthorizationSystemState::LOGGED_IN,
            SystemStateArg::ScreenLocked => UsbAuthorizationSystemState::SCREEN_LOCKED,
            SystemStateArg::SetUp => UsbAuthorizationSystemState::SET_UP,
        }
    }
}

#[derive(ValueEnum, Copy, Clone, Debug, PartialEq, Eq)]
enum ListType {
    Authorized,
    Deferred,
    Ask,
    All,
}

async fn show_device_lists(
    service: &dyn IUsbAuthManager,
    list_type: &ListType,
) -> binder::Result<()> {
    info!("Fetching device list(s)...");

    match list_type {
        ListType::Authorized => {
            let authorized_devices = service.getAuthorizedUsbDevices()?;
            println!("--- Authorized Devices ---");
            print_device_list(&authorized_devices);
        }
        ListType::Deferred => {
            let deferred_devices = service.getDeferredUsbDevices()?;
            println!("--- Deferred Devices ---");
            print_device_list(&deferred_devices);
        }
        ListType::Ask => {
            let ask_devices = service.getDevicesAwaitingAuthorization()?;
            println!("--- Devices Awaiting Authorization ---");
            print_device_list(&ask_devices);
        }
        ListType::All => {
            let authorized_devices = service.getAuthorizedUsbDevices()?;
            println!("--- Authorized Devices ---");
            print_device_list(&authorized_devices);

            let deferred_devices = service.getDeferredUsbDevices()?;
            println!("\n--- Deferred Devices ---");
            print_device_list(&deferred_devices);

            let ask_devices = service.getDevicesAwaitingAuthorization()?;
            println!("\n--- Devices Awaiting Authorization ---");
            print_device_list(&ask_devices);
        }
    }

    Ok(())
}

fn print_device_list(devices: &[UsbAuthDeviceInfo]) {
    if devices.is_empty() {
        println!("None");
        return;
    }
    for (i, device) in devices.iter().enumerate() {
        println!("  {}:", i + 1);
        println!("     Manufacturer: {}", device.manufacturer);
        println!("     Product: {}", device.productName);
        println!("     Vendor ID: {:#04x}", device.vendorId);
        println!("     Product ID: {:#04x}", device.productId);
        println!("     Syspath: {}", device.syspath);
    }
}
