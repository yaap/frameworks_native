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

//! Helpers for managing vsock SELinux context

use anyhow::{bail, Context, Result};
use binder::ParcelFileDescriptor;
use std::ffi::{CStr, CString};
use std::fs::File;
use std::os::unix::io::{FromRawFd, IntoRawFd};
use std::sync::Mutex;
use vsock::VsockStream;

/// Validates that a vsock port is unprivileged (>= 1024)
///
/// TODO: use validate_vsock_port from AVF libs
fn validate_vsock_port(port: u32) -> Result<()> {
    /// Vsock privileged ports are below this number.
    const VSOCK_PRIV_PORT_MAX: u32 = 1024;

    let is_unprivileged = port >= VSOCK_PRIV_PORT_MAX;

    if is_unprivileged {
        Ok(())
    } else {
        bail!("Can't connect to privileged port {port}")
    }
}

fn vsock_stream_to_pfd(stream: VsockStream) -> ParcelFileDescriptor {
    // SAFETY: ownership is transferred from stream to f
    let f = unsafe { File::from_raw_fd(stream.into_raw_fd()) };
    ParcelFileDescriptor::new(f)
}

extern "C" {
    fn setsockcreatecon(context: *const ::std::os::raw::c_char) -> ::std::os::raw::c_int;
}

static SOCK_CONTEXT_MTX: Mutex<()> = Mutex::new(());

fn with_sock_context<T, F>(context: &CStr, f: F) -> T
where
    F: FnOnce() -> T,
{
    let lock = SOCK_CONTEXT_MTX.lock().unwrap();
    // SAFETY: call is protected by SOCK_CONTEXT_MTX mutex, context is alive until
    // setsockcreatecon is set to NULL at the end of this function.
    unsafe {
        setsockcreatecon(context.as_ptr());
    };
    let result: T = f();
    // SAFETY: call is protected by SOCK_CONTEXT_MTX mutex
    unsafe {
        setsockcreatecon(::std::ptr::null());
    };
    drop(lock);
    result
}

/// Connects to the vsock port of the given CID with a given SELinux context.
pub(crate) fn connect_with_cid_port_context(
    cid: u32,
    port: u32,
    context: &str,
) -> Result<ParcelFileDescriptor> {
    validate_vsock_port(port)?;
    let context_cstr = CString::new(context)
        .with_context(|| format!("Failed to convert context to CString: {context}"))?;
    with_sock_context(&context_cstr, || -> Result<ParcelFileDescriptor> {
        let stream = VsockStream::connect_with_cid_port(cid, port).context("Failed to connect")?;
        Ok(vsock_stream_to_pfd(stream))
    })
}
