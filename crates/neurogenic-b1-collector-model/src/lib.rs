//! Host-testable core for the bounded, pre-restart B1 lifecycle collector.
//!
//! This crate deliberately contains no process spawning, device filesystem
//! adapter, network API, service control, hook code, or executable entry point.

use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

#[cfg(unix)]
use std::os::unix::fs::{MetadataExt, OpenOptionsExt};

pub const SCHEMA: &str = "neurogenic-b1-p3.2c-v1";
pub const MAX_REPORT_BYTES: usize = 4096;
pub const MAX_FIELDS: usize = 32;
pub const MAX_VALUE_BYTES: usize = 128;
pub const MAX_SOURCE_BYTES: usize = 16 * 1024;
pub const RESULT_NAME: &str = "b1-p3-2c-report.txt";
const PARTIAL_NAME: &str = ".b1-p3-2c-report.partial";
const STOCK_EXECUTABLE: &str = "/usr/bin/MPC Sample";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CollectionStatus {
    Complete,
    Partial,
    Failed,
}

impl CollectionStatus {
    const fn as_str(self) -> &'static str {
        match self {
            Self::Complete => "complete",
            Self::Partial => "partial",
            Self::Failed => "failed",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CollectionError {
    DeadlineExpired,
    SourceUnavailable(&'static str),
    SourceTooLarge(&'static str),
    InvalidUtf8(&'static str),
    InvalidService(&'static str),
    InvalidProcess(&'static str),
    InvalidNetwork(&'static str),
    ReportBounds,
}

pub trait MonotonicClock {
    fn now_ms(&mut self) -> u64;
}

#[derive(Clone, Debug, Default)]
pub struct ObservationInputs {
    pub unavailable_sources: BTreeSet<&'static str>,
    pub service: Vec<u8>,
    pub executable: Vec<u8>,
    pub stat_before: Vec<u8>,
    pub status: Vec<u8>,
    pub cgroup: Vec<u8>,
    pub stat_after: Vec<u8>,
    pub run_mount: Option<Vec<u8>>,
    pub network: Option<Vec<u8>>,
    pub sockets: Option<Vec<u8>>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CollectionOutcome {
    pub status: CollectionStatus,
    pub report: Vec<u8>,
    pub unavailable: BTreeSet<&'static str>,
}

fn checkpoint(clock: &mut impl MonotonicClock, deadline_ms: u64) -> Result<(), CollectionError> {
    if clock.now_ms() >= deadline_ms {
        Err(CollectionError::DeadlineExpired)
    } else {
        Ok(())
    }
}

fn bounded_text<'a>(name: &'static str, bytes: &'a [u8]) -> Result<&'a str, CollectionError> {
    if bytes.len() > MAX_SOURCE_BYTES {
        return Err(CollectionError::SourceTooLarge(name));
    }
    std::str::from_utf8(bytes).map_err(|_| CollectionError::InvalidUtf8(name))
}

fn properties(text: &str) -> BTreeMap<&str, &str> {
    text.lines()
        .filter_map(|line| line.split_once('='))
        .collect()
}

fn safe_token(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"_.@:-".contains(&byte))
}

fn safe_path(value: &str) -> bool {
    value.starts_with('/')
        && value.len() <= 128
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"_./@:-".contains(&byte))
}

fn insert(
    fields: &mut BTreeMap<&'static str, String>,
    key: &'static str,
    value: impl Into<String>,
) {
    fields.insert(key, value.into());
}

fn parse_stat(text: &str) -> Option<(u32, u64)> {
    let open = text.find('(')?;
    let close = text.rfind(')')?;
    let pid = text[..open].trim().parse().ok()?;
    let fields: Vec<_> = text[close + 1..].split_whitespace().collect();
    let start = fields.get(19)?.parse().ok()?;
    Some((pid, start))
}

fn parse_status(text: &str) -> Option<(u32, u32)> {
    let mut uid = None;
    let mut gid = None;
    for line in text.lines() {
        if let Some(value) = line.strip_prefix("Uid:") {
            uid = value.split_whitespace().next()?.parse().ok();
        } else if let Some(value) = line.strip_prefix("Gid:") {
            gid = value.split_whitespace().next()?.parse().ok();
        }
    }
    Some((uid?, gid?))
}

/// Normalize already-bounded observations. The caller must ensure each future
/// blocking device read is independently capped by the same absolute deadline.
///
/// # Errors
///
/// Returns an error on deadline expiry, oversized or malformed evidence,
/// incoherent stock-process identity, or report-bound violations.
pub fn collect(
    inputs: &ObservationInputs,
    clock: &mut impl MonotonicClock,
    deadline_ms: u64,
) -> Result<CollectionOutcome, CollectionError> {
    checkpoint(clock, deadline_ms)?;
    let mut fields = BTreeMap::new();
    insert(&mut fields, "evidence_schema", SCHEMA);
    insert(&mut fields, "collection_phase", "pre-restart");
    insert(&mut fields, "restart_observed", "false");
    let main_pid = parse_service(inputs, &mut fields)?;
    checkpoint(clock, deadline_ms)?;
    parse_process(inputs, main_pid, &mut fields)?;
    checkpoint(clock, deadline_ms)?;

    let mut unavailable = BTreeSet::new();
    let run = (!inputs.unavailable_sources.contains("run"))
        .then_some(inputs.run_mount.as_deref())
        .flatten();
    parse_run(run, &mut fields, &mut unavailable)?;
    checkpoint(clock, deadline_ms)?;
    let network = (!inputs.unavailable_sources.contains("network"))
        .then_some(inputs.network.as_deref())
        .flatten();
    parse_network(network, &mut fields, &mut unavailable)?;
    checkpoint(clock, deadline_ms)?;
    let sockets = (!inputs.unavailable_sources.contains("sockets"))
        .then_some(inputs.sockets.as_deref())
        .flatten();
    parse_sockets(sockets, &mut fields, &mut unavailable)?;
    checkpoint(clock, deadline_ms)?;

    let status = if unavailable.is_empty() {
        CollectionStatus::Complete
    } else {
        CollectionStatus::Partial
    };
    insert(&mut fields, "collection_status", status.as_str());
    let report = format_report(&fields)?;
    checkpoint(clock, deadline_ms)?;
    Ok(CollectionOutcome {
        status,
        report,
        unavailable,
    })
}

fn parse_service(
    inputs: &ObservationInputs,
    fields: &mut BTreeMap<&'static str, String>,
) -> Result<u32, CollectionError> {
    if inputs.unavailable_sources.contains("service") {
        return Err(CollectionError::SourceUnavailable("service"));
    }
    let service = bounded_text("service", &inputs.service)?;
    let props = properties(service);
    let main_pid: u32 = props
        .get("MainPID")
        .ok_or(CollectionError::InvalidService("missing MainPID"))?
        .parse()
        .map_err(|_| CollectionError::InvalidService("malformed MainPID"))?;
    if main_pid == 0 {
        return Err(CollectionError::InvalidService("zero MainPID"));
    }
    insert(fields, "service_main_pid", main_pid.to_string());
    for (source, output) in [
        ("Type", "service_type"),
        ("KillMode", "service_kill_mode"),
        ("Restart", "service_restart"),
    ] {
        if let Some(value) = props.get(source).filter(|value| safe_token(value)) {
            insert(fields, output, *value);
        }
    }
    if let Some(value) = props.get("ControlGroup").filter(|value| safe_path(value)) {
        insert(fields, "service_cgroup", *value);
    }
    if let Some(value) = props
        .get("SystemdVersion")
        .filter(|value| value.bytes().all(|byte| byte.is_ascii_digit()))
    {
        insert(fields, "systemd_version", *value);
    }
    Ok(main_pid)
}

fn parse_process(
    inputs: &ObservationInputs,
    main_pid: u32,
    fields: &mut BTreeMap<&'static str, String>,
) -> Result<(), CollectionError> {
    if inputs.unavailable_sources.contains("process") {
        return Err(CollectionError::SourceUnavailable("process"));
    }
    let executable = bounded_text("executable", &inputs.executable)?.trim_end();
    if executable != STOCK_EXECUTABLE {
        return Err(CollectionError::InvalidProcess("wrong executable"));
    }
    insert(fields, "process_executable", "stock-exact");

    let before = parse_stat(bounded_text("stat_before", &inputs.stat_before)?)
        .ok_or(CollectionError::InvalidProcess("malformed initial stat"))?;
    let after = parse_stat(bounded_text("stat_after", &inputs.stat_after)?)
        .ok_or(CollectionError::InvalidProcess("malformed final stat"))?;
    if before.0 != main_pid || after.0 != main_pid {
        return Err(CollectionError::InvalidProcess("PID changed"));
    }
    if before.1 != after.1 {
        return Err(CollectionError::InvalidProcess("start token changed"));
    }
    insert(fields, "process_pid", main_pid.to_string());
    insert(fields, "process_start_ticks", before.1.to_string());

    let (uid, gid) = parse_status(bounded_text("status", &inputs.status)?)
        .ok_or(CollectionError::InvalidProcess("malformed status"))?;
    insert(fields, "process_uid", uid.to_string());
    insert(fields, "process_gid", gid.to_string());
    let cgroup = bounded_text("cgroup", &inputs.cgroup)?.trim();
    let process_cgroup = cgroup
        .split_once("::")
        .map_or(cgroup, |(_, path)| path)
        .trim();
    if !safe_path(process_cgroup) {
        return Err(CollectionError::InvalidProcess("malformed cgroup"));
    }
    if let Some(service_cgroup) = fields.get("service_cgroup") {
        if service_cgroup != process_cgroup {
            return Err(CollectionError::InvalidProcess("cgroup mismatch"));
        }
    }
    insert(fields, "process_cgroup", process_cgroup);
    Ok(())
}

fn parse_run(
    input: Option<&[u8]>,
    fields: &mut BTreeMap<&'static str, String>,
    unavailable: &mut BTreeSet<&'static str>,
) -> Result<(), CollectionError> {
    let Some(input) = input else {
        unavailable.insert("run");
        return Ok(());
    };
    let text = bounded_text("run_mount", input)?;
    let mut found_mount = false;
    let mut found_run_mode = false;
    let mut found_runtime_mode = false;
    for line in text.lines() {
        if let Some(value) = line.strip_prefix("RUN_MODE=").filter(|value| {
            !value.is_empty() && value.bytes().all(|byte| matches!(byte, b'0'..=b'7'))
        }) {
            insert(fields, "run_mode", value);
            found_run_mode = true;
        } else if let Some(value) = line.strip_prefix("RUNTIME_UNIT_DIR_MODE=").filter(|value| {
            !value.is_empty() && value.bytes().all(|byte| matches!(byte, b'0'..=b'7'))
        }) {
            insert(fields, "runtime_unit_dir_mode", value);
            found_runtime_mode = true;
        } else if line.contains(" /run ") || line.starts_with("/run ") {
            let words: Vec<_> = line.split_whitespace().collect();
            if let Some(kind) = words
                .iter()
                .find(|word| matches!(**word, "tmpfs" | "ramfs"))
            {
                insert(fields, "run_filesystem", *kind);
                insert(
                    fields,
                    "run_read_write",
                    if words
                        .iter()
                        .any(|word| word.split(',').any(|item| item == "rw"))
                    {
                        "true"
                    } else {
                        "false"
                    },
                );
                found_mount = true;
            }
        }
    }
    if !(found_mount && found_run_mode && found_runtime_mode) {
        unavailable.insert("run");
    }
    Ok(())
}

#[allow(clippy::too_many_lines)] // Keeping collection and cross-field validation together is safer.
fn parse_network(
    input: Option<&[u8]>,
    fields: &mut BTreeMap<&'static str, String>,
    unavailable: &mut BTreeSet<&'static str>,
) -> Result<(), CollectionError> {
    let Some(input) = input else {
        unavailable.insert("network");
        return Ok(());
    };
    let text = bounded_text("network", input)?;
    let mut address_interfaces = BTreeSet::new();
    let mut route_interfaces = BTreeSet::new();
    let mut link_states: BTreeMap<&str, BTreeSet<&str>> = BTreeMap::new();
    for line in text.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        if let Some(position) = words.iter().position(|word| *word == "192.168.155.1/28") {
            let interface = position
                .checked_sub(1)
                .and_then(|index| words.get(index))
                .copied();
            if let Some(interface) = interface.filter(|value| safe_token(value)) {
                address_interfaces.insert(interface);
            } else {
                return Err(CollectionError::InvalidNetwork(
                    "malformed address interface",
                ));
            }
        } else if line.contains("192.168.155.1/") {
            return Err(CollectionError::InvalidNetwork("incorrect USB prefix"));
        }
        if words.first() == Some(&"192.168.155.0/28") {
            let interface = words
                .iter()
                .position(|word| *word == "dev")
                .and_then(|index| words.get(index + 1))
                .copied();
            if let Some(interface) = interface.filter(|value| safe_token(value)) {
                route_interfaces.insert(interface);
            } else {
                return Err(CollectionError::InvalidNetwork("malformed route interface"));
            }
        }
        if line.starts_with("LINK ") {
            match (words.get(1).copied(), words.get(2).copied(), words.len()) {
                (Some(interface), Some(state @ ("UP" | "DOWN" | "UNKNOWN")), 3)
                    if safe_token(interface) =>
                {
                    link_states.entry(interface).or_default().insert(state);
                }
                _ => return Err(CollectionError::InvalidNetwork("malformed link state")),
            }
        }
    }
    match (address_interfaces.len(), route_interfaces.len()) {
        (0, _) | (_, 0) => {
            unavailable.insert("network");
        }
        (1, 1) => {
            let address = address_interfaces
                .first()
                .copied()
                .ok_or(CollectionError::InvalidNetwork("missing address"))?;
            let route = route_interfaces
                .first()
                .copied()
                .ok_or(CollectionError::InvalidNetwork("missing route"))?;
            if address != route {
                return Err(CollectionError::InvalidNetwork("USB route mismatch"));
            }
            insert(fields, "usb_interface", address);
            insert(fields, "usb_ipv4", "192.168.155.1/28");
            insert(fields, "usb_route", "192.168.155.0/28");
            insert(fields, "usb_route_interface", route);
            match link_states.get(address) {
                None => {
                    insert(fields, "usb_link_up", "unknown");
                    unavailable.insert("link_state");
                }
                Some(states) if states.len() == 1 => {
                    let state = states.first().copied().unwrap_or("UNKNOWN");
                    insert(
                        fields,
                        "usb_link_up",
                        if state == "UP" {
                            "true"
                        } else if state == "DOWN" {
                            "false"
                        } else {
                            "unknown"
                        },
                    );
                    if state == "UNKNOWN" {
                        unavailable.insert("link_state");
                    }
                }
                Some(_) => {
                    return Err(CollectionError::InvalidNetwork("contradictory link states"));
                }
            }
        }
        _ => {
            return Err(CollectionError::InvalidNetwork(
                "contradictory USB interfaces",
            ));
        }
    }
    Ok(())
}

fn parse_sockets(
    input: Option<&[u8]>,
    fields: &mut BTreeMap<&'static str, String>,
    unavailable: &mut BTreeSet<&'static str>,
) -> Result<(), CollectionError> {
    let Some(input) = input else {
        unavailable.insert("sockets");
        return Ok(());
    };
    let text = bounded_text("sockets", input)?;
    let mut exact = BTreeSet::new();
    let mut wildcard = BTreeSet::new();
    for line in text.lines() {
        for word in line.split_whitespace() {
            if let Some(port) = word.strip_prefix("192.168.155.1:") {
                if let Ok(port) = port.parse::<u16>() {
                    exact.insert(port);
                }
            } else if let Some(port) = word
                .strip_prefix("0.0.0.0:")
                .or_else(|| word.strip_prefix("[::]:"))
            {
                if let Ok(port) = port.parse::<u16>() {
                    wildcard.insert(port);
                }
            }
        }
    }
    insert(fields, "listener_exact_ports", ports(&exact));
    insert(fields, "listener_wildcard_ports", ports(&wildcard));
    Ok(())
}

fn ports(values: &BTreeSet<u16>) -> String {
    if values.is_empty() {
        "none".into()
    } else {
        values
            .iter()
            .map(u16::to_string)
            .collect::<Vec<_>>()
            .join(",")
    }
}

/// Format normalized fields in deterministic order.
///
/// # Errors
///
/// Returns [`CollectionError::ReportBounds`] for invalid keys or values, too
/// many fields, non-ASCII content, or output exceeding the fixed size limit.
pub fn format_report(fields: &BTreeMap<&'static str, String>) -> Result<Vec<u8>, CollectionError> {
    if fields.len() > MAX_FIELDS
        || fields.iter().any(|(key, value)| {
            key.is_empty()
                || value.len() > MAX_VALUE_BYTES
                || !key
                    .bytes()
                    .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
                || !value.is_ascii()
                || value.contains(['\r', '\n', '='])
        })
    {
        return Err(CollectionError::ReportBounds);
    }
    let mut output = Vec::new();
    for (key, value) in fields {
        output.extend_from_slice(key.as_bytes());
        output.push(b'=');
        output.extend_from_slice(value.as_bytes());
        output.push(b'\n');
    }
    if output.len() > MAX_REPORT_BYTES {
        return Err(CollectionError::ReportBounds);
    }
    Ok(output)
}

/// Produce the only permitted failure report; it contains no raw error data.
///
/// # Errors
///
/// Returns [`CollectionError::ReportBounds`] only if the fixed schema itself
/// violates the formatter contract.
pub fn format_failure_report() -> Result<Vec<u8>, CollectionError> {
    format_report(&BTreeMap::from([
        ("collection_phase", "pre-restart".to_owned()),
        ("collection_status", "failed".to_owned()),
        ("evidence_schema", SCHEMA.to_owned()),
        ("restart_observed", "false".to_owned()),
    ]))
}

#[derive(Clone, Debug)]
pub struct ValidatedDestination {
    path: PathBuf,
    #[cfg(unix)]
    device: u64,
    #[cfg(unix)]
    inode: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MountEvidence<'a> {
    pub source: &'a str,
    pub filesystem: &'a str,
    pub read_write: bool,
    pub mount_path: &'a Path,
    pub exclusive_create_supported: bool,
}

impl ValidatedDestination {
    /// Validate one exact, existing directory without following a final symlink.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when metadata cannot be read or the target is not
    /// an exact directory. Physical SD identity remains a future adapter gate.
    pub fn inspect_exact(path: &Path, evidence: &MountEvidence<'_>) -> io::Result<Self> {
        if evidence.source != "/dev/mmcblk1p1"
            || evidence.filesystem != "exfat"
            || !evidence.read_write
            || evidence.mount_path != path
            || !evidence.exclusive_create_supported
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "destination mount identity mismatch",
            ));
        }
        let metadata = fs::symlink_metadata(path)?;
        if metadata.file_type().is_symlink() || !metadata.is_dir() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "destination is not an exact directory",
            ));
        }
        Ok(Self {
            path: path.to_path_buf(),
            #[cfg(unix)]
            device: metadata.dev(),
            #[cfg(unix)]
            inode: metadata.ino(),
        })
    }
}

