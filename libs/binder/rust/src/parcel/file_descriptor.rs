/*
 * Copyright (C) 2020 The Android Open Source Project
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

#[cfg(not(feature = "std"))]
pub use nostd_parcel_fd::ParcelFileDescriptor;
#[cfg(feature = "std")]
pub use std_parcel_fd::ParcelFileDescriptor;

use crate::binder::AsNative;
use crate::binder_impl::{
    BorrowedParcel, Deserialize, DeserializeArray, DeserializeOption, Serialize, SerializeArray,
    SerializeOption,
};
use crate::error::{status_result, Result, StatusCode};
use crate::sys;
#[cfg(feature = "std")]
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};

#[cfg(feature = "std")]
mod std_parcel_fd {
    use std::{
        io,
        os::fd::{AsFd, AsRawFd, BorrowedFd, IntoRawFd, OwnedFd, RawFd},
    };

    /// Rust version of the Java class android.os.ParcelFileDescriptor
    #[derive(Debug)]
    pub struct ParcelFileDescriptor(OwnedFd);

    impl ParcelFileDescriptor {
        /// Create a new `ParcelFileDescriptor`
        pub fn new<F: Into<OwnedFd>>(fd: F) -> Self {
            Self(fd.into())
        }

        /// Creates a new `ParcelFileDescriptor` referring to the same resource by duplicating the
        /// underlying file descriptor.
        pub fn try_clone(&self) -> Result<Self, io::Error> {
            Ok(Self(self.0.try_clone()?))
        }
    }

    impl AsRef<OwnedFd> for ParcelFileDescriptor {
        fn as_ref(&self) -> &OwnedFd {
            &self.0
        }
    }

    impl From<ParcelFileDescriptor> for OwnedFd {
        fn from(fd: ParcelFileDescriptor) -> OwnedFd {
            fd.0
        }
    }

    impl AsRawFd for ParcelFileDescriptor {
        fn as_raw_fd(&self) -> RawFd {
            self.0.as_raw_fd()
        }
    }

    impl IntoRawFd for ParcelFileDescriptor {
        fn into_raw_fd(self) -> RawFd {
            self.0.into_raw_fd()
        }
    }

    impl AsFd for ParcelFileDescriptor {
        fn as_fd(&self) -> BorrowedFd<'_> {
            self.0.as_fd()
        }
    }
}

#[cfg(not(feature = "std"))]
mod nostd_parcel_fd {
    use core::ffi::c_int;
    use core::mem::ManuallyDrop;
    use core::num::NonZeroI32;

    /// Rust version of the Java class android.os.ParcelFileDescriptor
    /// This version of ParcelFileDescriptor is for no_std environments that have support for
    /// user space handles. TEE OSes fall into this category.
    ///
    /// Note that ParcelFileDescriptor owns the underlying resource and will attempt to close it
    /// when dropped. This implementation calls libc::close, which while not typical in no_std
    /// environments, gives us a standard mechanism for closing resources. An implementation of
    /// `close` is already required for native binder, which libbinder_rs depends on, so we assume
    /// availability of `close` here.
    #[derive(Debug)]
    pub struct ParcelFileDescriptor {
        // We don't have a NoAllOnes niche type available to us so we store the fd as non-zero.
        // This requires some extra care to increment by 1 when writing and decrement by 1 when
        // reading. See to_storable_fd and get_fd below.
        stored_fd: NonZeroI32,
    }

    impl ParcelFileDescriptor {
        /// Create a new `ParcelFileDescriptor`
        ///
        /// # Safety
        ///
        /// The file descriptor must be valid (i.e. not -1), open, and suitable for transferring
        /// ownership. It must not require any cleanup other than `libc::close`.
        pub unsafe fn from_raw_fd(raw_fd: c_int) -> Self {
            Self { stored_fd: Self::to_storable_fd(raw_fd) }
        }

        /// Get the underlying file descriptor as a c_int.
        /// This does not transfer ownership of the file descriptor to the caller.
        pub fn as_raw_fd(&self) -> c_int {
            self.get_fd()
        }

        /// Get the underlying file descriptor as a c_int.
        /// This transfers ownership of the file descriptor to the caller.
        pub fn into_raw_fd(self) -> c_int {
            ManuallyDrop::new(self).as_raw_fd()
        }

        fn to_storable_fd(fd: c_int) -> NonZeroI32 {
            let fd = fd + 1;
            fd.try_into().unwrap()
        }

        fn get_fd(&self) -> c_int {
            self.stored_fd.get() - 1
        }
    }

    impl Drop for ParcelFileDescriptor {
        fn drop(&mut self) {
            // Safety: self.0 is a valid integer.
            unsafe {
                libc::close(self.as_raw_fd());
            }
        }
    }
}

impl PartialEq for ParcelFileDescriptor {
    // Since ParcelFileDescriptors own the FD, if this function ever returns true (and it is used to
    // compare two different objects), then it would imply that an FD is double-owned.
    fn eq(&self, other: &Self) -> bool {
        self.as_raw_fd() == other.as_raw_fd()
    }
}

impl Eq for ParcelFileDescriptor {}

impl Serialize for ParcelFileDescriptor {
    fn serialize(&self, parcel: &mut BorrowedParcel<'_>) -> Result<()> {
        let fd = self.as_raw_fd();
        // Safety: `Parcel` always contains a valid pointer to an
        // `AParcel`. Likewise, `ParcelFileDescriptor` always contains a
        // valid file, so we can borrow a valid file
        // descriptor. `AParcel_writeParcelFileDescriptor` does NOT take
        // ownership of the fd, so we need not duplicate it first.
        let status = unsafe { sys::AParcel_writeParcelFileDescriptor(parcel.as_native_mut(), fd) };
        status_result(status)
    }
}

impl SerializeArray for ParcelFileDescriptor {}

impl SerializeOption for ParcelFileDescriptor {
    fn serialize_option(this: Option<&Self>, parcel: &mut BorrowedParcel<'_>) -> Result<()> {
        if let Some(f) = this {
            f.serialize(parcel)
        } else {
            let status =
            // Safety: `Parcel` always contains a valid pointer to an
            // `AParcel`. `AParcel_writeParcelFileDescriptor` accepts the
            // value `-1` as the file descriptor to signify serializing a
            // null file descriptor.
                unsafe { sys::AParcel_writeParcelFileDescriptor(parcel.as_native_mut(), -1i32) };
            status_result(status)
        }
    }
}

impl DeserializeOption for ParcelFileDescriptor {
    fn deserialize_option(parcel: &BorrowedParcel<'_>) -> Result<Option<Self>> {
        let mut fd = -1i32;
        // Safety: `Parcel` always contains a valid pointer to an
        // `AParcel`. We pass a valid mutable pointer to an i32, which
        // `AParcel_readParcelFileDescriptor` assigns the valid file
        // descriptor into, or `-1` if deserializing a null file
        // descriptor. The read function passes ownership of the file
        // descriptor to its caller if it was non-null, so we must take
        // ownership of the file and ensure that it is eventually closed.
        unsafe {
            status_result(sys::AParcel_readParcelFileDescriptor(parcel.as_native(), &mut fd))?;
        }
        if fd < 0 {
            Ok(None)
        } else {
            #[cfg(feature = "std")]
            {
                // Safety: At this point, we know that the file descriptor was
                // not -1, so must be a valid, owned file descriptor which we
                // can safely turn into a `File`.
                let file = unsafe { OwnedFd::from_raw_fd(fd) };
                Ok(Some(ParcelFileDescriptor::new(file)))
            }
            #[cfg(not(feature = "std"))]
            {
                // Safety: The fd is not -1 and AParcel_readParcelFileDescriptor
                // expects the caller to take ownership of fd.
                let pfd = unsafe { ParcelFileDescriptor::from_raw_fd(fd) };
                Ok(Some(pfd))
            }
        }
    }
}

impl Deserialize for ParcelFileDescriptor {
    type UninitType = Option<Self>;
    fn uninit() -> Self::UninitType {
        Self::UninitType::default()
    }
    fn from_init(value: Self) -> Self::UninitType {
        Some(value)
    }

    fn deserialize(parcel: &BorrowedParcel<'_>) -> Result<Self> {
        Deserialize::deserialize(parcel).transpose().unwrap_or(Err(StatusCode::UNEXPECTED_NULL))
    }
}

impl DeserializeArray for ParcelFileDescriptor {}
