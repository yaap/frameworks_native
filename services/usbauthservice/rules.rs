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

//! This module defines the data structures for the USB authorization rule language.
//! It includes enums for actions and operators, structs for device identification
//! and attributes, and the core `Rule` and `Policy` structures for managing
//! authorization rules.

use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use std::collections::HashMap;
use thiserror::Error;

// Data structures for the USB authorization rule language.

/// Represents the action to be taken when a rule matches a device.
#[derive(Debug, PartialEq, Clone)]
pub enum Action {
    /// Allow the USB device connection.
    Allow,
    /// Allow the USB device if the client has persisted a previous allow decision.
    /// Similar to Ask but should not be user visible.
    AllowPersisted,
    /// Ask the user for authorization.
    Ask,
    /// Deny the USB device connection.
    Deny,
    /// Defer the decision to a lower-priority rule or default policy.
    Defer,
    /// Remove the device (e.g., if it was previously authorized).
    Remove,
}

impl Action {
    /// Parses an Action from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Self, ParseError> {
        if let Some(&action_str) = parts.peek() {
            let action = match action_str {
                "allow" => Ok(Action::Allow),
                "allow-persisted" => Ok(Action::AllowPersisted),
                "ask" => Ok(Action::Ask),
                "deny" => Ok(Action::Deny),
                "defer" => Ok(Action::Defer),
                "remove" => Ok(Action::Remove),
                other => Err(ParseError::InvalidAction(other.to_string())),
            }?;
            parts.next(); // Consume the action string after successful parsing.
            Ok(action)
        } else {
            Err(ParseError::UnexpectedToken(String::new()))
        }
    }
}

/// Represents an operator for matching multiple values in a condition.
#[derive(Debug, PartialEq, Clone)]
pub enum Operator {
    /// All specified values must match.
    AllOf,
    /// At least one of the specified values must match.
    OneOf,
    /// None of the specified values must match.
    NoneOf,
    /// The set of device values must exactly equal the set of specified values.
    Equals,
}

impl Default for Operator {
    /// The default operator is `Equals`.
    fn default() -> Self {
        Operator::Equals
    }
}

impl Operator {
    /// Parses an operator from the iterator, consuming it if found.
    /// Returns `Some(Operator)` if an operator is found and parsed,
    /// `None` if the next token is not an operator.
    pub fn parse(parts: &mut std::iter::Peekable<std::str::SplitWhitespace>) -> Option<Self> {
        if let Some(&op_str) = parts.peek() {
            let operator = match op_str {
                "all-of" => Operator::AllOf,
                "one-of" | "any-of" => Operator::OneOf,
                "none-of" => Operator::NoneOf,
                "equals" => Operator::Equals,
                _ => return None, // Not an operator, don't consume
            };
            parts.next(); // Consume the operator string
            Some(operator)
        } else {
            None
        }
    }

    /// Evaluates the operator where each rule item is matched against a collection of device items.
    fn evaluate_match<RuleItem, DeviceItem>(
        &self,
        rule_values: &[RuleItem],
        device_values: &[DeviceItem],
        item_matcher: impl Fn(&RuleItem, &DeviceItem) -> bool,
    ) -> bool {
        let has_match = |rule_item: &RuleItem| {
            device_values.iter().any(|device_item| item_matcher(rule_item, device_item))
        };

        match self {
            Operator::OneOf => rule_values.iter().any(has_match),
            Operator::AllOf => rule_values.iter().all(has_match),
            Operator::NoneOf => !rule_values.iter().any(has_match),
            Operator::Equals => {
                device_values.len() == rule_values.len() && rule_values.iter().all(has_match)
            }
        }
    }
}

/// Represents a device's vendor and product IDs.
#[derive(Debug, Clone, Default)]
pub struct DeviceId {
    /// The vendor ID of the device, if specified.
    pub vendor_id: Option<u16>,
    /// The product ID of the device, if specified.
    pub product_id: Option<u16>,
}

impl PartialEq for DeviceId {
    /// Checks if two `DeviceId` instances are equal in a "rule-like" manner.
    ///
    /// This implementation considers `self` (representing a rule's `DeviceId`)
    /// to match `other` (representing a device's `DeviceId`) if:
    /// - If `self.vendor_id` is specified, `other.vendor_id` must match it.
    /// - If `self.product_id` is specified, `other.product_id` must match it.
    ///   If a field in `self` is `None`, it acts as a wildcard and does not impose
    ///   a restriction on the corresponding field in `other`.
    fn eq(&self, other: &Self) -> bool {
        if let Some(self_vendor_id) = self.vendor_id {
            if other.vendor_id != Some(self_vendor_id) {
                return false;
            }
        }
        if let Some(self_product_id) = self.product_id {
            if other.product_id != Some(self_product_id) {
                return false;
            }
        }
        true
    }
}

/// Represents a USB interface type, defined by its class, subclass, and protocol.
#[derive(Debug, Clone)]
pub struct InterfaceType {
    /// The USB interface class.
    pub class: u8,
    /// The USB interface subclass, if specified.
    pub subclass: Option<u8>,
    /// The USB interface protocol, if specified.
    pub protocol: Option<u8>,
}

