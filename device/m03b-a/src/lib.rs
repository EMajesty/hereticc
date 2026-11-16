//! M0.3b-A zero-hook firmware compatibility probe.
//!
//! This library is loaded once with `LD_PRELOAD`, performs a read-only firmware
//! fingerprint check, writes one bounded report under `/run`, and returns. It
//! deliberately exports no callable C symbol and installs no callback.

use std::fmt::Write as _;
use std::fs::OpenOptions;
use std::io::Write as _;

use mpc_sample_sdk::{
    image::DiscoveryReason,
    probe::{self, ExecutableIdentity, ProbeResult, ProbeStatus},
};

mod sd_report;

use sd_report::ReportCategory;

/// HERETICC diagnostic version.
pub const PROBE_VERSION: &str = "0.3.0";
/// Exact upstream SDK revision patched by this experiment.
pub const SDK_COMMIT: &str = "e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39";
/// Human-readable patch-series identifier.
pub const PATCH_SET: &str = "m03b-a4-process-aware-v3";
/// Only supported stock firmware version.
pub const EXPECTED_FIRMWARE: &str = "1.3.0.12";
/// Transient report location on the device.
pub const REPORT_PATH: &str = "/run/hereticc-m03b-a.txt";
/// Hard upper bound for the complete report.
pub const MAX_REPORT_BYTES: usize = 768;

fn report_category(identity: ExecutableIdentity) -> ReportCategory {
    match identity {
        ExecutableIdentity::Expected => ReportCategory::Expected,
        ExecutableIdentity::Unexpected | ExecutableIdentity::Unreadable => {
            ReportCategory::Unexpected
        }
    }
}

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
    let discovery_reason = match result.discovery_reason {
        DiscoveryReason::Found => "found",
        DiscoveryReason::NoLoaderImages => "no-loader-images",
        DiscoveryReason::NoUnnamedImage => "no-unnamed-image",
        DiscoveryReason::MissingProgramHeaders => "missing-program-headers",
        DiscoveryReason::NoLoadSegment => "no-load-segment",
        DiscoveryReason::ProbeOffsetOutsideLoadSegments => "probe-offset-outside-load-segments",
        DiscoveryReason::AddressOverflow => "address-overflow",
        DiscoveryReason::LoaderIterationError => "loader-iteration-error",
    };
    let executable_identity = match result.process.executable_identity {
        ExecutableIdentity::Expected => "expected",
        ExecutableIdentity::Unexpected => "unexpected",
        ExecutableIdentity::Unreadable => "unreadable",
    };
    let category = report_category(result.process.executable_identity);
    let (category_label, category_reason) = match result.process.executable_identity {
        ExecutableIdentity::Expected => ("expected", "exact-executable-match"),
        ExecutableIdentity::Unexpected => ("unexpected", "executable-mismatch"),
        ExecutableIdentity::Unreadable => ("unexpected", "executable-identity-unreadable"),
    };
    debug_assert_eq!(
        category,
        if category_label == "expected" {
            ReportCategory::Expected
        } else {
            ReportCategory::Unexpected
        }
    );
    let loader_timing_plausible = matches!(
        result.discovery_reason,
        DiscoveryReason::NoLoaderImages | DiscoveryReason::NoUnnamedImage
    );
    writeln!(report, "hereticc_probe_version={PROBE_VERSION}").expect("String writes cannot fail");
    writeln!(report, "upstream_sdk_commit={SDK_COMMIT}").expect("String writes cannot fail");
    writeln!(report, "patch_set={PATCH_SET}").expect("String writes cannot fail");
    writeln!(report, "expected_firmware={EXPECTED_FIRMWARE}").expect("String writes cannot fail");
    writeln!(report, "image_discovery={discovery}").expect("String writes cannot fail");
    writeln!(report, "discovery_reason={discovery_reason}").expect("String writes cannot fail");
    writeln!(report, "process_id={}", result.process.pid).expect("String writes cannot fail");
    writeln!(report, "parent_process_id={}", result.process.parent_pid)
        .expect("String writes cannot fail");
    writeln!(report, "executable_identity={executable_identity}")
        .expect("String writes cannot fail");
    writeln!(report, "report_category={category_label}").expect("String writes cannot fail");
    writeln!(report, "report_category_reason={category_reason}")
        .expect("String writes cannot fail");
    writeln!(
        report,
        "expected_executable_mapped={}",
        result.process.expected_executable_mapped
    )
    .expect("String writes cannot fail");
    writeln!(
        report,
        "process_mappings_readable={}",
        result.process.mappings_readable
    )
    .expect("String writes cannot fail");
    writeln!(
        report,
        "expected_executable_mapping_count={}",
        result.process.expected_mapping_count
    )
    .expect("String writes cannot fail");
    writeln!(report, "loader_images_seen={}", result.loader_images)
        .expect("String writes cannot fail");
    writeln!(
        report,
        "rejected_named_candidates={}",
        result.rejected_named_candidates
    )
    .expect("String writes cannot fail");
    writeln!(report, "loader_timing_plausible={loader_timing_plausible}")
        .expect("String writes cannot fail");
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

