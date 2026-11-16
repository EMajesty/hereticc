//! M0.3b-B0 single-site transparent DATA-encoder hook experiment.
//!
//! The constructor is device-facing, but this stage is for source, host tests,
//! and static `AArch64` inspection only. It must not be deployed or executed on
//! an MPC without a separately authorized B1 task.

use core::fmt::Write as _;
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
#[cfg(not(test))]
use std::fs::OpenOptions;
#[cfg(not(test))]
use std::io::Write as _;

use mpc_sample_sdk::probe::{ExecutableIdentity, ProbeResult, ProbeStatus};

/// Exact upstream SDK revision.
pub const SDK_COMMIT: &str = "e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39";
/// HERETICC patch-set identifier.
pub const PATCH_SET: &str = "m03b-b0-selective-data-v1";
/// Only supported stock firmware.
pub const EXPECTED_FIRMWARE: &str = "1.3.0.12";
/// Transient report; B0 deliberately creates no SD package.
pub const REPORT_PATH: &str = "/run/hereticc-m03b-b0.txt";
/// Hard report bound inherited from the compatibility probes.
pub const MAX_REPORT_BYTES: usize = 768;

type DataHandler = unsafe extern "C" fn(usize, i32);

static ORIGINAL_DATA: AtomicUsize = AtomicUsize::new(0);
static FORWARDED_CALLS: AtomicU64 = AtomicU64::new(0);

/// Selective installation outcome used by reporting and host tests.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InstallStatus {
    /// Exact executable identity was not established.
    WrongExecutable,
    /// The complete firmware fingerprint did not match.
    IncompatibleFirmware,
    /// The required site or trampoline could not be prepared.
    PreparationFailed,
    /// Private relay/trampoline pages could not be made executable.
    SealFailed,
    /// Branch publication failed and no live patch was observed.
    CommitFailedClean,
    /// Publication failed and rollback could not prove the site restored.
    CommitFailedIndeterminate,
    /// The one selected branch was installed.
    Installed,
}

/// Bounded facts about one installation attempt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[allow(clippy::struct_excessive_bools)]
pub struct InstallResult {
    /// Final classification.
    pub status: InstallStatus,
    /// Whether the required site was prepared before publication.
    pub prepared: bool,
    /// Whether stock-code modification was attempted.
    pub modification_attempted: bool,
    /// Whether the installer observed a committed branch.
    pub installed: bool,
    /// Whether a lifetime-stable forwarding trampoline was published first.
    pub forwarding_ready: bool,
}

trait Backend {
    fn prepare_data(&mut self, handler: usize) -> Option<usize>;
    fn seal(&mut self) -> bool;
    fn publish(&mut self) -> bool;
    fn touched(&self) -> bool;
    fn abort(&mut self);
}

fn rejected(status: InstallStatus) -> InstallResult {
    InstallResult {
        status,
        prepared: false,
        modification_attempted: false,
        installed: false,
        forwarding_ready: false,
    }
}

fn install_with(result: ProbeResult, backend: &mut impl Backend) -> InstallResult {
    if result.process.executable_identity != ExecutableIdentity::Expected {
        return rejected(InstallStatus::WrongExecutable);
    }
    if result.status != ProbeStatus::Compatible || result.checked != 63 || result.expected != 63 {
        return rejected(InstallStatus::IncompatibleFirmware);
    }
    let Some(trampoline) = backend.prepare_data(data_forward as *const () as usize) else {
        backend.abort();
        return rejected(InstallStatus::PreparationFailed);
    };

    // Publish the original before a branch can make the handler reachable.
    ORIGINAL_DATA.store(trampoline, Ordering::Release);
    if !backend.seal() {
        ORIGINAL_DATA.store(0, Ordering::Release);
        backend.abort();
        return InstallResult {
            status: InstallStatus::SealFailed,
            prepared: true,
            modification_attempted: false,
            installed: false,
            forwarding_ready: false,
        };
    }
    if backend.publish() {
        return InstallResult {
            status: InstallStatus::Installed,
            prepared: true,
            modification_attempted: true,
            installed: true,
            forwarding_ready: true,
        };
    }
    let touched = backend.touched();
    if !touched {
        ORIGINAL_DATA.store(0, Ordering::Release);
    }
    InstallResult {
        status: if touched {
            InstallStatus::CommitFailedIndeterminate
        } else {
            InstallStatus::CommitFailedClean
        },
        prepared: true,
        modification_attempted: true,
        installed: false,
        forwarding_ready: touched,
    }
}

