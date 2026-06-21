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

//! A parser for the USB authorization policy language.
//!
//! This module provides functionality to parse a text-based policy file
//! into a structured `Policy` object that can be used to make authorization
//! decisions.

use crate::rules::*;
use log::error;
use std::fmt;
use std::fs;
use std::path::{Path, PathBuf};
use thiserror::Error;

/// Encapsulates different types of errors that can occur during policy loading.
#[derive(Debug, Error, PartialEq)]
pub enum PolicyInnerError {
    /// An error occurred during the parsing of a rule.
    #[error(transparent)]
    Parse(#[from] ParseError),
    /// An error occurred while adding a parsed rule to the policy.
    #[error(transparent)]
    AddRule(#[from] AddRuleError),
    /// An I/O error occurred, with a descriptive error message.
    #[error("I/O error: {0}")]
    Io(String),
    /// Import is disallowed in this file.
    #[error("Import is disallowed: {0}")]
    ImportDisallowed(String),
}

/// Represents an error that occurred during policy loading.
#[derive(Debug, Error, PartialEq)]
pub struct PolicyLoadError {
    /// The file path from which the policy was attempted to be loaded.
    pub file_path: String,
    /// The encapsulated error.
    pub error: PolicyInnerError,
}

impl fmt::Display for PolicyLoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.file_path.is_empty() {
            write!(f, "{}", self.error)
        } else {
            write!(f, "Error loading policy from {}: {}", self.file_path, self.error)
        }
    }
}

impl PolicyLoadError {
    /// Returns a new `PolicyLoadError` with the given file path.
    pub fn with_file_path(mut self, path: &Path) -> Self {
        self.file_path = path.to_string_lossy().to_string();
        self
    }
}

impl From<PolicyInnerError> for PolicyLoadError {
    fn from(error: PolicyInnerError) -> Self {
        Self { file_path: String::new(), error }
    }
}

/// Stored location of system allowlist if used.
const SYSTEM_ALLOWLIST_PATH: &str = "/system/etc/usb_auth/allowlist";

/// Stored location of vendor allowlist if used.
const VENDOR_ALLOWLIST_PATH: &str = "/vendor/etc/usb_auth/allowlist";

/// Parses a USB authorization policy from a string content.
#[derive(Debug)]
pub struct Parser {
    /// Is ro.debuggable set to 1?
    debuggable: bool,

    /// The parsed policy.
    policy: Policy,

    /// Path to system allowlist.
    system_allowlist_path: PathBuf,

    /// Path to vendor allowlist.
    vendor_allowlist_path: PathBuf,
}

impl Parser {
    /// Construct `Parser` with given inputs.
    ///
    /// Args:
    ///   - debuggable: Whether ro.debuggable is set.
    ///   - system_allowlist_path: Path to the system allowlist.
    ///   - vendor_allowlist_path: Path to the vendor allowlist.
    pub fn with_paths<P: AsRef<Path>>(
        debuggable: bool,
        system_allowlist_path: P,
        vendor_allowlist_path: P,
    ) -> Self {
        let policy = Policy::new();
        Self {
            debuggable,
            policy,
            system_allowlist_path: system_allowlist_path.as_ref().to_path_buf(),
            vendor_allowlist_path: vendor_allowlist_path.as_ref().to_path_buf(),
        }
    }

    /// Construct `Parser` with given inputs.
    ///
    /// Args:
    ///   - debuggable: Whether ro.debuggable is set.
    pub fn new(debuggable: bool) -> Self {
        Self::with_paths(debuggable, SYSTEM_ALLOWLIST_PATH, VENDOR_ALLOWLIST_PATH)
    }

    /// Returns a reference to the parsed policy.
    pub fn policy(&self) -> &Policy {
        &self.policy
    }

    /// Parse rules from a given file path.
    pub fn parse_from_file(&mut self, path: &Path) -> Result<(), PolicyLoadError> {
        let content = fs::read_to_string(path).map_err(|e| PolicyLoadError {
            file_path: path.to_string_lossy().to_string(),
            error: PolicyInnerError::Io(e.to_string()),
        })?;

        self.parse_policy_contents(&content)
    }