/// Publish one fixed-name report directly with exclusive creation.
///
/// # Errors
///
/// Returns an I/O error for oversize output, changed destination identity,
/// existing final files, write/sync failures, or an inaccessible destination.
pub fn publish_once(destination: &ValidatedDestination, report: &[u8]) -> io::Result<PathBuf> {
    publish_once_with(destination, report, |_| Ok(()))
}

/// Write the complete bounded report, correctly handling short writes.
///
/// # Errors
///
/// Returns an error for oversized reports, zero-progress writes, or backend
/// failures such as a full filesystem.
pub fn write_report(writer: &mut impl Write, report: &[u8]) -> io::Result<()> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "report exceeds bound",
        ));
    }
    writer.write_all(report)
}

fn publish_once_with(
    destination: &ValidatedDestination,
    report: &[u8],
    mut inject: impl FnMut(PublicationStage) -> io::Result<()>,
) -> io::Result<PathBuf> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "report exceeds bound",
        ));
    }
    let metadata = fs::symlink_metadata(&destination.path)?;
    if metadata.file_type().is_symlink() || !metadata.is_dir() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "destination changed after validation",
        ));
    }
    #[cfg(unix)]
    if metadata.dev() != destination.device || metadata.ino() != destination.inode {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "destination identity changed after validation",
        ));
    }
    let final_path = destination.path.join(RESULT_NAME);
    if destination.path.join(PARTIAL_NAME).try_exists()? {
        return Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            "reserved partial marker already exists",
        ));
    }
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    options.mode(0o600);
    let mut file = options.open(&final_path)?;
    let write_result = (|| {
        write_report(&mut file, report)?;
        inject(PublicationStage::BeforeFileSync)?;
        file.sync_all()?;
        inject(PublicationStage::BeforeDirectorySync)?;
        let directory = fs::File::open(&destination.path)?;
        directory.sync_all()?;
        Ok(())
    })();
    drop(file);
    write_result.map(|()| final_path)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PublicationStage {
    BeforeFileSync,
    BeforeDirectorySync,
}