/// Transparent encoder handler: count, then call the original once unchanged.
///
/// DATA has a unit return, so calling the trampoline directly preserves its
/// complete return contract. A missing trampoline fails closed; production
/// publication stores it before the handler can become reachable.
unsafe extern "C" fn data_forward(surface: usize, delta: i32) {
    let address = ORIGINAL_DATA.load(Ordering::Acquire);
    if address == 0 {
        return;
    }
    FORWARDED_CALLS.fetch_add(1, Ordering::Relaxed);
    // SAFETY: only the DATA trampoline, with this exact ABI, is stored here.
    let original: DataHandler = unsafe { core::mem::transmute(address) };
    // SAFETY: the stock arguments are forwarded byte-for-byte and exactly once.
    unsafe { original(surface, delta) };
}

#[cfg(not(test))]
struct DeviceBackend;

#[cfg(not(test))]
impl Backend for DeviceBackend {
    fn prepare_data(&mut self, handler: usize) -> Option<usize> {
        // SAFETY: the SDK exposes only its reviewed DATA site and the handler ABI matches.
        unsafe { mpc_sample_sdk::selective_input::prepare_data(handler) }
    }

    fn seal(&mut self) -> bool {
        mpc_sample_sdk::selective_input::seal()
    }

    fn publish(&mut self) -> bool {
        mpc_sample_sdk::selective_input::publish()
    }

    fn touched(&self) -> bool {
        mpc_sample_sdk::selective_input::touched()
    }

    fn abort(&mut self) {
        mpc_sample_sdk::selective_input::abort();
    }
}

/// Format the bounded, non-sensitive constructor report.
///
/// # Errors
///
/// Returns [`ReportError::TooLarge`] if fixed fields exceed the declared bound.
pub fn format_report(probe: ProbeResult, install: InstallResult) -> Result<String, ReportError> {
    let mut out = String::with_capacity(512);
    let compatible = probe.status == ProbeStatus::Compatible && probe.checked == 63;
    writeln!(out, "hereticc_experiment=m03b-b0").expect("String writes cannot fail");
    writeln!(out, "upstream_sdk_commit={SDK_COMMIT}").expect("String writes cannot fail");
    writeln!(out, "patch_set={PATCH_SET}").expect("String writes cannot fail");
    writeln!(out, "expected_firmware={EXPECTED_FIRMWARE}").expect("String writes cannot fail");
    writeln!(out, "firmware_compatible={compatible}").expect("String writes cannot fail");
    writeln!(out, "fingerprint_words_checked={}", probe.checked)
        .expect("String writes cannot fail");
    writeln!(out, "selected_hook=DATA_HOOK").expect("String writes cannot fail");
    writeln!(out, "required_site_prepared={}", install.prepared)
        .expect("String writes cannot fail");
    writeln!(
        out,
        "stock_code_modification_attempted={}",
        install.modification_attempted
    )
    .expect("String writes cannot fail");
    writeln!(out, "installation_succeeded={}", install.installed)
        .expect("String writes cannot fail");
    writeln!(out, "forwarding_ready={}", install.forwarding_ready)
        .expect("String writes cannot fail");
    writeln!(out, "forwarding_observed=false").expect("String writes cannot fail");
    writeln!(out, "final_status={:?}", install.status).expect("String writes cannot fail");
    if out.len() > MAX_REPORT_BYTES {
        return Err(ReportError::TooLarge);
    }
    Ok(out)
}

/// Report formatting error.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReportError {
    /// Fixed fields exceeded the declared bound.
    TooLarge,
}

#[cfg(not(test))]
extern "C" fn start_probe() {
    let probe = mpc_sample_sdk::probe::inspect_process();
    let install = install_with(probe, &mut DeviceBackend);
    if let Ok(report) = format_report(probe, install)
        && let Ok(mut file) = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(REPORT_PATH)
    {
        let _ = file.write_all(report.as_bytes());
    }
}

#[used]
#[cfg(not(test))]
#[cfg_attr(target_os = "linux", unsafe(link_section = ".init_array"))]
static HERETICC_B0_ENTRY: extern "C" fn() = start_probe;

#[cfg(test)]
mod tests {
    use super::*;
    use mpc_sample_sdk::{
        image::DiscoveryReason,
        probe::{FINGERPRINT_WORDS, ProcessContext},
    };
    use std::sync::atomic::AtomicI32;

    #[derive(Default)]
    struct FakeBackend {
        trampoline: Option<usize>,
        seal: bool,
        publish: bool,
        touched: bool,
        prepare_calls: usize,
        seal_calls: usize,
        publish_calls: usize,
        abort_calls: usize,
    }

    impl Backend for FakeBackend {
        fn prepare_data(&mut self, _: usize) -> Option<usize> {
            self.prepare_calls += 1;
            self.trampoline
        }
        fn seal(&mut self) -> bool {
            self.seal_calls += 1;
            self.seal
        }
        fn publish(&mut self) -> bool {
            self.publish_calls += 1;
            self.publish
        }
        fn touched(&self) -> bool {
            self.touched
        }
        fn abort(&mut self) {
            self.abort_calls += 1;
        }
    }