impl PartialEq for InterfaceType {
    /// Checks if two `InterfaceType` instances are equal.
    ///
    /// This implementation considers two `InterfaceType`s equal if their `class`
    /// matches, and if `subclass` and `protocol` are specified in `self`, they
    /// must also match in `other`. This allows for a "rule-like" matching where
    /// a more general rule (e.g., class only) can match a more specific device
    /// interface (class, subclass, protocol).
    fn eq(&self, other: &Self) -> bool {
        if self.class != other.class {
            return false;
        }
        if let Some(subclass) = self.subclass {
            if other.subclass != Some(subclass) {
                return false;
            }
        }
        if let Some(protocol) = self.protocol {
            if other.protocol != Some(protocol) {
                return false;
            }
        }
        true
    }
}

/// Represents a range for matching the bcdDevice of a USB device.
#[derive(Debug, PartialEq, Clone)]
pub struct BcdDeviceRange {
    /// The start of the BCD device version range (inclusive).
    pub start: u16,
    /// The end of the BCD device version range (inclusive), if specified.
    /// If `None`, the range includes all versions from `start` onwards.
    pub end: Option<u16>,
}

/// Represents a rule attribute for matching device ports.
#[derive(Debug, PartialEq, Clone)]
pub struct PortAttribute {
    /// The operator to use for matching.
    pub operator: Operator,
    /// The list of port identifiers to match against.
    pub ports: Vec<String>,
}

impl PortAttribute {
    /// Creates a new `PortAttribute`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, ports: Vec<String>) -> Self {
        Self { operator: operator.unwrap_or_default(), ports }
    }

    /// Checks if this `PortAttribute` matches the given device ports.
    pub fn matches_device_ports(&self, device_ports: &[String]) -> bool {
        self.operator.evaluate_match(&self.ports, device_ports, |rule_port, device_port| {
            rule_port == device_port
        })
    }
}

/// Represents a rule attribute for matching device interfaces.
#[derive(Debug, PartialEq, Clone)]
pub struct InterfaceAttribute {
    /// The operator to use for matching.
    pub operator: Operator,
    /// The list of interface types to match against.
    pub interfaces: Vec<InterfaceType>,
}

impl InterfaceAttribute {
    /// Creates a new `InterfaceAttribute`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, interfaces: Vec<InterfaceType>) -> Self {
        Self { operator: operator.unwrap_or_default(), interfaces }
    }

    /// Checks if this `InterfaceAttribute` matches the given device interfaces.
    pub fn matches_device_interfaces(&self, device_interfaces: &[InterfaceType]) -> bool {
        self.operator.evaluate_match(&self.interfaces, device_interfaces, PartialEq::eq)
    }
}

/// Represents a USB device with its various attributes.
///
/// This struct holds information about a connected USB device, which is then
/// used to evaluate against defined rules.
#[derive(Debug, Clone, Default)]
pub struct UsbDevice {
    /// The name of the device, if available.
    pub name: Option<String>,
    /// The serial number of the device, if available.
    pub serial: Option<String>,
    /// The vendor ID of the device, if available.
    pub vendor_id: Option<u16>,
    /// The product ID of the device, if available.
    pub product_id: Option<u16>,
    /// The device's release number in binary-coded decimal.
    pub bcd_device: Option<u16>,
    /// A list of port identifiers the device is connected through.
    pub ports: Vec<String>,
    /// A list of interface types supported by the device.
    pub interfaces: Vec<InterfaceType>,
    /// Indicates if the device is an internal component of the system.
    pub is_internal: bool,
}

/// Represents a set of attributes used to match against a `UsbDevice`.
///
/// These attributes define the conditions under which a `Rule` applies.
#[derive(Debug, PartialEq, Clone, Default)]
pub struct DeviceAttributes {
    /// Matches the device's name.
    pub name: Option<String>,
    /// Matches the device's serial number.
    pub serial: Option<String>,
    /// Matches the device's vendor and/or product ID.
    pub with_id: Option<DeviceId>,
    /// Matches the device's bcdDevice against a version range.
    pub with_bcd_device_range: Option<BcdDeviceRange>,
    /// Matches the ports the device is connected via, using an optional operator.
    pub via_port: Option<PortAttribute>,
    /// Matches the interface types the device provides, using an optional operator.
    pub with_interface: Option<InterfaceAttribute>,
    /// Matches whether the device is an internal component.
    pub internal_device: Option<bool>,
}

impl DeviceAttributes {
    /// Parses device attributes from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Option<Self>, ParseError> {
        let mut attrs = DeviceAttributes::default();
        let mut has_attributes = false;

