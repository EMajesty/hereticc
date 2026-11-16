//! M0.3b-A zero-hook firmware compatibility probe.
//!
//! This library is loaded once with `LD_PRELOAD`, performs a read-only firmware
//! fingerprint check, writes one bounded report under `/run`, and returns. It
//! deliberately exports no callable C symbol and installs no callback.

use std::fmt::Write as _;
use std::fs::OpenOptions;
use std::io::Write as _;

use mpc_sample_sdk::probe::{self, ProbeResult, ProbeStatus};

/// HERETICC diagnostic version.
pub const PROBE_VERSION: &str = "0.1.0";
/// Exact upstream SDK revision patched by this experiment.
pub const SDK_COMMIT: &str = "e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39";
/// Human-readable patch-series identifier.
pub const PATCH_SET: &str = "m03b-a-zero-hook-v1";
/// Only supported stock firmware version.
pub const EXPECTED_FIRMWARE: &str = "1.3.0.12";
/// Transient report location on the device.
pub const REPORT_PATH: &str = "/run/hereticc-m03b-a.txt";
/// Hard upper bound for the complete report.
pub const MAX_REPORT_BYTES: usize = 768;

/// Convert one result into the bounded, non-sensitive report format.
///
/// # Errors
///
/// Returns an error if the fixed-format report exceeds [`MAX_REPORT_BYTES`].
pub fn format_report(result: ProbeResult) -> Result<String, ReportError> {
    let mut report = String::with_capacity(384);
    let (discovery, fingerprint, status_code) = match result.status {
        ProbeStatus::Compatible => ("found", "match", 0),
        ProbeStatus::MissingImage => ("missing", "not-checked", 10),
        ProbeStatus::Unreadable => ("found", "unreadable", 11),
        ProbeStatus::Mismatch => ("found", "mismatch", 12),
        ProbeStatus::TableSizeMismatch => ("found", "table-size-mismatch", 13),
    };
    writeln!(report, "hereticc_probe_version={PROBE_VERSION}").expect("String writes cannot fail");
    writeln!(report, "upstream_sdk_commit={SDK_COMMIT}").expect("String writes cannot fail");
    writeln!(report, "patch_set={PATCH_SET}").expect("String writes cannot fail");
    writeln!(report, "expected_firmware={EXPECTED_FIRMWARE}").expect("String writes cannot fail");
    writeln!(report, "image_discovery={discovery}").expect("String writes cannot fail");
    writeln!(report, "fingerprint_words_expected={}", result.expected)
        .expect("String writes cannot fail");
    writeln!(report, "fingerprint_words_checked={}", result.checked)
        .expect("String writes cannot fail");
    writeln!(report, "fingerprint_status={fingerprint}").expect("String writes cannot fail");
    writeln!(report, "feature_hooks_compiled=false").expect("String writes cannot fail");
    writeln!(report, "final_status={status_code}").expect("String writes cannot fail");
    if report.len() > MAX_REPORT_BYTES {
        return Err(ReportError::TooLarge);
    }
    Ok(report)
}

/// Report construction failure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReportError {
    /// The report exceeded its compile-time size bound.
    TooLarge,
}

/// Write one report to the transient device path.
///
/// Failure is returned to the constructor and otherwise ignored; it cannot
/// enable another SDK path or modify the stock application.
///
/// # Errors
///
/// Returns filesystem or report-formatting errors.
fn write_report(result: ProbeResult) -> Result<(), WriteError> {
    let report = format_report(result).map_err(|_| WriteError::Format)?;
    let mut file = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(REPORT_PATH)
        .map_err(|_| WriteError::Io)?;
    file.write_all(report.as_bytes())
        .map_err(|_| WriteError::Io)
}

#[derive(Debug)]
enum WriteError {
    Format,
    Io,
}

/// Dynamic-loader constructor. It performs bounded read-only inspection and
/// returns immediately. The function is intentionally not exported by name.
extern "C" fn start_probe() {
    let result = probe::inspect_process();
    let _ = write_report(result);
}

#[used]
#[cfg_attr(target_os = "linux", unsafe(link_section = ".init_array"))]
static HERETICC_PROBE_ENTRY: extern "C" fn() = start_probe;

#[cfg(test)]
mod tests {
    use super::{MAX_REPORT_BYTES, format_report};
    use mpc_sample_sdk::probe::{FINGERPRINT_WORDS, ProbeResult, ProbeStatus};

    #[test]
    fn report_is_bounded_and_contains_no_address_or_raw_data() {
        let report = format_report(ProbeResult {
            status: ProbeStatus::Compatible,
            checked: FINGERPRINT_WORDS,
            expected: FINGERPRINT_WORDS,
        })
        .expect("bounded report");
        assert!(report.len() <= MAX_REPORT_BYTES);
        assert!(report.contains("fingerprint_words_checked=63\n"));
        assert!(report.contains("feature_hooks_compiled=false\n"));
        assert!(!report.contains("0x"));
    }

    #[test]
    fn every_status_has_a_bounded_report() {
        for status in [
            ProbeStatus::Compatible,
            ProbeStatus::MissingImage,
            ProbeStatus::Unreadable,
            ProbeStatus::Mismatch,
            ProbeStatus::TableSizeMismatch,
        ] {
            let report = format_report(ProbeResult {
                status,
                checked: FINGERPRINT_WORDS,
                expected: FINGERPRINT_WORDS,
            })
            .expect("bounded report");
            assert!(report.len() <= MAX_REPORT_BYTES);
        }
    }
}