    fn probe(identity: ExecutableIdentity, status: ProbeStatus) -> ProbeResult {
        ProbeResult {
            status,
            checked: if status == ProbeStatus::Compatible {
                FINGERPRINT_WORDS
            } else {
                1
            },
            expected: FINGERPRINT_WORDS,
            discovery_reason: DiscoveryReason::Found,
            loader_images: 1,
            rejected_named_candidates: 0,
            process: ProcessContext {
                pid: 1,
                parent_pid: 0,
                executable_identity: identity,
                expected_executable_mapped: identity == ExecutableIdentity::Expected,
                mappings_readable: true,
                expected_mapping_count: u8::from(identity == ExecutableIdentity::Expected),
            },
        }
    }

    #[test]
    fn wrong_executable_and_firmware_mismatch_never_prepare() {
        for value in [
            probe(ExecutableIdentity::Unexpected, ProbeStatus::Compatible),
            probe(ExecutableIdentity::Expected, ProbeStatus::Mismatch),
        ] {
            let mut backend = FakeBackend::default();
            let result = install_with(value, &mut backend);
            assert!(!result.modification_attempted);
            assert_eq!(backend.prepare_calls, 0);
        }
    }

    #[test]
    fn missing_site_fails_before_modification() {
        let mut backend = FakeBackend::default();
        let result = install_with(
            probe(ExecutableIdentity::Expected, ProbeStatus::Compatible),
            &mut backend,
        );
        assert_eq!(result.status, InstallStatus::PreparationFailed);
        assert!(!result.modification_attempted);
        assert_eq!(backend.abort_calls, 1);
    }

    #[test]
    fn commit_failures_distinguish_clean_and_indeterminate_results() {
        for touched in [false, true] {
            let mut backend = FakeBackend {
                trampoline: Some(1),
                seal: true,
                touched,
                ..FakeBackend::default()
            };
            let result = install_with(
                probe(ExecutableIdentity::Expected, ProbeStatus::Compatible),
                &mut backend,
            );
            assert!(result.modification_attempted);
            assert!(!result.installed);
            assert_eq!(result.forwarding_ready, touched);
        }
    }

    #[test]
    fn seal_failure_precedes_stock_modification() {
        let mut backend = FakeBackend {
            trampoline: Some(1),
            seal: false,
            ..FakeBackend::default()
        };
        let result = install_with(
            probe(ExecutableIdentity::Expected, ProbeStatus::Compatible),
            &mut backend,
        );
        assert_eq!(result.status, InstallStatus::SealFailed);
        assert!(result.prepared);
        assert!(!result.modification_attempted);
        assert_eq!(backend.publish_calls, 0);
        assert_eq!(backend.abort_calls, 1);
    }

    #[test]
    fn successful_publication_reports_one_ready_selected_hook() {
        let mut backend = FakeBackend {
            trampoline: Some(1),
            seal: true,
            publish: true,
            ..FakeBackend::default()
        };
        let result = install_with(
            probe(ExecutableIdentity::Expected, ProbeStatus::Compatible),
            &mut backend,
        );
        assert_eq!(result.status, InstallStatus::Installed);
        assert!(result.installed);
        assert!(result.forwarding_ready);
        assert_eq!(backend.prepare_calls, 1);
        assert_eq!(backend.seal_calls, 1);
        assert_eq!(backend.publish_calls, 1);
    }

    static CALLS: AtomicU64 = AtomicU64::new(0);
    static SURFACE: AtomicUsize = AtomicUsize::new(0);
    static DELTA: AtomicI32 = AtomicI32::new(0);

    unsafe extern "C" fn original(surface: usize, delta: i32) {
        CALLS.fetch_add(1, Ordering::Relaxed);
        SURFACE.store(surface, Ordering::Relaxed);
        DELTA.store(delta, Ordering::Relaxed);
    }

    #[test]
    fn handler_forwards_arguments_exactly_once() {
        CALLS.store(0, Ordering::Relaxed);
        ORIGINAL_DATA.store(original as *const () as usize, Ordering::Release);
        // SAFETY: test original has the exact handler ABI.
        unsafe { data_forward(0x1234, -7) };
        assert_eq!(CALLS.load(Ordering::Relaxed), 1);
        assert_eq!(SURFACE.load(Ordering::Relaxed), 0x1234);
        assert_eq!(DELTA.load(Ordering::Relaxed), -7);
    }

    #[test]
    fn report_is_bounded_and_does_not_claim_observed_forwarding() {
        let value = probe(ExecutableIdentity::Expected, ProbeStatus::Compatible);
        let report =
            format_report(value, rejected(InstallStatus::PreparationFailed)).expect("bounded");
        assert!(report.len() <= MAX_REPORT_BYTES);
        assert!(report.contains("selected_hook=DATA_HOOK\n"));
        assert!(report.contains("forwarding_observed=false\n"));
        assert!(!report.contains("0x"));
    }
}