        while let Some(&part) = parts.peek() {
            if !Self::is_valid_device_attribute(part) {
                break;
            }
            parts.next(); // Consume the attribute keyword
            has_attributes = true;
            match part {
                "name" => {
                    attrs.name = Self::parse_string_attribute(parts, "name")?;
                }
                "serial" => {
                    attrs.serial = Self::parse_string_attribute(parts, "serial")?;
                }
                "with-id" => {
                    attrs.with_id = Some(Self::parse_device_id(parts)?);
                }
                "with-bcd-device-range" => {
                    attrs.with_bcd_device_range = Some(Self::parse_bcd_device_range(parts)?);
                }
                "with-interface" => {
                    attrs.with_interface = Some(Self::parse_interface_attribute(parts)?);
                }
                "internal-device" => {
                    attrs.internal_device = Some(true);
                }
                "via-port" => {
                    attrs.via_port = Some(Self::parse_port_attribute(parts)?);
                }
                _ => {
                    // This case should ideally not be reached due to the `is_valid_device_attribute` check.
                    // However, the compiler requires an exhaustive match for `&str`.
                    return Err(ParseError::InvalidAttribute(part.to_string()));
                }
            }
        }
        if has_attributes {
            return Ok(Some(attrs));
        }
        Ok(None)
    }

    fn is_valid_device_attribute(attribute_keyword: &str) -> bool {
        attribute_keyword == "name"
            || attribute_keyword == "serial"
            || attribute_keyword == "with-id"
            || attribute_keyword == "with-interface"
            || attribute_keyword == "internal-device"
            || attribute_keyword == "with-bcd-device-range"
            || attribute_keyword == "via-port"
    }

    fn parse_string_attribute(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
        attribute_keyword: &str,
    ) -> Result<Option<String>, ParseError> {
        let value = parts.next().ok_or(ParseError::MissingValue(attribute_keyword.to_string()))?;
        Ok(Some(value.to_string()))
    }

    /// Parses the 'with-id' attribute, returning a `DeviceId` on success.
    fn parse_device_id(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<DeviceId, ParseError> {
        let id_str = parts.next().ok_or(ParseError::MissingValue("with-id".to_string()))?;
        let (vendor_id, product_id) =
            id_str.split_once(':').ok_or(ParseError::InvalidDeviceIdFormat(id_str.to_string()))?;
        Ok(DeviceId {
            vendor_id: if vendor_id == "*" {
                None
            } else {
                Some(
                    u16::from_str_radix(vendor_id, 16)
                        .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
                )
            },
            product_id: if product_id == "*" {
                None
            } else {
                Some(
                    u16::from_str_radix(product_id, 16)
                        .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
                )
            },
        })
    }

    /// Parses the 'with-bcd-device-range' attribute, returning a `BcdDeviceRange` on success.
    fn parse_bcd_device_range(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<BcdDeviceRange, ParseError> {
        let range_str =
            parts.next().ok_or(ParseError::MissingValue("with-bcd-device-range".to_string()))?;
        let (start_str, end_str) = range_str
            .split_once(':')
            .ok_or(ParseError::InvalidBcdDeviceRangeFormat(range_str.to_string()))?;

        let start = u16::from_str_radix(start_str, 16)
            .map_err(|e| ParseError::ParseIntError(e.to_string()))?;

        let end = if end_str == "*" {
            None
        } else {
            Some(
                u16::from_str_radix(end_str, 16)
                    .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
            )
        };

        if let Some(end_val) = end {
            if start > end_val {
                return Err(ParseError::InvalidBcdDeviceRangeFormat(
                    "start of range cannot be greater than end of range".to_string(),
                ));
            }
        }

        Ok(BcdDeviceRange { start, end })
    }

    /// Parses the 'with-interface' attribute, returning an `InterfaceAttribute` on success.
    fn parse_interface_attribute(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<InterfaceAttribute, ParseError> {
        let op = Operator::parse(parts);
        let interface_strings = if op.is_some() {
            Self::parse_bracketed_string_list(parts, "interface")?
        } else {
            vec![parts
                .next()
                .ok_or(ParseError::MissingValue("interface type".to_string()))?
                .to_string()]
        };
        let parsed_interfaces = interface_strings
            .into_iter()
            .map(|s| InterfaceType::parse(&s))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(InterfaceAttribute::new(op, parsed_interfaces))
    }

    /// Parses the 'via-port' attribute, returning a `PortAttribute` on success.
    fn parse_port_attribute(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<PortAttribute, ParseError> {
        let op = Operator::parse(parts);
        let port_strings_result = if op.is_some() {
            Self::parse_bracketed_string_list(parts, "port")
        } else {
            parts
                .next()
                .ok_or(ParseError::MissingValue("port".to_string()))
                .map(|s| vec![s.to_string()])
        }?;

        Ok(PortAttribute::new(op, port_strings_result))
    }

    /// Parses a list of strings enclosed in curly braces `{ ... }`.
    fn parse_bracketed_string_list(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
        context_name: &str,
    ) -> Result<Vec<String>, ParseError> {
        if parts.peek() != Some(&"{") {
            return Err(ParseError::UnexpectedToken(format!(
                "Expected ' {{ ' to start {} list",
                context_name
            )));
        }
        parts.next(); // Consume the '{' token.

        let mut items = Vec::new();
        while let Some(part) = parts.peek() {
            if *part == "}" {
                break;
            }
            let token = parts.next().unwrap().trim_end_matches(',');
            if !token.is_empty() {
                items.push(token.to_string());
            }
        }
        if parts.next() != Some("}") {
            return Err(ParseError::UnexpectedToken(format!(
                "Expected ' }} ' to close {} list",
                context_name
            )));
        }
        Ok(items)
    }
}

impl InterfaceType {
    fn parse(s: &str) -> Result<Self, ParseError> {
        let parts: Vec<&str> = s.split(':').collect();
        if parts.len() != 3 {
            return Err(ParseError::InvalidInterfaceTypeFormat(s.to_string()));
        }
        Ok(InterfaceType {
            class: u8::from_str_radix(parts[0], 16)
                .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
            subclass: if parts[1] == "*" {
                None
            } else {
                Some(
                    u8::from_str_radix(parts[1], 16)
                        .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
                )
            },
            protocol: if parts[2] == "*" {
                None
            } else {
                Some(
                    u8::from_str_radix(parts[2], 16)
                        .map_err(|e| ParseError::ParseIntError(e.to_string()))?,
                )
            },
        })
    }
}

/// If you update the `UsbAuthorizationSystemState` AIDL file, you must also
/// update the `ALL_SYSTEM_STATES` constant to ensure consistency
/// between the AIDL definition and the Rust policy. This constant is used to
/// initialize the policy with all valid system states and to validate rules
/// during addition.
/// A constant array containing all possible `UsbAuthorizationSystemState` values.
pub const ALL_SYSTEM_STATES: &[UsbAuthorizationSystemState] = &[
    UsbAuthorizationSystemState::BOOTED,
    UsbAuthorizationSystemState::LOGGED_IN,
    UsbAuthorizationSystemState::SCREEN_LOCKED,
    UsbAuthorizationSystemState::SET_UP,
];

/// Represents a condition based on the system's authorization state.
#[derive(Debug, PartialEq, Clone)]
pub struct SystemCondition {
    /// The operator to apply when evaluating multiple states (e.g., `OneOf`, `AllOf`).
    pub operator: Operator,
    /// The list of `UsbAuthorizationSystemState` values to match against.
    pub states: Vec<UsbAuthorizationSystemState>,
}

impl SystemCondition {
    /// Creates a new `SystemCondition`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, states: Vec<UsbAuthorizationSystemState>) -> Self {
        Self { operator: operator.unwrap_or_default(), states }
    }

    /// Parses a system condition from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Option<Self>, ParseError> {
        if parts.peek() != Some(&"when") {
            return Ok(None);
        }
        parts.next(); // Consume "when"
        let op = Operator::parse(parts);

        let mut states = Vec::new();
        if op.is_some() {
            let state_strings =
                DeviceAttributes::parse_bracketed_string_list(parts, "system state")?;
            for s in state_strings {
                states.push(Self::parse_system_state(&s)?);
            }
        } else {
            // If no operator, try to parse one or more system states directly.
            // This implicitly uses Operator::Equals for multiple states if no explicit operator is given.
            while let Some(&state_str) = parts.peek() {
                match Self::parse_system_state(state_str) {
                    Ok(state) => {
                        states.push(state);
                        parts.next(); // Consume the state string
                    }
                    Err(ParseError::UnknownSystemState(e)) => {
                        return Err(ParseError::UnknownSystemState(e));
                    }
                    Err(e) => {
                        // Other parsing errors for states
                        return Err(e);
                    }
                }
            }
        }

        if states.is_empty() {
            return Err(ParseError::MissingValue("system states for 'when' condition".to_string()));
        }

        Ok(Some(SystemCondition::new(op, states)))
    }

    fn parse_system_state(s: &str) -> Result<UsbAuthorizationSystemState, ParseError> {
        match s {
            "Booted" => Ok(UsbAuthorizationSystemState::BOOTED),
            "LoggedIn" => Ok(UsbAuthorizationSystemState::LOGGED_IN),
            "ScreenLocked" => Ok(UsbAuthorizationSystemState::SCREEN_LOCKED),
            "Setup" => Ok(UsbAuthorizationSystemState::SET_UP),
            _ => Err(ParseError::UnknownSystemState(s.to_string())),
        }
    }
}

/// Describes the allowlist type, which determines where the allowlist is loaded from.
#[derive(Debug, Clone)]
pub enum AllowListType {
    /// Load from system partition.
    System,
    /// Load from vendor partition.
    Vendor,
}

impl AllowListType {
    /// Parses an allowlist type from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<AllowListType, ParseError> {
        if let Some(&type_str) = parts.peek() {
            let allowlist_type = match type_str {
                "system" => Ok(AllowListType::System),
                "vendor" => Ok(AllowListType::Vendor),
                _ => Err(ParseError::InvalidImportAllowListType(type_str.to_string())),
            };
            parts.next(); // Consume type

            allowlist_type
        } else {
            Err(ParseError::UnexpectedToken(String::new()))
        }
    }
}

