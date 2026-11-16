//! Host-testable core for the bounded, pre-restart B1 lifecycle collector.
//!
//! This crate deliberately contains no process spawning, device filesystem
//! adapter, network API, service control, hook code, or executable entry point.

use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

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
    let mut address_interface = None;
    let mut route_interface = None;
    let mut link_up = None;
    for line in text.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        if let Some(position) = words.iter().position(|word| *word == "192.168.155.1/28") {
            address_interface = position
                .checked_sub(1)
                .and_then(|index| words.get(index))
                .copied();
        } else if line.contains("192.168.155.1/") {
            return Err(CollectionError::InvalidProcess("incorrect USB prefix"));
        }
        if words.first() == Some(&"192.168.155.0/28") {
            route_interface = words
                .iter()
                .position(|word| *word == "dev")
                .and_then(|index| words.get(index + 1))
                .copied();
        }
        if line.starts_with("LINK ") {
            let interface = words.get(1).copied();
            let state = words.get(2).copied();
            if interface == address_interface {
                link_up = state.map(|value| value == "UP");
            }
        }
    }
    match (address_interface, route_interface) {
        (Some(address), Some(route)) if address == route && safe_token(address) => {
            insert(fields, "usb_interface", address);
            insert(fields, "usb_ipv4", "192.168.155.1/28");
            insert(fields, "usb_route", "192.168.155.0/28");
            insert(fields, "usb_route_interface", route);
            insert(
                fields,
                "usb_link_up",
                link_up.map_or("unknown", |up| if up { "true" } else { "false" }),
            );
            if link_up.is_none() {
                unavailable.insert("link_state");
            }
        }
        (Some(_), Some(_)) => return Err(CollectionError::InvalidProcess("USB route mismatch")),
        _ => {
            unavailable.insert("network");
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
pub struct ValidatedDestination(PathBuf);

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MountEvidence<'a> {
    pub source: &'a str,
    pub filesystem: &'a str,
    pub read_write: bool,
    pub mount_path: &'a Path,
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
        Ok(Self(path.to_path_buf()))
    }
}

/// Publish one fixed-name report through a no-clobber partial file and hard link.
///
/// # Errors
///
/// Returns an I/O error for oversize output, existing partial/final files,
/// write/sync/link failures, or an inaccessible destination.
pub fn publish_once(destination: &ValidatedDestination, report: &[u8]) -> io::Result<PathBuf> {
    publish_once_with(destination, report, || Ok(()))
}

fn publish_once_with(
    destination: &ValidatedDestination,
    report: &[u8],
    after_write: impl FnOnce() -> io::Result<()>,
) -> io::Result<PathBuf> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "report exceeds bound",
        ));
    }
    let partial = destination.0.join(PARTIAL_NAME);
    let final_path = destination.0.join(RESULT_NAME);
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    options.mode(0o600);
    let mut file = options.open(&partial)?;
    let write_result = (|| {
        file.write_all(report)?;
        after_write()?;
        file.sync_all()?;
        fs::hard_link(&partial, &final_path)?;
        let directory = fs::File::open(&destination.0)?;
        directory.sync_all()?;
        Ok(())
    })();
    drop(file);
    let _ = fs::remove_file(&partial);
    write_result.map(|()| final_path)
}

#[cfg(test)]
mod publication_failure_tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn failure_after_write_removes_partial_and_never_publishes_final() {
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
        };
        let destination = ValidatedDestination::inspect_exact(&root, &evidence).expect("validate");
        let error = publish_once_with(&destination, b"report\n", || {
            Err(io::Error::new(io::ErrorKind::StorageFull, "injected"))
        })
        .expect_err("injected write-stage failure");
        assert_eq!(error.kind(), io::ErrorKind::StorageFull);
        assert!(!root.join(PARTIAL_NAME).exists());
        assert!(!root.join(RESULT_NAME).exists());
        fs::remove_dir(root).expect("cleanup fixture");
    }
}