fn write_reports(result: ProbeResult) -> Result<(), ReportError> {
    let category = report_category(result.process.executable_identity);
    let report = format_report(result)?;
    let _ = write_transient_report(&report);
    let _ = sd_report::write_to_detected_sd(&report, category);
    Ok(())
}

fn write_transient_report(report: &str) -> Result<(), WriteError> {
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
    Io,
}

/// Dynamic-loader constructor. It performs bounded read-only inspection and
/// returns immediately. The function is intentionally not exported by name.
extern "C" fn start_probe() {
    let result = probe::inspect_process();
    let _ = write_reports(result);
}

#[used]
#[cfg_attr(target_os = "linux", unsafe(link_section = ".init_array"))]
static HERETICC_PROBE_ENTRY: extern "C" fn() = start_probe;

#[cfg(test)]
mod tests {
    use super::{MAX_REPORT_BYTES, ReportCategory, format_report, report_category};
    use mpc_sample_sdk::{
        image::DiscoveryReason,
        probe::{ExecutableIdentity, FINGERPRINT_WORDS, ProbeResult, ProbeStatus, ProcessContext},
    };

    fn result(status: ProbeStatus) -> ProbeResult {
        ProbeResult {
            status,
            checked: if status == ProbeStatus::Compatible {
                FINGERPRINT_WORDS
            } else {
                0
            },
            expected: FINGERPRINT_WORDS,
            discovery_reason: if status == ProbeStatus::MissingImage {
                DiscoveryReason::NoUnnamedImage
            } else {
                DiscoveryReason::Found
            },
            loader_images: 4,
            rejected_named_candidates: 1,
            process: ProcessContext {
                pid: 42,
                parent_pid: 1,
                executable_identity: ExecutableIdentity::Expected,
                expected_executable_mapped: true,
                mappings_readable: true,
                expected_mapping_count: 1,
            },
        }
    }

    #[test]
    fn report_is_bounded_and_contains_no_address_or_raw_data() {
        let report = format_report(result(ProbeStatus::Compatible)).expect("bounded report");
        assert!(report.len() <= MAX_REPORT_BYTES);
        assert!(report.contains("fingerprint_words_checked=63\n"));
        assert!(report.contains("feature_hooks_compiled=false\n"));
        assert!(report.contains("process_id=42\n"));
        assert!(report.contains("discovery_reason=found\n"));
        assert!(report.contains("report_category=expected\n"));
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
            let report = format_report(result(status)).expect("bounded report");
            assert!(report.len() <= MAX_REPORT_BYTES);
        }
    }

    #[test]
    fn every_discovery_reason_has_a_bounded_documented_code() {
        for reason in [
            DiscoveryReason::Found,
            DiscoveryReason::NoLoaderImages,
            DiscoveryReason::NoUnnamedImage,
            DiscoveryReason::MissingProgramHeaders,
            DiscoveryReason::NoLoadSegment,
            DiscoveryReason::ProbeOffsetOutsideLoadSegments,
            DiscoveryReason::AddressOverflow,
            DiscoveryReason::LoaderIterationError,
        ] {
            let mut value = result(ProbeStatus::MissingImage);
            value.discovery_reason = reason;
            let report = format_report(value).expect("bounded reason report");
            assert!(report.len() <= MAX_REPORT_BYTES);
            assert!(report.contains("discovery_reason="));
        }
    }

    #[test]
    fn unreadable_identity_uses_unexpected_reporting_category() {
        assert_eq!(
            report_category(ExecutableIdentity::Unreadable),
            ReportCategory::Unexpected
        );
        assert_eq!(
            report_category(ExecutableIdentity::Unexpected),
            ReportCategory::Unexpected
        );
        assert_eq!(
            report_category(ExecutableIdentity::Expected),
            ReportCategory::Expected
        );
        let mut value = result(ProbeStatus::MissingImage);
        value.process.executable_identity = ExecutableIdentity::Unreadable;
        let report = format_report(value).expect("bounded unreadable report");
        assert!(report.contains("report_category=unexpected\n"));
        assert!(report.contains("report_category_reason=executable-identity-unreadable\n"));
    }

    #[test]
    fn maximum_width_fields_fit_report_limit() {
        let mut value = result(ProbeStatus::MissingImage);
        value.discovery_reason = DiscoveryReason::ProbeOffsetOutsideLoadSegments;
        value.process.pid = u32::MAX;
        value.process.parent_pid = u32::MAX;
        value.process.executable_identity = ExecutableIdentity::Unreadable;
        value.process.expected_mapping_count = u8::MAX;
        value.loader_images = u16::MAX;
        value.rejected_named_candidates = u16::MAX;
        let report = format_report(value).expect("maximum-width report remains bounded");
        assert!(report.len() <= MAX_REPORT_BYTES);
        assert!(report.contains("process_id=4294967295\n"));
        assert!(report.contains("loader_images_seen=65535\n"));
    }
}