/// Represents additional constraints on the `import-allowlist` rule.
#[derive(Debug, Clone)]
pub enum AllowListCondition {
    /// Checks whether the system is debuggable (i.e. `ro.debuggable` is set).
    Debuggable,
}

impl AllowListCondition {
    /// Parses an allowlist condition from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Option<AllowListCondition>, ParseError> {
        if parts.peek() != Some(&"when") {
            return Ok(None);
        }
        parts.next(); // Consume "when"

        if let Some(&condition_str) = parts.peek() {
            let condition = match condition_str {
                "debuggable" => Ok(Some(AllowListCondition::Debuggable)),
                _ => Err(ParseError::InvalidImportAllowListCondition(condition_str.to_string())),
            };
            parts.next(); // Consume the condition string

            condition
        } else {
            Err(ParseError::UnexpectedToken(String::new()))
        }
    }
}

/// Represents an import Rule.
///
/// This rule will read the imported allowlist rules and insert them in place.
/// Since this is an allowlist, a few constraints are enforced on the rules (or they are dropped):
///   - Only `Allow` actions are allowed.
///   - All actions MUST have device attributes.
///   - Actions may not provide system conditions or interface actions.
///   - Default actions are not allowed.
///
/// If there are any parsing errors in the imported file, this entire rule is dropped and an error
/// will be logged (but parsing of the remaining rules file can continue).
#[derive(Debug, Clone)]
pub struct ImportAllowListRule {
    /// Used to determine where to load the allowlist from.
    pub allowlist_type: AllowListType,

