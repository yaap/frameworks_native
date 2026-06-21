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

use super::*;
use std::hint::black_box;
use std::os::unix::process::CommandExt;
use std::process::Command;
use std::time::{Duration, Instant};

const UID_A: u32 = 19801;
const UID_B: u32 = 19901;

fn busy_loop(iterations: u64) {
    let mut sum: u64 = 1;
    for i in 1..iterations {
        sum = sum.wrapping_mul(black_box(i));
    }
    black_box(sum);
}

#[test]
fn helper_busy_loop() {
    if let Ok(iters) = std::env::var("BUSY_LOOP_ITERS") {
        let iterations: u64 = iters.parse().unwrap();
        busy_loop(iterations);
    }
}

#[test]
fn test_read_uid_cpu_cycles_reads_correctly() {
    let lock = named_lock::NamedLock::create("libcycleperuid_test").unwrap();
    let _guard = lock.lock().unwrap();

    if !is_rapl_available() {
        return;
    }

    let map = MapHandle::from_pinned_path(UID_CPU_CYCLE_MAP_PATH).expect("Failed to open map");

    // Clear map to remove stale entries from other tests
    for key in map.keys() {
        let _ = map.delete(&key);
    }

    let cycles_a: u64 = 123456789;
    let cycles_b: u64 = 987654321;

    let num_cpus = libbpf_rs::num_possible_cpus().unwrap();
    let zeroed_val: Vec<u8> = 0u64.to_ne_bytes().to_vec();

    let mut values_a: Vec<Vec<u8>> = vec![zeroed_val.clone(); num_cpus];
    values_a[0] = cycles_a.to_ne_bytes().to_vec();

    let mut values_b: Vec<Vec<u8>> = vec![zeroed_val.clone(); num_cpus];
    values_b[0] = cycles_b.to_ne_bytes().to_vec();

    map.update_percpu(&UID_A.to_ne_bytes(), &values_a, MapFlags::ANY)
        .expect("Failed to update map for UID A");
    map.update_percpu(&UID_B.to_ne_bytes(), &values_b, MapFlags::ANY)
        .expect("Failed to update map for UID B");

    let tracker = CpuCycleTracker::new().expect("Failed to create tracker");
    let cycles_map = tracker.read_uid_cpu_cycles().expect("Failed to read cycles");

    assert!(cycles_map.contains_key(&UID_A));
    assert!(cycles_map.contains_key(&UID_B));
    assert_eq!(cycles_map.get(&UID_A), Some(&cycles_a));
    assert_eq!(cycles_map.get(&UID_B), Some(&cycles_b));

    let _ = map.delete(&UID_A.to_ne_bytes());
    let _ = map.delete(&UID_B.to_ne_bytes());
}

#[test]
fn test_read_uid_power_delta_calculates_correctly() {
    let lock = named_lock::NamedLock::create("libcycleperuid_test").unwrap();
    let _guard = lock.lock().unwrap();

    if !is_rapl_available() {
        println!("Skipping test: RAPL not available");
        return;
    }

    let mut tracker = CpuCycleTracker::new().expect("Failed to create tracker");

    // Ensure we can start tracking
    tracker.start_tracking().expect("Failed to start tracking");

    // Run for approximately 4 seconds to ensure sufficient RAPL energy accumulation.
    let iterations_a = 3_000_000_000u64;
    let iterations_b = 6_000_000_000u64;
    let v_tol = 0.05;

    tracker.read_uid_power_delta().expect("Failed initial read");
    let initial_energy = tracker.read_package_power().expect("Failed read package power");
    let initial_cycles_map = tracker.read_uid_cpu_cycles().expect("Failed read cycles");
    let initial_cycles_a = *initial_cycles_map.get(&UID_A).unwrap_or(&0);
    let initial_cycles_b = *initial_cycles_map.get(&UID_B).unwrap_or(&0);

    let start = Instant::now();
    let exe = std::env::current_exe().expect("Failed to get current executable");

    let mut child_a = Command::new(&exe)
        .arg("--exact")
        .arg("tests::helper_busy_loop")
        .env("BUSY_LOOP_ITERS", iterations_a.to_string())
        .uid(UID_A)
        .spawn()
        .expect("Failed to spawn child A");

    let mut child_b = Command::new(&exe)
        .arg("--exact")
        .arg("tests::helper_busy_loop")
        .env("BUSY_LOOP_ITERS", iterations_b.to_string())
        .uid(UID_B)
        .spawn()
        .expect("Failed to spawn child B");

    let status_a = child_a.wait().expect("Failed to wait on child A");
    let status_b = child_b.wait().expect("Failed to wait on child B");

    assert!(status_a.success());
    assert!(status_b.success());

    let duration = start.elapsed();
    assert!(duration > Duration::from_secs(1), "Workload runtime too short: {:?}", duration);

    let power_delta_map = tracker.read_uid_power_delta().expect("Failed read power delta");
    let final_energy = tracker.read_package_power().expect("Failed read final power");
    let total_energy_delta = (final_energy - initial_energy) as u64;

    if total_energy_delta == 0 {
        println!("Skipping: No energy delta recorded");
        return;
    }

    let final_cycles_map = tracker.read_uid_cpu_cycles().expect("Failed read final cycles");
    let final_cycles_a = *final_cycles_map.get(&UID_A).unwrap_or(&initial_cycles_a);
    let final_cycles_b = *final_cycles_map.get(&UID_B).unwrap_or(&initial_cycles_b);

    let cycle_delta_a = final_cycles_a - initial_cycles_a;
    let cycle_delta_b = final_cycles_b - initial_cycles_b;

    assert!(cycle_delta_a > iterations_a * 3, "Cycle delta A too low: {}", cycle_delta_a);
    assert!(cycle_delta_b > iterations_b * 3, "Cycle delta B too low: {}", cycle_delta_b);

    let mut total_cycle_delta = 0;
    for (uid, final_c) in &final_cycles_map {
        let initial_c = *initial_cycles_map.get(uid).unwrap_or(&0);
        if *final_c > initial_c {
            total_cycle_delta += final_c - initial_c;
        }
    }

    if total_cycle_delta == 0 {
        println!("Skipping: No cycle delta recorded");
        return;
    }

    assert!(power_delta_map.contains_key(&UID_A));
    assert!(power_delta_map.contains_key(&UID_B));

    let total_attributed =
        power_delta_map.get(&UID_A).unwrap() + power_delta_map.get(&UID_B).unwrap();
    assert!(total_attributed > total_energy_delta / 2, "Attributed < 50% total");

    let expected_power_a =
        (cycle_delta_a as f64 / total_cycle_delta as f64) * total_energy_delta as f64;
    let expected_power_b =
        (cycle_delta_b as f64 / total_cycle_delta as f64) * total_energy_delta as f64;

    let power_a = *power_delta_map.get(&UID_A).unwrap() as f64;
    let power_b = *power_delta_map.get(&UID_B).unwrap() as f64;

    assert!(
        (power_a - expected_power_a).abs() < expected_power_a * v_tol,
        "Power A mismatch: got {}, want {}",
        power_a,
        expected_power_a
    );
    assert!(
        (power_b - expected_power_b).abs() < expected_power_b * v_tol,
        "Power B mismatch: got {}, want {}",
        power_b,
        expected_power_b
    );
}
