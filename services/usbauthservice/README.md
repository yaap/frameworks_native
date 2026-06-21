If you update the `UsbAuthorizationSystemState` AIDL file, you must also update
the `ALL_SYSTEM_STATES` constant in `rules.rs` to ensure consistency between the
AIDL definition and the Rust policy. This constant is used to initialize the
policy with all valid system states and to validate rules during addition.