    /// What condition to apply for this allowlist.
    pub allowlist_condition: Option<AllowListCondition>,
}

impl ImportAllowListRule {
    /// Parses an ImportAllowListRule from the given iterator of whitespace-separated parts.
    ///
    /// An import rule follows the format: `import-allowlist [allowlist-type]
    ///     [when allowlist-condition]`.
    /// For example: `import-allowlist vendor when debuggable`.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Self, ParseError> {
        if let Some(&import_sym) = parts.peek() {
            if import_sym != "import-allowlist" {
                return Err(ParseError::InvalidImportAllowList(import_sym.to_string()));
            }
            parts.next(); // Consume the action string after successful parsing.

            let allowlist_type = AllowListType::parse(parts)?;
            let allowlist_condition = AllowListCondition::parse(parts)?;

            if parts.peek().is_some() {
                return Err(ParseError::UnexpectedToken(format!(
                    "Unexpected token at end of rule: '{}'",
                    parts.next().unwrap()
                )));
            }

            Ok(ImportAllowListRule { allowlist_type, allowlist_condition })
        } else {
            Err(ParseError::UnexpectedToken(String::new()))
        }
    }

    /// Validate that the provided rules match the requirements for imported allowlist rules.
    pub fn validate_rules(rules: &[Rule]) -> Result<(), ParseError> {
        for rule in rules {
            if rule.action != Action::Allow {
                return Err(ParseError::InvalidRuleInAllowList(format!(
                    "Rule in allowlist had invalid action: {:?}",
                    rule.action
                )));
            }

            if rule.attributes.is_none() {
                return Err(ParseError::InvalidRuleInAllowList(
                    "Rule in allowlist had empty device attributes".to_string(),
                ));
            }

            if rule.condition.is_some() {
                return Err(ParseError::InvalidRuleInAllowList(
                    "Rule in allowlist had non-empty system condition".to_string(),
                ));
            }
        }

        Ok(())
    }
}

/// Represents a single USB authorization rule.
///
/// A rule consists of an `Action` to perform, `DeviceAttributes` to match against
/// a `UsbDevice`, and an optional `SystemCondition` based on the system's state.
#[derive(Debug, PartialEq, Clone)]
pub struct Rule {
    /// The action to take if this rule matches.
    pub action: Action,
    /// The attributes a `UsbDevice` must have to match this rule.
    pub attributes: Option<DeviceAttributes>,
    /// An optional condition based on the system's current authorization state.
    pub condition: Option<SystemCondition>,
}

/// Implementation of the `Rule` struct.
impl Rule {
    /// Parses a Rule from the given iterator of whitespace-separated parts.
    ///
    /// A rule string follows the format: `action [device-attributes] [when system-conditions]`
    /// For example: `allow name "MyDevice" when OneOf { Booted, LoggedIn }`
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Self, ParseError> {
        let action = Action::parse(parts)?;
        let attributes = DeviceAttributes::parse(parts)?;
        let condition = SystemCondition::parse(parts)?;

        if parts.peek().is_some() {
            return Err(ParseError::UnexpectedToken(format!(
                "Unexpected token at end of rule: '{}'",
                parts.next().unwrap()
            )));
        }

        Ok(Rule { action, attributes, condition })
    }

