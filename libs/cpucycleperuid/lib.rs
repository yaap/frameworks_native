/*
 * Copyright (C) 2025 The Android Open Source Project
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

//! This crate provides functionality for tracking CPU cycles and power consumption
//! per UID on Android, utilizing eBPF programs and Intel RAPL interface.

use anyhow::{anyhow, bail, Result};
use libbpf_rs::{num_possible_cpus, MapCore, MapFlags, MapHandle};
use std::collections::HashMap;
use std::ffi::CString;
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom};
use std::os::unix::io::{AsRawFd, FromRawFd, OwnedFd, RawFd};
use std::sync::Mutex;

use log::{error, info, warn};

const LOG_TAG: &str = "cpucycleperuid_rust";

const PKG_POWER_PATH: &str = "/sys/class/powercap/intel-rapl:0/energy_uj";
const MAX_PKG_POWER_PATH: &str = "/sys/class/powercap/intel-rapl:0/max_energy_range_uj";
const BPF_PROG_PATH: &str = "/sys/fs/bpf/cpucycleperuid/prog_cyclePerUid_tp_sched_switch";
const LAST_RECORDED_CYCLE_MAP_PATH: &str =
    "/sys/fs/bpf/cpucycleperuid/map_cyclePerUid_last_recorded_cycle_map";
const LAST_RUNNING_PID_MAP_PATH: &str =
    "/sys/fs/bpf/cpucycleperuid/map_cyclePerUid_last_running_pid_map";
const UID_CPU_CYCLE_MAP_PATH: &str = "/sys/fs/bpf/cpucycleperuid/map_cyclePerUid_uid_cpu_cycle_map";
const TSC_EVENTS_PATH: &str = "/sys/fs/bpf/cpucycleperuid/map_cyclePerUid_tsc_events";
const DESYNC_COUNTER_MAP_PATH: &str = "/sys/fs/bpf/cpucycleperuid/map_cyclePerUid_desync_counter";

const EVENT_TYPE: &str = "sched";
const EVENT_NAME: &str = "sched_switch";

static TRACKER: Mutex<Option<CpuCycleTracker>> = Mutex::new(None);

/// Tracks CPU cycles and power consumption per UID.
pub(crate) struct CpuCycleTracker {
    tracking: bool,
    last_recorded_cycle_map: MapHandle,
    last_running_pid_map: MapHandle,
    uid_cpu_cycle_map: MapHandle,
    _tsc_events_map: MapHandle,
    desync_counter_map: MapHandle,
    pkg_power_file: File,
    _perf_event_fds: Vec<OwnedFd>, // For TSC events
    last_package_energy: u64,
    max_package_energy: u64,
    last_uid_cpu_cycles: HashMap<u32, u64>,
    tracepoint_link_fd: Option<OwnedFd>,
}

impl CpuCycleTracker {
    /// Creates a new CpuCycleTracker instance.
    pub(crate) fn new() -> Result<Self> {
        if !is_rapl_available() {
            error!(
                "{}: init_globals: RAPL interface not found at {}, aborting BPF setup.",
                LOG_TAG, PKG_POWER_PATH
            );
            bail!("RAPL interface not available");
        }

        let last_recorded_cycle_map = MapHandle::from_pinned_path(LAST_RECORDED_CYCLE_MAP_PATH)?;
        let last_running_pid_map = MapHandle::from_pinned_path(LAST_RUNNING_PID_MAP_PATH)?;
        let uid_cpu_cycle_map = MapHandle::from_pinned_path(UID_CPU_CYCLE_MAP_PATH)?;
        let desync_counter_map = MapHandle::from_pinned_path(DESYNC_COUNTER_MAP_PATH)?;

        let (perf_event_fds, tsc_events_map) = setup_tsc_events()?;

        let max_package_energy = read_sysfs_file(MAX_PKG_POWER_PATH)?.trim().parse::<i64>()? as u64;
        if max_package_energy == 0 {
            error!("{}: Failed to read max package energy from {}", LOG_TAG, MAX_PKG_POWER_PATH);
            bail!("Failed to read max package energy");
        }
        info!("{}: Max package energy: {}", LOG_TAG, max_package_energy);

        let pkg_power_file = OpenOptions::new().read(true).open(PKG_POWER_PATH)?;

        Ok(Self {
            tracking: false,
            last_recorded_cycle_map,
            last_running_pid_map,
            uid_cpu_cycle_map,
            _tsc_events_map: tsc_events_map,
            desync_counter_map,
            pkg_power_file,
            _perf_event_fds: perf_event_fds,
            last_package_energy: 0,
            max_package_energy,
            last_uid_cpu_cycles: HashMap::new(),
            tracepoint_link_fd: None,
        })
    }

    /// Starts the BPF tracking by initializing globals and attaching the tracepoint program.
    pub(crate) fn start_tracking(&mut self) -> Result<bool> {
        info!("{}: Starting BPF tracking", LOG_TAG);
        if self.tracking {
            return Ok(true);
        }

        self.init_bpf_maps()?;

        match attach_tracepoint_program() {
            Ok(link_fd) => {
                self.tracepoint_link_fd = link_fd;
            }
            Err(e) => {
                let errno = errno::errno().0;
                error!("{}: Failed to attach tracepoint: {} (errno: {})", LOG_TAG, e, errno);
                return Err(e); // Propagate the error
            }
        }

        self.tracking = true;
        Ok(true)
    }

    /// Stops the BPF tracking by detaching the BPF program and cleaning up resources.
    pub(crate) fn stop_tracking(&mut self) {
        if !self.tracking {
            return;
        }
        self.tracking = false;

        if let Some(fd) = self.tracepoint_link_fd.take() {
            // SAFETY: bpf_link_detach is safe to call with a valid link FD.
            // We guarantee it's valid because it's an OwnedFd we got from bpf_link_create.
            let ret = unsafe { libbpf_sys::bpf_link_detach(fd.as_raw_fd()) };
            if ret < 0 {
                let err = errno::errno().0;
                error!("{}: Failed to detach link FD {:?}: {} (errno: {})", LOG_TAG, fd, ret, err);
            }
            // fd is dropped here and automatically closed, expressing clear ownership.
        }

        info!("{} BPF Tracking Stopped", LOG_TAG);
    }

    /// Reads the current total package energy consumption from the RAPL interface.
    pub(crate) fn read_package_power(&mut self) -> Result<i64> {
        read_number_from_file_handle(&mut self.pkg_power_file)
    }

    /// Reads the last recorded TSC value from the BPF map.
    pub(crate) fn read_last_recorded_cycle(&self) -> Result<i64> {
        let num_cpus = num_possible_cpus()
            .map_err(|e| anyhow!("Failed to get number of possible CPUs: {}", e))?;

        let key = (0u32).to_ne_bytes(); // Key for the percpu array

        let per_cpu_byte_values = self
            .last_recorded_cycle_map
            .lookup_percpu(&key, MapFlags::ANY)?
            .ok_or(anyhow!("Map entry not found"))?;

        if per_cpu_byte_values.len() != num_cpus {
            error!(
                "{}: lookup_percpu returned {} entries, expected {}",
                LOG_TAG,
                per_cpu_byte_values.len(),
                num_cpus
            );
            // Continue anyway, but this is unexpected
        }

        let mut max_cycle = 0u64;
        for (cpu_id, cpu_val_bytes) in per_cpu_byte_values.iter().enumerate() {
            if cpu_val_bytes.len() == std::mem::size_of::<u64>() {
                let val = u64::from_ne_bytes(cpu_val_bytes.as_slice().try_into().unwrap());
                if val > max_cycle {
                    max_cycle = val;
                }
            } else {
                error!(
                    "{}: Unexpected value size for CPU {}: {}",
                    LOG_TAG,
                    cpu_id,
                    cpu_val_bytes.len()
                );
            }
        }
        Ok(max_cycle as i64)
    }

    /// Reads the accumulated desync count from the BPF map.
    pub(crate) fn read_desync_count(&self) -> Result<u64> {
        let num_cpus = num_possible_cpus()
            .map_err(|e| anyhow!("Failed to get number of possible CPUs: {}", e))?;

        let key = (0u32).to_ne_bytes(); // Key for the percpu array

        let per_cpu_byte_values = self
            .desync_counter_map
            .lookup_percpu(&key, MapFlags::ANY)?
            .ok_or(anyhow!("Map entry not found"))?;

        if per_cpu_byte_values.len() != num_cpus {
            error!(
                "{}: lookup_percpu returned {} entries, expected {}",
                LOG_TAG,
                per_cpu_byte_values.len(),
                num_cpus
            );
            // Continue anyway, but this is unexpected
        }

        let mut total_count = 0u64;
        for (cpu_id, cpu_val_bytes) in per_cpu_byte_values.iter().enumerate() {
            if cpu_val_bytes.len() == std::mem::size_of::<u64>() {
                total_count += u64::from_ne_bytes(cpu_val_bytes.as_slice().try_into().unwrap());
            } else {
                error!(
                    "{}: Unexpected value size for CPU {}: {}",
                    LOG_TAG,
                    cpu_id,
                    cpu_val_bytes.len()
                );
            }
        }
        Ok(total_count)
    }

    /// Reads the accumulated CPU cycles for each UID from the BPF map.
    pub(crate) fn read_uid_cpu_cycles(&self) -> Result<HashMap<u32, u64>> {
        let mut ret = HashMap::new();
        let map = &self.uid_cpu_cycle_map;

        for key_bytes in map.keys() {
            let uid = u32::from_ne_bytes(key_bytes.clone().try_into().unwrap());
            if let Some(per_cpu_values) = map.lookup_percpu(&key_bytes, MapFlags::ANY)? {
                let mut total_cycles = 0u64;
                for cpu_val_bytes in per_cpu_values {
                    if cpu_val_bytes.len() == std::mem::size_of::<u64>() {
                        total_cycles +=
                            u64::from_ne_bytes(cpu_val_bytes.as_slice().try_into().unwrap());
                    } else {
                        error!(
                            "{}: Unexpected value size for PERCPU_HASH entry for UID {}",
                            LOG_TAG, uid
                        );
                    }
                }
                ret.insert(uid, total_cycles);
            } else {
                error!("{}: Failed to look up key for UID {}", LOG_TAG, uid);
            }
        }
        Ok(ret)
    }

    /// Calculates the energy delta consumed by each UID since the last call.
    pub(crate) fn read_uid_power_delta(&mut self) -> Result<HashMap<u32, u64>> {
        let mut power_delta = HashMap::new();

        // 1. Read Current Package Energy
        let current_energy = self.read_package_power()? as u64;

        // 2. Read Current PID CPU Times
        let current_uid_cpu_cycles = self.read_uid_cpu_cycles()?;

        // 3. Calculate Deltas
        let energy_delta = if self.last_package_energy > 0 {
            if current_energy >= self.last_package_energy {
                current_energy - self.last_package_energy
            } else {
                // RAPL wrap around case
                (self.max_package_energy - self.last_package_energy) + current_energy
            }
        } else {
            0
        };

        let mut total_cpu_cycle_delta = 0;
        let mut uid_cpu_cycle_deltas = HashMap::new();

        for (uid, current_cycle) in &current_uid_cpu_cycles {
            let last_cycle = self.last_uid_cpu_cycles.get(uid).unwrap_or(&0);
            if current_cycle > last_cycle {
                let delta = current_cycle - last_cycle;
                uid_cpu_cycle_deltas.insert(*uid, delta);
                total_cpu_cycle_delta += delta;
            } else if current_cycle < last_cycle {
                error!(
                    "{}: UID {}: current_cycle < last_cycle ({} < {}), skipping delta",
                    LOG_TAG, uid, current_cycle, last_cycle
                );
            }
        }

        // 4. Attribute Energy
        if energy_delta > 0 && total_cpu_cycle_delta > 0 {
            for (uid, cpu_delta) in &uid_cpu_cycle_deltas {
                let uid_power =
                    (*cpu_delta as f64 / total_cpu_cycle_delta as f64 * energy_delta as f64) as u64;
                power_delta.insert(*uid, uid_power);
            }
        }

        // 5. Update Previous Values
        self.last_uid_cpu_cycles = current_uid_cpu_cycles;
        self.last_package_energy = current_energy;

        Ok(power_delta)
    }

    // Initializes the BPF maps, clearing any existing entries.
    fn init_bpf_maps(&self) -> Result<()> {
        let num_cpus = num_possible_cpus()
            .map_err(|e| anyhow!("Failed to get number of possible CPUs: {}", e))?;
        if num_cpus == 0 {
            bail!("Failed to get number of possible CPUs");
        }
        let zero32 = 0u32.to_ne_bytes();

        let zeroed_val: Vec<u8> = 0u64.to_ne_bytes().to_vec();
        let values: Vec<Vec<u8>> = vec![zeroed_val; num_cpus];
        self.last_recorded_cycle_map.update_percpu(&zero32, &values, MapFlags::ANY)?;

        let zeroed_val_pid: Vec<u8> = 0i32.to_ne_bytes().to_vec(); // pid_t is i32
        let values_pid: Vec<Vec<u8>> = vec![zeroed_val_pid; num_cpus];
        self.last_running_pid_map.update_percpu(&zero32, &values_pid, MapFlags::ANY)?;
        self.desync_counter_map.update_percpu(&zero32, &values, MapFlags::ANY)?;

        for key_bytes in self.uid_cpu_cycle_map.keys() {
            self.uid_cpu_cycle_map.delete(&key_bytes)?;
        }

        info!("{}: BPF maps initialized", LOG_TAG);
        Ok(())
    }
}

impl Drop for CpuCycleTracker {
    fn drop(&mut self) {
        self.stop_tracking();
    }
}

// Wrapper around the perf_event_open syscall.
fn perf_event_open(
    hw_event: *mut libbpf_sys::perf_event_attr,
    pid: libc::pid_t,
    cpu: libc::c_int,
    group_fd: libc::c_int,
    flags: libc::c_ulong,
) -> Result<OwnedFd, i32> {
    // SAFETY: `libc::syscall` is safe to call with valid arguments for `SYS_perf_event_open`.
    // The kernel handles the system call, and the arguments are correctly prepared.
    let fd: RawFd =
        unsafe { libc::syscall(libc::SYS_perf_event_open, hw_event, pid, cpu, group_fd, flags) }
            .try_into()
            .map_err(|_| errno::errno().0)?;

    if fd < 0 {
        let err = errno::errno().0;
        error!("{}: perf_event_open failed for cpu {}: {}", LOG_TAG, cpu, err);
        Err(err)
    } else {
        // SAFETY: The file descriptor `fd` is a valid, newly created file descriptor
        // from the `perf_event_open` syscall, and we are taking ownership of it.
        Ok(unsafe { OwnedFd::from_raw_fd(fd) })
    }
}

// Reads a single i64 value from an open file.
fn read_number_from_file_handle(file: &mut File) -> Result<i64> {
    // Rewind to the beginning of the file
    file.seek(SeekFrom::Start(0))?;

    let mut buf = [0u8; 64];
    let bytes_read = file.read(&mut buf)?;
    let data = std::str::from_utf8(&buf[..bytes_read])?.trim();

    Ok(data.parse()?)
}

fn read_sysfs_file(file_path: &str) -> Result<String> {
    let mut file = File::open(file_path)?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)?;
    if contents.is_empty() {
        error!("{}: Empty file: {}", LOG_TAG, file_path);
        return Err(anyhow!("Empty file: {}", file_path));
    }
    Ok(contents)
}

/// Checks if the Intel RAPL interface for package power is available.
pub(crate) fn is_rapl_available() -> bool {
    let exists = std::path::Path::new(PKG_POWER_PATH).exists();
    exists
}

// Attaches the BPF program to the specified tracepoint.
fn attach_tracepoint_program() -> Result<Option<OwnedFd>> {
    // Get FD for the pinned program
    let prog_path_cstr = CString::new(BPF_PROG_PATH).unwrap();
    let opts = libbpf_sys::bpf_obj_get_opts {
        sz: std::mem::size_of::<libbpf_sys::bpf_obj_get_opts>() as u64,
        file_flags: libbpf_sys::BPF_F_RDONLY,
        ..Default::default()
    };

    // SAFETY: bpf_obj_get_opts is safe to call with a valid CString path and initialized options.
    // The kernel will return a valid FD or a negative error code.
    let prog_raw_fd = unsafe { libbpf_sys::bpf_obj_get_opts(prog_path_cstr.as_ptr(), &opts) };
    if prog_raw_fd < 0 {
        error!(
            "{}: Failed to get program FD from {}: {} (errno: {})",
            LOG_TAG,
            BPF_PROG_PATH,
            prog_raw_fd,
            errno::errno().0
        );
        bail!(
            "Failed to get program FD from {}: {} (errno: {})",
            BPF_PROG_PATH,
            prog_raw_fd,
            errno::errno().0
        );
    }
    // SAFETY: prog_raw_fd is a valid file descriptor obtained from bpf_obj_get_opts if >= 0.
    let prog_fd = unsafe { OwnedFd::from_raw_fd(prog_raw_fd) };

    // For tp_btf (BPF_PROG_TYPE_TRACING + BPF_TRACE_RAW_TP), we attach using bpf_link_create.
    // The target_fd is 0 because the target BTF ID is embedded in the program.
    // attach_type must be BPF_TRACE_RAW_TP.
    // SAFETY: bpf_link_create is called with a valid program FD and correct attach parameters.
    // null_mut() is a safe value for the opts parameter as it indicates no additional options.
    let link_fd = unsafe {
        libbpf_sys::bpf_link_create(
            prog_fd.as_raw_fd(),
            0,
            libbpf_sys::BPF_TRACE_RAW_TP,
            std::ptr::null_mut(),
        )
    };

    if link_fd < 0 {
        let err = errno::errno().0;
        if err == libc::EEXIST {
            warn!(
                "{}: Tracepoint {}/{} already has a BPF program attached (EEXIST). Continuing.",
                LOG_TAG, EVENT_TYPE, EVENT_NAME
            );
            return Ok(None);
        } else if err == libc::EBUSY {
            warn!("{}: Tracepoint {}/{} is busy (EBUSY).", LOG_TAG, EVENT_TYPE, EVENT_NAME);
        }
        error!("{}: Failed to attach tp_btf {}: {} (errno: {})", LOG_TAG, EVENT_NAME, link_fd, err);
        bail!("Failed to attach tp_btf {}: {} (errno: {})", EVENT_NAME, link_fd, err);
    }
    info!("{}: Successfully attached tp_btf {} with link FD {}", LOG_TAG, EVENT_NAME, link_fd);

    // SAFETY: link_fd is a valid file descriptor obtained from bpf_link_create if >= 0.
    Ok(Some(unsafe { OwnedFd::from_raw_fd(link_fd) }))
}

// Sets up perf events for reading the Time Stamp Counter (TSC) on each CPU.
fn setup_tsc_events() -> Result<(Vec<OwnedFd>, MapHandle)> {
    let mut pe_attr = libbpf_sys::perf_event_attr {
        type_: libbpf_sys::PERF_TYPE_HARDWARE,
        size: std::mem::size_of::<libbpf_sys::perf_event_attr>() as u32,
        config: libbpf_sys::PERF_COUNT_HW_CPU_CYCLES as u64,
        ..Default::default()
    };
    pe_attr.set_disabled(0);
    let num_cpus =
        num_possible_cpus().map_err(|e| anyhow!("Failed to get number of possible CPUs: {}", e))?;
    if num_cpus == 0 {
        bail!("Failed to get number of possible CPUs");
    }

    let mut perf_event_fds = Vec::with_capacity(num_cpus);

    for cpu_id in 0..num_cpus {
        let fd = perf_event_open(&mut pe_attr, -1, cpu_id as libc::c_int, -1, 0)
            .map_err(|e| anyhow!("Failed to open perf event for CPU {}: {}", cpu_id, e))?;
        perf_event_fds.push(fd);
    }

    let map = MapHandle::from_pinned_path(TSC_EVENTS_PATH)?;

    for (cpu_id, fd) in perf_event_fds.iter().enumerate() {
        let map_key = (cpu_id as u32).to_ne_bytes();
        let fd_bytes = fd.as_raw_fd().to_ne_bytes();

        map.update(&map_key, &fd_bytes, MapFlags::ANY)?;
    }

    info!("{}: Successfully set up TSC perf events and populated BPF map.", LOG_TAG);
    Ok((perf_event_fds, map))
}

/// Checks if the required BPF program for cycle per UID tracking is loaded.
pub(crate) fn is_bpf_program_present() -> bool {
    if let Ok(guard) = TRACKER.lock() {
        if let Some(tracker) = guard.as_ref() {
            if tracker.tracking {
                return true;
            }
        }
    }
    let exists = std::path::Path::new(BPF_PROG_PATH).exists();
    exists
}

/// C-compatible wrapper for `is_rapl_available`.
#[no_mangle]
pub extern "C" fn rust_is_rapl_available() -> bool {
    is_rapl_available()
}

/// C-compatible wrapper for `is_bpf_program_present`.
#[no_mangle]
pub extern "C" fn rust_is_bpf_program_present() -> bool {
    is_bpf_program_present()
}

/// C-compatible wrapper for `start_tracking`.
#[no_mangle]
pub extern "C" fn rust_start_tracking() -> bool {
    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return false;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return false;
            }
        }
    }

    if let Some(tracker) = guard.as_mut() {
        match tracker.start_tracking() {
            Ok(ret) => ret,
            Err(e) => {
                error!("{}: Failed to start tracking: {}", LOG_TAG, e);
                false
            }
        }
    } else {
        false
    }
}

/// C-compatible wrapper for `stop_tracking`.
#[no_mangle]
pub extern "C" fn rust_stop_tracking() {
    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return;
        }
    };

    if let Some(tracker) = guard.as_mut() {
        tracker.stop_tracking();
    }
}

/// C-compatible wrapper for `is_tracking`.
#[no_mangle]
pub extern "C" fn rust_is_tracking() -> bool {
    let guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return false;
        }
    };

    if let Some(tracker) = guard.as_ref() {
        tracker.tracking
    } else {
        false
    }
}

/// C-compatible wrapper for `read_package_power`.
#[no_mangle]
pub extern "C" fn rust_read_package_power() -> i64 {
    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return -1;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return -1;
            }
        }
    }

    if let Some(tracker) = guard.as_mut() {
        tracker.read_package_power().unwrap_or(-1)
    } else {
        -1
    }
}

/// C-compatible wrapper for `read_last_recorded_cycle`.
#[no_mangle]
pub extern "C" fn rust_read_last_recorded_cycle() -> i64 {
    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return -1;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return -1;
            }
        }
    }

    if let Some(tracker) = guard.as_ref() {
        tracker.read_last_recorded_cycle().unwrap_or(-1)
    } else {
        -1
    }
}

/// C-compatible wrapper for `read_desync_count`.
#[no_mangle]
pub extern "C" fn rust_read_desync_count() -> i64 {
    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return -1;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return -1;
            }
        }
    }

    if let Some(tracker) = guard.as_ref() {
        tracker.read_desync_count().unwrap_or(0) as i64
    } else {
        -1
    }
}

const MAX_TRACKED_UIDS: usize = cycleperuid_defs::MAX_TRACKED_UIDS as usize;
const FFI_ARRAY_SIZE: usize = MAX_TRACKED_UIDS * 2;

/// C-compatible wrapper for `read_uid_cpu_cycles`.
///
/// # Safety
///
/// The caller must ensure that the `buffer` pointer is valid and points to a
/// mutable memory region of at least `buffer_len` * sizeof(u64) bytes.
/// The `buffer_len` must be at least FFI_ARRAY_SIZE.
///
/// The buffer does not need to be initialized before the call. The function will write
/// values into it and return the number of elements successfully written. Rust code
/// can assume the returned number of elements are initialized.
///
/// The buffer pointer is not retained beyond the lifetime of this function.
#[no_mangle]
pub unsafe extern "C" fn rust_read_uid_cpu_cycles(buffer: *mut u64, buffer_len: usize) -> usize {
    if buffer.is_null() || buffer_len < FFI_ARRAY_SIZE {
        error!(
            "{}: Buffer is null or too small. Provided: {}, Required: {}",
            LOG_TAG, buffer_len, FFI_ARRAY_SIZE
        );
        return 0;
    }

    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return 0;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return 0;
            }
        }
    }

    let cycles = match guard.as_ref().unwrap().read_uid_cpu_cycles() {
        Ok(cumulative_cycles) => cumulative_cycles,
        Err(e) => {
            error!("{}: Error in read_uid_cpu_cycles: {}", LOG_TAG, e);
            return 0;
        }
    };

    // SAFETY: The caller guarantees that the `buffer` pointer is valid and
    // points to a mutable memory region of at least `buffer_len` * sizeof(u64) bytes.
    let buffer_slice = unsafe { std::slice::from_raw_parts_mut(buffer, buffer_len) };

    let mut count = 0;

    for (uid, cycle) in cycles {
        if count < MAX_TRACKED_UIDS {
            let base_index = count * 2;
            buffer_slice[base_index] = uid as u64;
            buffer_slice[base_index + 1] = cycle;
            count += 1;
        } else {
            error!("{}: Exceeded MAX_TRACKED_UIDS, stopping buffer fill", LOG_TAG);
            break;
        }
    }
    count * 2
}

/// C-compatible wrapper for `read_uid_power_delta`.
///
/// # Safety
///
/// The caller must ensure that the `buffer` pointer is valid and points to a
/// mutable memory region of at least `buffer_len` * sizeof(u64) bytes.
/// The `buffer_len` must be at least FFI_ARRAY_SIZE.
///
/// The buffer does not need to be initialized before the call. The function will write
/// values into it and return the number of elements successfully written. Rust code
/// can assume the returned number of elements are initialized.
///
/// The buffer pointer is not retained beyond the lifetime of this function.
#[no_mangle]
pub unsafe extern "C" fn rust_read_uid_power_delta(buffer: *mut u64, buffer_len: usize) -> usize {
    if buffer.is_null() || buffer_len < FFI_ARRAY_SIZE {
        error!(
            "{}: Buffer is null or too small. Provided: {}, Required: {}",
            LOG_TAG, buffer_len, FFI_ARRAY_SIZE
        );
        return 0;
    }

    let mut guard = match TRACKER.lock() {
        Ok(g) => g,
        Err(e) => {
            error!("{}: Failed to lock tracker: {}", LOG_TAG, e);
            return 0;
        }
    };

    if guard.is_none() {
        match CpuCycleTracker::new() {
            Ok(tracker) => *guard = Some(tracker),
            Err(e) => {
                error!("{}: Failed to initialize CpuCycleTracker: {}", LOG_TAG, e);
                return 0;
            }
        }
    }

    let power_deltas = match guard.as_mut().unwrap().read_uid_power_delta() {
        Ok(deltas) => deltas,
        Err(e) => {
            error!("{}: Error in read_uid_power_delta: {}", LOG_TAG, e);
            return 0;
        }
    };

    if power_deltas.len() > MAX_TRACKED_UIDS {
        error!("{}: More UIDs than expected: {}", LOG_TAG, power_deltas.len());
    }

    // SAFETY: The caller guarantees that the `buffer` pointer is valid.
    let buffer_slice = unsafe { std::slice::from_raw_parts_mut(buffer, buffer_len) };

    let mut count = 0;
    for (uid, power) in power_deltas {
        if count < MAX_TRACKED_UIDS {
            buffer_slice[count * 2] = uid as u64;
            buffer_slice[count * 2 + 1] = power;
            count += 1;
        } else {
            error!("{}: Exceeded MAX_TRACKED_UIDS, stopping buffer fill", LOG_TAG);
            break;
        }
    }
    count * 2
}

#[cfg(test)]
mod tests;
