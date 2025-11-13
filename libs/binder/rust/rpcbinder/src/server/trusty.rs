/*
 * Copyright (C) 2023 The Android Open Source Project
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

use alloc::boxed::Box;
use binder::{unstable_api::AsNative, SpIBinder};
use libc::size_t;
use log::error;
use std::ffi::{c_char, c_void};
use std::ptr;
use tipc::{
    ClientIdentifier, ConnectResult, Handle, MessageResult, PortCfg, TipcError, UnbufferedService,
    Uuid,
};

/// Trait alias for the callback passed into the per-session constructor of the RpcServer.
/// Note: this is used in this file only, although it is marked as pub to be able to be used in
/// the definition of the pub constructor.
pub trait PerSessionCallback:
    Fn(ClientIdentifier) -> Option<SpIBinder> + Send + Sync + 'static
{
}
impl<T> PerSessionCallback for T where
    T: Fn(ClientIdentifier) -> Option<SpIBinder> + Send + Sync + 'static
{
}

pub struct RpcServer {
    inner: *mut binder_rpc_server_bindgen::ARpcServerTrusty,
}

/// SAFETY: The opaque handle points to a heap allocation
/// that should be process-wide and not tied to the current thread.
unsafe impl Send for RpcServer {}
/// SAFETY: The underlying C++ RpcServer class is thread-safe.
unsafe impl Sync for RpcServer {}

impl Drop for RpcServer {
    fn drop(&mut self) {
        // SAFETY: `ARpcServerTrusty_delete` is the correct destructor to call
        // on pointers returned by `ARpcServerTrusty_new`.
        unsafe {
            binder_rpc_server_bindgen::ARpcServerTrusty_delete(self.inner);
        }
    }
}

impl RpcServer {
    /// Allocates a new RpcServer object.
    pub fn new(service: SpIBinder) -> RpcServer {
        Self::new_per_session(move |_uuid| Some(service.clone()))
    }

    /// Allocates a new per-session RpcServer object.
    ///
    /// Per-session objects take a closure that gets called once
    /// for every new connection. The closure gets the `ClientIdentifier` of
    /// the peer and can accept or reject that connection.
    pub fn new_per_session<F: PerSessionCallback>(f: F) -> RpcServer {
        // SAFETY: Takes ownership of the returned handle, which has correct refcount.
        let inner = unsafe {
            binder_rpc_server_bindgen::ARpcServerTrusty_newPerSession(
                Some(per_session_callback_wrapper::<F>),
                Box::into_raw(Box::new(f)).cast(),
                Some(per_session_callback_deleter::<F>),
            )
        };
        RpcServer { inner }
    }
}

unsafe extern "C" fn per_session_callback_wrapper<F: PerSessionCallback>(
    client_id_ptr: *const c_void,
    len: size_t,
    cb_ptr: *mut c_char,
) -> *mut binder_rpc_server_bindgen::AIBinder {
    // SAFETY: This callback should only get called while the RpcServer is alive.
    let cb = unsafe { &mut *cb_ptr.cast::<F>() };

    if len < 1 {
        return ptr::null_mut();
    }
    // SAFETY: We have checked that the length is at least 1
    // We know that the pointer has not been freed at this point, because:
    // 1) The pointer is allocated in the call to: `on_connect` or `on_new_connection` in the
    //    implementation of the `UnbufferredService` trait for `RpcServer`.
    // 2) `on_connect` and `on_new_connection` invokes `ARpcServerTrusty_handleConnect`, immediately
    //    after the allocation.
    // 3) this callback is invoked in the callpath of `ARpcServerTrusty_handleConnect`.
    // We know that there is no concurrent mutable access to the pointer because it is allocated
    // and accessed in the same (single) process as per the callpath described above.
    let client_id_data = unsafe { std::slice::from_raw_parts(client_id_ptr.cast(), len) };
    let client_id = match ClientIdentifier::from_tagged_bytes(client_id_data) {
        Ok(c) => c,
        Err(_) => {
            error!("error in reconstructing the ClientIdentifier from pointer and length");
            return ptr::null_mut();
        }
    };

    cb(client_id).map_or_else(ptr::null_mut, |b| {
        // Prevent AIBinder_decStrong from being called before AIBinder_toPlatformBinder.
        // The per-session callback in C++ is supposed to call AIBinder_decStrong on the
        // pointer we return here.
        std::mem::ManuallyDrop::new(b).as_native_mut().cast()
    })
}

unsafe extern "C" fn per_session_callback_deleter<F: PerSessionCallback>(cb: *mut c_char) {
    // SAFETY: shared_ptr calls this to delete the pointer we gave it.
    // It should only get called once the last shared reference goes away.
    unsafe {
        drop(Box::<F>::from_raw(cb.cast()));
    }
}

pub struct RpcServerConnection {
    ctx: *mut c_void,
}

// SAFETY: The opaque handle: `ctx` points into a dynamic allocation,
// and not tied to anything specific to the current thread.
unsafe impl Send for RpcServerConnection {}

impl Drop for RpcServerConnection {
    fn drop(&mut self) {
        // We do not need to close handle_fd since we do not own it.
        unsafe {
            binder_rpc_server_bindgen::ARpcServerTrusty_handleChannelCleanup(self.ctx);
        }
    }
}

impl UnbufferedService for RpcServer {
    type Connection = RpcServerConnection;

    fn on_connect(
        &self,
        _port: &PortCfg,
        handle: &Handle,
        peer: &Uuid,
    ) -> tipc::Result<ConnectResult<Self::Connection>> {
        let mut conn = RpcServerConnection { ctx: std::ptr::null_mut() };
        let client_identifier = ClientIdentifier::UUID(peer.clone());
        let mut data = client_identifier.as_tagged_bytes();
        let len = data.len();
        // SAFETY: This unsafe block calls into a C++ function, which is considered safe, i.e. it
        // does not cause undefined behavior for valid inputs (see below), returns an integer which
        // indicates success or error, does not allocate or deallocate memory that Rust owns.
        // The inputs passed into the C++ function are valid: Trusty is single threaded, so there is
        // no concurrent access to `sef.inner`` and other inputs are not freed/deallocated until the
        // function returns. Correct length of the data pointed to by the pointer is passed in.
        let rc = unsafe {
            binder_rpc_server_bindgen::ARpcServerTrusty_handleConnect(
                self.inner,
                handle.as_raw_fd(),
                data.as_mut_ptr() as *const c_void,
                len,
                &mut conn.ctx,
            )
        };
        if rc < 0 {
            Err(TipcError::from_uapi(rc.into()))
        } else {
            Ok(ConnectResult::Accept(conn))
        }
    }

    fn on_message(
        &self,
        conn: &Self::Connection,
        _handle: &Handle,
        _buffer: &mut [u8],
    ) -> tipc::Result<MessageResult> {
        let rc = unsafe { binder_rpc_server_bindgen::ARpcServerTrusty_handleMessage(conn.ctx) };
        if rc < 0 {
            Err(TipcError::from_uapi(rc.into()))
        } else {
            Ok(MessageResult::MaintainConnection)
        }
    }

    fn on_disconnect(&self, conn: &Self::Connection) {
        unsafe { binder_rpc_server_bindgen::ARpcServerTrusty_handleDisconnect(conn.ctx) };
    }

    fn on_new_connection(
        &self,
        _port: &PortCfg,
        handle: &Handle,
        client_identifier: &ClientIdentifier,
    ) -> tipc::Result<ConnectResult<Self::Connection>> {
        let mut conn = RpcServerConnection { ctx: std::ptr::null_mut() };
        let mut data = client_identifier.as_tagged_bytes();
        let len = data.len();
        // SAFETY: This unsafe block calls into a C++ function, which is considered safe, i.e. it
        // does not cause undefined behavior for valid inputs (see below), returns an integer which
        // indicates success or error, does not allocate or deallocate memory that Rust owns.
        // The inputs passed into the C++ function are valid: Trusty is single threaded, so there is
        // no concurrent access to `sef.inner`` and other inputs are not freed/deallocated until the
        // function returns. Correct length of the data pointed to by the pointer is passed in.
        let rc = unsafe {
            binder_rpc_server_bindgen::ARpcServerTrusty_handleConnect(
                self.inner,
                handle.as_raw_fd(),
                data.as_mut_ptr() as *const c_void,
                len,
                &mut conn.ctx,
            )
        };
        if rc < 0 {
            Err(TipcError::from_uapi(rc.into()))
        } else {
            Ok(ConnectResult::Accept(conn))
        }
    }
}