    /// Evaluates if the given `UsbDevice` matches the rule's attributes.
    pub fn evaluate(&self, device: &UsbDevice) -> bool {
        // If no attributes are specified in the rule, it matches all devices.
        let Some(attributes) = &self.attributes else {
            return true;
        };

        if attributes.name.is_some() && device.name.as_deref() != attributes.name.as_deref() {
            return false;
        }

        if attributes.serial.is_some() && device.serial.as_deref() != attributes.serial.as_deref() {
            return false;
        }

        if let Some(rule_device_id) = &attributes.with_id {
            let device_id_from_usb_device =
                DeviceId { vendor_id: device.vendor_id, product_id: device.product_id };
            if rule_device_id != &device_id_from_usb_device {
                return false;
            }
        }

        if let Some(bcd_range) = &attributes.with_bcd_device_range {
            let Some(device_bcd) = device.bcd_device else {
                // Rule requires bcdDevice, but device does not have one. No match.
                return false;
            };

            if device_bcd < bcd_range.start {
                return false;
            }
            if let Some(end) = bcd_range.end {
                if device_bcd > end {
                    return false;
                }
            }
        }

        if let Some(rule_is_internal) = attributes.internal_device {
            if rule_is_internal != device.is_internal {
                return false;
            }
        }

        if let Some(via_port) = &attributes.via_port {
            let device_ports = &device.ports;
            if !via_port.matches_device_ports(device_ports) {
                return false;
            }
        }

        if let Some(with_interface) = &attributes.with_interface {
            let device_interfaces = &device.interfaces;
            if !with_interface.matches_device_interfaces(device_interfaces) {
                return false;
            }
        }

        true
    }
}

/// Parsed rules from policy file.
#[derive(Debug, Clone)]
pub enum ParsedRule {
    /// Rule mapping to an action taken on a device.
    Action(Rule),

    /// Import rules from a separate files.
    ImportAllowList(ImportAllowListRule),
}

impl ParsedRule {
    /// Parses a rule from the given iterator of whitespace-separated parts.
    pub fn parse(
        parts: &mut std::iter::Peekable<std::str::SplitWhitespace>,
    ) -> Result<Self, ParseError> {
        match Rule::parse(parts) {
            Ok(rule) => Ok(ParsedRule::Action(rule)),

            // Only if the Action token didn't parse do we accept other Rule types.
            Err(ParseError::InvalidAction(action_msg)) => match ImportAllowListRule::parse(parts) {
                Ok(import) => Ok(ParsedRule::ImportAllowList(import)),

                // If we failed to also parse the import-allowlist, report this as an invalid
                // action as that's the higher priority parse request.
                Err(ParseError::InvalidImportAllowList(_)) => {
                    Err(ParseError::InvalidAction(action_msg))
                }

                Err(e) => Err(e),
            },

            Err(e) => Err(e),
        }
    }
}

/// Represents a collection of USB authorization rules.
///
/// This struct manages all loaded rules, organizing them for efficient lookup
/// based on the system's current state.
#[derive(Debug, Default, Clone)]
pub struct Policy {
    /// A flat list of all rules added to the policy.
    pub all_rules: Vec<Rule>,
    /// A map of system states to lists of rules that apply to those states.
    pub rules_by_state: HashMap<UsbAuthorizationSystemState, Vec<Rule>>,
    /// A flag to track if a default rule (no device attributes and no system state condition) has been added.
    pub default_rule: Option<Rule>,
}

/// Implementation of the `Policy` struct.
impl Policy {
    /// Creates a new, empty `Policy`.
    pub fn new() -> Self {
        let mut rules_by_state = HashMap::new();
        for state in ALL_SYSTEM_STATES {
            rules_by_state.insert(*state, Vec::new());
        }
        Self { all_rules: Vec::new(), rules_by_state, default_rule: None }
    }

    /// Adds a new `Rule` to the policy.
    ///
    /// The rule is added to `all_rules` and also categorized into `rules_by_state`
    /// based on its `SystemCondition`. If no condition is specified, the rule
    /// applies to all `ALL_SYSTEM_STATES`.
    pub fn add_rule(&mut self, rule: Rule) -> Result<(), AddRuleError> {
        // Check for default rule condition: no attributes and no system condition.

        if rule.attributes.is_none() && rule.condition.is_none() {
            if self.default_rule.is_some() {
                return Err(AddRuleError::DuplicateDefaultRule);
            }
            self.default_rule = Some(rule.clone());
        }

        self.all_rules.push(rule.clone());

        let states: &[UsbAuthorizationSystemState] = match &rule.condition {
            Some(condition) => &condition.states,
            None => ALL_SYSTEM_STATES,
        };

        for state in states {
            // If `get_mut` returns `None`, it means the state was not a key in `rules_by_state`.
            // Since `rules_by_state` is initialized with all `ALL_SYSTEM_STATES`,
            // this implies the state is invalid.
            if let Some(rules_for_state) = self.rules_by_state.get_mut(state) {
                rules_for_state.push(rule.clone());
            } else {
                return Err(AddRuleError::InvalidSystemState(*state));
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_add_rule_no_condition() {
        let mut policy = Policy::new();
        let rule = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes::default()),
            condition: None,
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);
        assert_eq!(policy.all_rules[0], rule);

        assert_eq!(policy.rules_by_state.len(), ALL_SYSTEM_STATES.len());
        for state in ALL_SYSTEM_STATES {
            assert!(policy.rules_by_state.contains_key(state));
            assert_eq!(policy.rules_by_state[state].len(), 1);
            assert_eq!(policy.rules_by_state[state][0], rule);
        }
    }

    #[test]
    fn test_ensure_system_state_enum_is_in_sync() {
        let aidl_states = UsbAuthorizationSystemState::enum_values();
        assert_eq!(aidl_states.len(), ALL_SYSTEM_STATES.len());
        for state in ALL_SYSTEM_STATES {
            assert!(aidl_states.contains(state));
        }
        for state in aidl_states {
            assert!(ALL_SYSTEM_STATES.contains(&state));
        }
    }

    #[test]
    fn test_add_rule_with_single_condition() {
        let mut policy = Policy::new();
        let states = vec![UsbAuthorizationSystemState::LOGGED_IN];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Deny,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);

        for state in ALL_SYSTEM_STATES {
            let rules_for_state = policy.rules_by_state.get(state).unwrap();
            if *state == UsbAuthorizationSystemState::LOGGED_IN {
                assert_eq!(rules_for_state.len(), 1);
                assert_eq!(rules_for_state[0], rule);
                assert_eq!(
                    rules_for_state[0].condition.as_ref().unwrap().operator,
                    Operator::Equals
                );
            } else {
                assert!(rules_for_state.is_empty());
            }
        }
    }

