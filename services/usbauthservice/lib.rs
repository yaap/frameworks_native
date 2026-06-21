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

//! The `usbauthservice_core` crate provides the core logic for the USB authorization service.

/// Defines the structure and logic for USB authorization rules.
pub mod rules;

/// Defines the parser for USB authorization rules.
pub mod parser;

/// Provides functionality for retrieving device information.
pub mod device_info;

/// Handles the core authorization logic based on defined rules.
pub mod authorization;

/// Handles the core logic for the USB authorization service.
pub mod manager;

/// Handles the core logic for the USB authorization service.
pub mod service;
