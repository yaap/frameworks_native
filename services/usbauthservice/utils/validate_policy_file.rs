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

//! A utility to validate USB authorization policy files.
use clap::Parser as clapParser;
use std::path::Path;
use usbauthservice_core::parser::Parser;

#[derive(clapParser, Debug)]
struct Args {
    /// Path to the policy file to validate
    #[arg(name = "POLICY_FILE")]
    policy_file_path: String,
}

fn main() {
    let args = Args::parse();

    println!("Validating policy file: {}", args.policy_file_path);
    let mut parser = Parser::new(/*debuggable=*/ true);
    match parser.parse_from_file(Path::new(&args.policy_file_path)) {
        Err(e) => println!("{:#?}", e),
        Ok(_) => println!("{:#?}", parser.policy()),
    };
}

#[cfg(test)]
mod tests {
    use super::*;
    use log::debug;
    use std::env;

    fn init_logger() {
        let _ = env_logger::try_init();
    }

    #[test]
    fn test_validate_policy_file() {
        init_logger();
        // The `data` attribute in Android.bp copies the file to the test's execution directory,
        // preserving its relative path. We assume the test's current working directory is
        // the root of its test data.
        let current_dir = env::current_dir().expect("Failed to get current directory");
        let policy_file_path = current_dir.join("config").join("desktop_auth_policy.conf");

        debug!("Validating policy file: {:?}", policy_file_path);
        let mut parser = Parser::new(/*debuggable=*/ true);
        let result = parser.parse_from_file(&policy_file_path);
        assert!(result.is_ok(), "Failed to parse policy file: {:#?}", result.err());
    }
}