    #[test]
    fn test_add_rule_with_multiple_conditions() {
        let mut policy = Policy::new();
        let states = vec![
            UsbAuthorizationSystemState::LOGGED_IN,
            UsbAuthorizationSystemState::SCREEN_LOCKED,
        ];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Ask,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);

        for state in states {
            assert!(policy.rules_by_state.contains_key(&state));
            assert_eq!(
                policy.rules_by_state[&state][0].condition.as_ref().unwrap().operator,
                Operator::Equals
            );
            assert_eq!(policy.rules_by_state[&state].len(), 1);
            assert_eq!(policy.rules_by_state[&state][0], rule);
        }
    }

    #[test]
    fn test_add_rule_with_invalid_condition() {
        let mut policy = Policy::new();
        // Use a value not in ALL_SYSTEM_STATES
        let invalid_state = UsbAuthorizationSystemState(999) as UsbAuthorizationSystemState;
        let states = vec![invalid_state];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Deny,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        let result = policy.add_rule(rule.clone());

        assert_eq!(result, Err(AddRuleError::InvalidSystemState(invalid_state)));
        assert!(policy.all_rules.contains(&rule)); // Rule should still be in all_rules
        assert!(!policy.rules_by_state.values().any(|v| v.contains(&rule))); // Rule should not be added to rules_by_state for any state
    }

    #[test]
    fn test_add_duplicate_default_rule() {
        let mut policy = Policy::new();

        // First default rule
        let default_rule_1 = Rule { action: Action::Allow, attributes: None, condition: None };
        assert!(policy.add_rule(default_rule_1.clone()).is_ok());
        assert_eq!(policy.default_rule.as_ref(), Some(&default_rule_1));

        // Second default rule - should cause an error
        let default_rule_2 = Rule { action: Action::Deny, attributes: None, condition: None };
        let result = policy.add_rule(default_rule_2);
        assert_eq!(result, Err(AddRuleError::DuplicateDefaultRule));

        // Verify that only the first rule was added to all_rules and processed into rules_by_state
        assert_eq!(policy.all_rules.len(), 1);
        assert_eq!(policy.all_rules[0], default_rule_1);

        for state in ALL_SYSTEM_STATES {
            assert_eq!(policy.rules_by_state.get(state).unwrap().len(), 1);
            assert_eq!(policy.rules_by_state.get(state).unwrap()[0], default_rule_1);
        }
    }

    #[test]
    fn test_parse_bcd_device_range_ok() {
        // Test with a specific end
        let mut parts = "1234:5678".split_whitespace().peekable();
        let range = DeviceAttributes::parse_bcd_device_range(&mut parts).unwrap();
        assert_eq!(range, BcdDeviceRange { start: 0x1234, end: Some(0x5678) });

        // Test with a wildcard end
        let mut parts = "abcd:*".split_whitespace().peekable();
        let range = DeviceAttributes::parse_bcd_device_range(&mut parts).unwrap();
        assert_eq!(range, BcdDeviceRange { start: 0xabcd, end: None });
    }

    #[test]
    fn test_parse_bcd_device_range_invalid_format() {
        let mut parts = "1234".split_whitespace().peekable();
        let err = DeviceAttributes::parse_bcd_device_range(&mut parts).unwrap_err();
        assert!(matches!(err, ParseError::InvalidBcdDeviceRangeFormat(_)));
    }

    #[test]
    fn test_parse_bcd_device_range_invalid_range() {
        let mut parts = "5678:1234".split_whitespace().peekable();
        let err = DeviceAttributes::parse_bcd_device_range(&mut parts).unwrap_err();
        assert!(matches!(err, ParseError::InvalidBcdDeviceRangeFormat(_)));
    }

    #[test]
    fn test_evaluate_bcd_device_range() {
        // Device bcdDevice is within the range
        let rule = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_bcd_device_range: Some(BcdDeviceRange { start: 0x0200, end: Some(0x0299) }),
                ..Default::default()
            }),
            condition: None,
        };
        let device = UsbDevice { bcd_device: Some(0x0250), ..Default::default() };
        assert!(rule.evaluate(&device));

        // Device bcdDevice is at the start of the range
        let device = UsbDevice { bcd_device: Some(0x0200), ..Default::default() };
        assert!(rule.evaluate(&device));

        // Device bcdDevice is at the end of the range
        let device = UsbDevice { bcd_device: Some(0x0299), ..Default::default() };
        assert!(rule.evaluate(&device));

        // Device bcdDevice is below the range
        let device = UsbDevice { bcd_device: Some(0x0199), ..Default::default() };
        assert!(!rule.evaluate(&device));

        // Device bcdDevice is above the range
        let device = UsbDevice { bcd_device: Some(0x0300), ..Default::default() };
        assert!(!rule.evaluate(&device));

        // Rule with wildcard end
        let rule_wildcard = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                with_bcd_device_range: Some(BcdDeviceRange { start: 0x0300, end: None }),
                ..Default::default()
            }),
            condition: None,
        };

        // Device bcdDevice is above the start
        let device = UsbDevice { bcd_device: Some(0x0400), ..Default::default() };
        assert!(rule_wildcard.evaluate(&device));

        // Device bcdDevice is at the start
        let device = UsbDevice { bcd_device: Some(0x0300), ..Default::default() };
        assert!(rule_wildcard.evaluate(&device));

        // Device bcdDevice is below the start
        let device = UsbDevice { bcd_device: Some(0x0299), ..Default::default() };
        assert!(!rule_wildcard.evaluate(&device));

        // Device has no bcdDevice
        let device_no_bcd = UsbDevice { bcd_device: None, ..Default::default() };
        assert!(!rule.evaluate(&device_no_bcd));

        // Rule has no bcd_device_range, device has bcdDevice. Should match.
        let rule_no_bcd_range = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes {
                name: Some("test".to_string()),
                ..Default::default()
            }),
            condition: None,
        };
        let device_with_bcd = UsbDevice {
            bcd_device: Some(0x0200),
            name: Some("test".to_string()),
            ..Default::default()
        };
        assert!(rule_no_bcd_range.evaluate(&device_with_bcd));
    }
}