#[cfg(test)]
mod publication_failure_tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn failures_before_sync_completion_leave_non_retryable_final() {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock after epoch")
            .as_nanos();
        let root = std::env::temp_dir().join(format!("neurogenic-p3-2d-partial-{unique}"));
        fs::create_dir(&root).expect("create fixture");
        let evidence = MountEvidence {
            source: "/dev/mmcblk1p1",
            filesystem: "exfat",
            read_write: true,
            mount_path: &root,
            exclusive_create_supported: true,
        };
        for stage in [
            PublicationStage::BeforeFileSync,
            PublicationStage::BeforeDirectorySync,
        ] {
            let destination =
                ValidatedDestination::inspect_exact(&root, &evidence).expect("validate");
            let error = publish_once_with(&destination, b"report\n", |current| {
                if current == stage {
                    Err(io::Error::new(io::ErrorKind::StorageFull, "injected"))
                } else {
                    Ok(())
                }
            })
            .expect_err("injected sync-stage failure");
            assert_eq!(error.kind(), io::ErrorKind::StorageFull);
            assert!(root.join(RESULT_NAME).exists());
            fs::remove_file(root.join(RESULT_NAME)).expect("reset fixture");
        }
        fs::remove_dir_all(root).expect("cleanup fixture");
    }
}