    /// Parses a policy directly from a string content.
    fn parse_policy_contents(&mut self, content: &str) -> Result<(), PolicyLoadError> {
        for line in content.lines() {
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }

            match Parser::parse_rule(trimmed).map_err(PolicyInnerError::from)? {
                // Action rules get added directly.
                ParsedRule::Action(rule) => {
                    self.policy.add_rule(rule).map_err(PolicyInnerError::from)?
                }

                // Import rules need to expanded and validated before they are added.
                ParsedRule::ImportAllowList(import) => {
                    // Validate conditions if they exist.
                    if let Some(condition) = import.allowlist_condition {
                        match condition {
                            AllowListCondition::Debuggable => {
                                if !self.debuggable {
                                    continue;
                                }
                            }
                        }
                    }

                    match self.parse_import_allowlist(&import.allowlist_type) {
                        Ok(rules) => {
                            for rule in rules {
                                self.policy.add_rule(rule).map_err(PolicyInnerError::from)?;
                            }
                        }
                        Err(e) => error!("Failed to parse import-allowlist: {}", e),
                    }
                }
            }
        }

        Ok(())
    }

    /// Parses a single rule line.
    fn parse_rule(line: &str) -> Result<ParsedRule, ParseError> {
        // Pre-process the line to ensure delimiters are separated by spaces
        // for easier parsing.
        let processed_line = line.replace('{', " { ").replace('}', " } ");
        let mut parts = processed_line.split_whitespace().peekable();

        let parsed_rule = ParsedRule::parse(&mut parts)?;
        Ok(parsed_rule)
    }

    /// Parses an `import-allowlist` and returns all resulting rules from the file.
    fn parse_import_allowlist(
        &mut self,
        allowlist_type: &AllowListType,
    ) -> Result<Vec<Rule>, PolicyLoadError> {
        let file_path = match allowlist_type {
            AllowListType::System => self.system_allowlist_path.as_path(),
            AllowListType::Vendor => self.vendor_allowlist_path.as_path(),
        };

        let content = fs::read_to_string(file_path).map_err(|e| PolicyLoadError {
            file_path: file_path.to_string_lossy().to_string(),
            error: PolicyInnerError::Io(e.to_string()),
        })?;

        let mut result: Vec<Rule> = Vec::new();

        for line in content.lines() {
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            match Parser::parse_rule(trimmed).map_err(PolicyInnerError::from)? {
                // Action rules get added directly.
                ParsedRule::Action(rule) => {
                    result.push(rule);
                }

                // Can't import in import rules.
                ParsedRule::ImportAllowList(_) => {
                    return Err(PolicyLoadError::from(PolicyInnerError::ImportDisallowed(
                        "Import disallowed in already imported file".to_string(),
                    )));
                }
            }
        }

        // Validate the resulting rules.
        ImportAllowListRule::validate_rules(&result).map_err(PolicyInnerError::from)?;

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
    use std::collections::HashMap;
    use std::io::Write;
    use tempfile::{tempdir, NamedTempFile, TempDir};

    fn get_type_path(root: &Path, which: AllowListType) -> PathBuf {
        match which {
            AllowListType::System => root.join("system/etc/usb_auth/allowlist"),
            AllowListType::Vendor => root.join("vendor/etc/usb_auth/allowlist"),
        }
    }

    fn make_parser_with_debuggable(debuggable: bool) -> (TempDir, Parser) {
        let tmpdir = tempdir().unwrap();
        let system_allowlist_path = get_type_path(tmpdir.path(), AllowListType::System);
        let vendor_allowlist_path = get_type_path(tmpdir.path(), AllowListType::Vendor);

        (tmpdir, Parser::with_paths(debuggable, system_allowlist_path, vendor_allowlist_path))
    }

    fn make_parser() -> (TempDir, Parser) {
        make_parser_with_debuggable(true)
    }

    fn write_to_allowlist(root: &Path, which: AllowListType, content: &str) {
        let path = get_type_path(root, which);
        fs::create_dir_all(path.parent().unwrap()).expect("Failed to create allowlist dir");
        fs::write(path, content).expect("Failed to write to allowlist file");
    }

    #[test]
    fn test_parse_missing_action() {
        let rule_str = "with-id 18d1:4ee7";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError {
                file_path: "".to_string(),
                error: PolicyInnerError::Parse(ParseError::InvalidAction("with-id".to_string())),
            }
        );
    }

    #[test]
    fn test_parse_simple_allow_rule() {
        let rule_str = "allow with-id 18d1:4ee7";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        assert_eq!(
            rule.attributes.as_ref().unwrap().with_id,
            Some(DeviceId { vendor_id: Some(0x18d1), product_id: Some(0x4ee7) })
        );
    }

    #[test]
    fn test_parse_interface_rule() {
        let rule_str = "allow with-interface 03:*:*";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        let if_attr = rule.attributes.as_ref().unwrap().with_interface.as_ref().unwrap();
        let interfaces = &if_attr.interfaces;
        assert_eq!(if_attr.operator, Operator::Equals); // Default for single value
        assert_eq!(interfaces.len(), 1);
        assert_eq!(interfaces[0], InterfaceType { class: 0x03, subclass: None, protocol: None });
    }

    #[test]
    fn test_parse_multiple_interfaces_rule() {
        let rule_str = "deny with-interface any-of { 03:*:*, 08:06:* }";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Deny);
        let if_attr = rule.attributes.as_ref().unwrap().with_interface.as_ref().unwrap();
        assert_eq!(if_attr.operator, Operator::OneOf);
        let interfaces = &if_attr.interfaces;
        assert_eq!(interfaces.len(), 2);
        assert_eq!(interfaces[0], InterfaceType { class: 0x03, subclass: None, protocol: None });
        assert_eq!(
            interfaces[1],
            InterfaceType { class: 0x08, subclass: Some(0x06), protocol: None }
        );
    }

    #[test]
    fn test_parse_rule_with_system_condition() {
        let rule_str = "ask with-id 1234:5678 when Booted ScreenLocked";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Ask);
        let attributes = rule.attributes.as_ref().unwrap();
        assert_eq!(
            attributes.with_id,
            Some(DeviceId { vendor_id: Some(0x1234), product_id: Some(0x5678) })
        );
        let condition = rule.condition.as_ref().unwrap();
        assert_eq!(condition.operator, Operator::Equals); // Default
        assert_eq!(condition.states.len(), 2);
        assert_eq!(condition.states[0], UsbAuthorizationSystemState::BOOTED);
        assert_eq!(condition.states[1], UsbAuthorizationSystemState::SCREEN_LOCKED);
    }

    #[test]
    fn test_parse_invalid_rule() {
        let rule_str = "allow with-id 1234:5678 extra-token";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::UnexpectedToken(
                "Unexpected token at end of rule: 'extra-token'".to_string()
            )))
        );
    }

    #[test]
    fn test_parse_file_with_comments_and_empty_lines() {
        let content = "
# This is a comment
allow with-id 1111:2222

defer with-interface 08:*:*
";
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "{}", content).unwrap();
        let (_tmp, mut parser) = make_parser();
        parser.parse_from_file(file.path()).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 2);
        assert_eq!(policy.all_rules[0].action, Action::Allow);
        assert_eq!(policy.all_rules[1].action, Action::Defer);
    }

    #[test]
    fn test_parse_via_port_rule() {
        let rule_str = "allow via-port any-of { usb1, usb2 }";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        let port_attr = rule.attributes.as_ref().unwrap().via_port.as_ref().unwrap();
        assert_eq!(port_attr.operator, Operator::OneOf);
        let ports = &port_attr.ports;
        assert_eq!(ports, &vec!["usb1".to_string(), "usb2".to_string()]);
    }

    #[test]
    fn test_parse_complex_rule() {
        let rule_str = "allow name MyDevice serial 123 with-id 1234:5678 with-interface 03:01:02 internal-device when LoggedIn";
        let (_tmp, mut parser) = make_parser();
        parser.parse_policy_contents(rule_str).unwrap();
        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 1);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        let attributes = rule.attributes.as_ref().unwrap();
        assert_eq!(attributes.name, Some("MyDevice".to_string()));
        assert_eq!(attributes.serial, Some("123".to_string()));
        assert_eq!(
            attributes.with_id,
            Some(DeviceId { vendor_id: Some(0x1234), product_id: Some(0x5678) })
        );
        let if_attr = attributes.with_interface.as_ref().unwrap();
        let interfaces = &if_attr.interfaces;
        assert_eq!(
            interfaces[0],
            InterfaceType { class: 0x03, subclass: Some(0x01), protocol: Some(0x02) }
        );
        assert_eq!(attributes.internal_device, Some(true));
        let condition = rule.condition.as_ref().unwrap();
        assert_eq!(condition.states[0], UsbAuthorizationSystemState::LOGGED_IN);
    }

    #[test]
    fn test_parse_duplicate_default_rule() {
        let rule_str = "allow\ndeny";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::AddRule(AddRuleError::DuplicateDefaultRule)),
        );
    }

    #[test]
    fn test_parse_empty_attribute() {
        let rule_str = "allow name";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::MissingValue(
                "name".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_vendor_id() {
        let rule_str = "allow with-id invalid:1234";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_product_id() {
        let rule_str = "allow with-id 1234:invalid";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_class() {
        let rule_str = "allow with-interface invalid:*:*";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_subclass() {
        let rule_str = "allow with-interface 03:invalid:*";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_interface_protocol() {
        let rule_str = "allow with-interface 03:01:invalid";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::ParseIntError(
                "invalid digit found in string".to_string()
            ))),
        );
    }

    #[test]
    fn test_parse_invalid_system_state() {
        let rule_str = "allow when InvalidState";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::Parse(ParseError::UnknownSystemState(
                "InvalidState".to_string()
            ))),
        );
    }

    #[test]
    fn test_invalid_rule_order() {
        let rule_str = "allow when Booted with-id 1234:5678";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::UnknownSystemState(
                "with-id".to_string()
            )))
        );
    }

    #[test]
    fn test_parse_import_allowlist() {
        // Valid import and allowlist contents
        let rule_str = "import-allowlist vendor when debuggable";
        let allowlist_str = "allow with-id 18d1:0100\nallow with-interface 03:*:*";

        let (tmp, mut parser) = make_parser();
        write_to_allowlist(tmp.path(), AllowListType::Vendor, allowlist_str);
        parser.parse_policy_contents(rule_str).unwrap();

        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 2);
        let rule = &policy.all_rules[0];
        assert_eq!(rule.action, Action::Allow);
        let attributes = rule.attributes.as_ref().unwrap();
        assert_eq!(
            attributes.with_id,
            Some(DeviceId { vendor_id: Some(0x18d1), product_id: Some(0x0100) })
        );
    }

    #[test]
    fn test_skip_import_allowlist_not_debuggable() {
        // Valid import and allowlist contents
        let rule_str = "import-allowlist vendor when debuggable";
        let allowlist_str = "allow with-id 18d1:0100\nallow with-interface 03:*:*";

        let (tmp, mut parser) = make_parser_with_debuggable(false);
        write_to_allowlist(tmp.path(), AllowListType::Vendor, allowlist_str);
        parser.parse_policy_contents(rule_str).unwrap();

        let policy = parser.policy;
        assert_eq!(policy.all_rules.len(), 0);
    }

    #[test]
    fn test_parse_invalid_allowlist_type() {
        let rule_str = "import-allowlist foobar";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::InvalidImportAllowListType(
                "foobar".to_string()
            )))
        );
    }

    #[test]
    fn test_parse_invalid_import_allowlist_condition_missing_when() {
        let rule_str = "import-allowlist system foobar";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(ParseError::UnexpectedToken(
                "Unexpected token at end of rule: 'foobar'".to_string()
            )))
        );
    }

    #[test]
    fn test_parse_invalid_import_allowlist_condition() {
        let rule_str = "import-allowlist system when foobar";
        let (_tmp, mut parser) = make_parser();
        let error = parser.parse_policy_contents(rule_str).unwrap_err();
        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::from(
                ParseError::InvalidImportAllowListCondition("foobar".to_string())
            ))
        );
    }

    #[test]
    fn test_parse_invalid_allowlist_disallow_nested() {
        let nested_allowlist_str = "import-allowlist system";

        let (tmp, mut parser) = make_parser();
        write_to_allowlist(tmp.path(), AllowListType::Vendor, nested_allowlist_str);
        let error = parser.parse_import_allowlist(&AllowListType::Vendor).unwrap_err();

        assert_eq!(
            error,
            PolicyLoadError::from(PolicyInnerError::ImportDisallowed(
                "Import disallowed in already imported file".to_string(),
            ))
        );
    }

    #[test]
    fn test_validate_imported_rules() {
        let (tmp, mut parser) = make_parser();

        let mut rule_err_map = HashMap::new();
        rule_err_map.insert(
            "deny with-id 1234:4567",
            PolicyLoadError::from(PolicyInnerError::from(ParseError::InvalidRuleInAllowList(
                format!("Rule in allowlist had invalid action: {:?}", Action::Deny),
            ))),
        );
        rule_err_map.insert(
            "allow when Booted",
            PolicyLoadError::from(PolicyInnerError::from(ParseError::InvalidRuleInAllowList(
                "Rule in allowlist had empty device attributes".to_string(),
            ))),
        );
        rule_err_map.insert(
            "allow with-id 1234:4567 when Booted",
            PolicyLoadError::from(PolicyInnerError::from(ParseError::InvalidRuleInAllowList(
                "Rule in allowlist had non-empty system condition".to_string(),
            ))),
        );
        rule_err_map.insert(
            "allow",
            PolicyLoadError::from(PolicyInnerError::from(ParseError::InvalidRuleInAllowList(
                "Rule in allowlist had empty device attributes".to_string(),
            ))),
        );

        for (rule_str, expected_err) in rule_err_map {
            write_to_allowlist(tmp.path(), AllowListType::Vendor, rule_str);
            let error = parser.parse_import_allowlist(&AllowListType::Vendor).unwrap_err();

            assert_eq!(error, expected_err, "Expected error did not match for rule: {}", &rule_str);
        }
    }
}