/// Represents an error that occurred when adding a rule to a policy.
#[derive(Debug, Error, PartialEq)]
pub enum AddRuleError {
    /// The rule contains a `UsbAuthorizationSystemState` that is not recognized by the system.
    #[error("Rule contains an invalid system state: {0:?}")]
    InvalidSystemState(UsbAuthorizationSystemState),
    /// Attempted to add a second default rule, but only one is allowed.
    #[error("Attempted to add a second default rule, but only one is allowed.")]
    DuplicateDefaultRule,
}

/// Represents an error that occurred during parsing of a rule or policy.
#[derive(Debug, Error, PartialEq)]
pub enum ParseError {
    /// Indicates an invalid action keyword (e.g., "unknown-action").
    #[error("Invalid action: '{0}'")]
    InvalidAction(String),
    /// Indicates an invalid attribute keyword (e.g., "invalid-attr").
    #[error("Unknown attribute: '{0}'")]
    InvalidAttribute(String),
    /// Indicates an invalid operator keyword (e.g., "wrong-op").
    #[error("Invalid operator: '{0}'")]
    InvalidOperator(String),
    /// Indicates a missing value for an expected attribute (e.g., "name").
    #[error("Missing value for '{0}'")]
    MissingValue(String),
    /// Indicates an invalid format for a device ID (e.g., "123").
    #[error("Invalid device ID format: '{0}'")]
    InvalidDeviceIdFormat(String),
    /// Indicates an invalid format for a bcdDevice range (e.g., "123").
    #[error("Invalid bcdDevice range format: '{0}'")]
    InvalidBcdDeviceRangeFormat(String),
    /// Indicates an error parsing a number (e.g., vendor ID, product ID, class).
    #[error("Parse integer error: '{0}'")]
    ParseIntError(String),
    /// Indicates an invalid format for an interface type (e.g., "1:2").
    #[error("Invalid interface type format: '{0}'")]
    InvalidInterfaceTypeFormat(String),
    /// Indicates an unknown system state keyword (e.g., "Unauthorised").
    #[error("Unknown system state: '{0}'")]
    UnknownSystemState(String),
    /// Indicates an unexpected token in the input stream.
    #[error("Unexpected token: '{0}'")]
    UnexpectedToken(String),
    /// Invalid keyword for parsing `import-allowlist`.
    #[error("Invalid import keyword: '{0}'")]
    InvalidImportAllowList(String),
    /// Invalid allowlist type.
    #[error("Invalid import type: '{0}'")]
    InvalidImportAllowListType(String),
    /// Invalid allowlist condition.
    #[error("Invalid import condition: '{0}'")]
    InvalidImportAllowListCondition(String),
    /// Invalid rule in an imported allowlist rule set.
    #[error("Invalid rule inside allowlist: '{0}'")]
    InvalidRuleInAllowList(String),
    /// Generic parsing error for unexpected situations.
    #[error("Parsing error: '{0}'")]
    Generic(String),
}
