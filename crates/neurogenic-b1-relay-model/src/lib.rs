//! Host-only model of the bounded B1 diagnostic relay.
//!
//! This crate deliberately contains no socket, filesystem, process, systemd,
//! firmware, or hook integration. Callers provide constructor-report bytes and
//! snapshot observations through bounded, deterministic interfaces.

use core::fmt::Write as _;

pub const CONSTRUCTOR_REPORT_LIMIT: usize = 768;
pub const SNAPSHOT_BYTES: usize = 72;
const SNAPSHOT_BYTES_U32: u32 = 72;
pub const SNAPSHOT_REPORT_LIMIT: usize = 384;
pub const REQUEST_LIMIT: usize = 1_024;
pub const HEADER_LIMIT: usize = 16;
pub const STABLE_READ_ATTEMPTS: usize = 8;

const SNAPSHOT_MAGIC: &[u8; 8] = b"HRTCB1P0";
const SNAPSHOT_VERSION: u32 = 2;
const EXPECTED_EXECUTABLE: &str = "/usr/bin/MPC Sample";
const EXPECTED_EXPERIMENT: &str = "m03b-b1p0";
const EXPECTED_PATCH: &str = "m03b-b1p0-snapshot-identity-v4";
const EXPECTED_FIRMWARE: &str = "1.3.0.12";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Limits {
    pub process_wait_ms: u64,
    pub arming_ms: u64,
    pub overall_ms: u64,
    pub request_ms: u64,
    pub snapshot_ms: u64,
    pub max_requests: u16,
}

impl Default for Limits {
    fn default() -> Self {
        Self {
            process_wait_ms: 30_000,
            arming_ms: 15_000,
            overall_ms: 120_000,
            request_ms: 2_000,
            snapshot_ms: 3_000,
            max_requests: 32,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Waiting,
    Armed,
    Reported,
    Failed,
    Expired,
}

impl State {
    #[must_use]
    pub const fn terminal(self) -> bool {
        matches!(self, Self::Reported | Self::Failed | Self::Expired)
    }

    const fn label(self) -> &'static str {
        match self {
            Self::Waiting => "WAITING",
            Self::Armed => "ARMED",
            Self::Reported => "REPORTED",
            Self::Failed => "FAILED",
            Self::Expired => "EXPIRED",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProcessIdentity {
    pub pid: u32,
    pub start_ticks: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExpectedIdentity {
    pub pid: u32,
    pub start_ticks: Option<u64>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Counters {
    pub invocations: u64,
    pub successful_forwards: u64,
    pub positive_deltas: u64,
    pub negative_deltas: u64,
    pub zero_deltas: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VerifiedSnapshot {
    pub identity: ProcessIdentity,
    pub counters: Counters,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProcessObservation<'a> {
    pub pid: u32,
    pub start_ticks: u64,
    pub executable: &'a str,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SnapshotError {
    WrongLength,
    WrongMagic,
    WrongVersion,
    Malformed,
    Unstable,
    WrongPid,
    WrongExecutable,
    WrongStartToken,
    NoCallbacks,
    NoSuccessfulForwards,
    ReportTooLarge,
    Deadline,
    Source,
}

/// Abstracts the existing B0.2 reader contract without device I/O.
///
/// A future adapter must call the unchanged reader and map its result here.
/// Host fixtures use [`validate_snapshot_reads`] to exercise equivalent bytes,
/// stability, identity, and counter checks.
pub trait SnapshotReader {
    /// Return one stable, process-verified snapshot before the supplied deadline.
    ///
    /// # Errors
    ///
    /// Returns the corresponding B0.2 validation or bounded-source failure.
    fn read_verified(
        &mut self,
        expected: ExpectedIdentity,
        deadline_ms: u64,
    ) -> Result<VerifiedSnapshot, SnapshotError>;
}

/// Validate repeated in-memory reads using the B0.2 byte and identity contract.
///
/// # Errors
///
/// Returns a precise format, stability, counter, or process-identity failure.
pub fn validate_snapshot_reads(
    reads: &[&[u8]],
    process: ProcessObservation<'_>,
) -> Result<VerifiedSnapshot, SnapshotError> {
    if process.executable != EXPECTED_EXECUTABLE {
        return Err(SnapshotError::WrongExecutable);
    }
    let stable = reads
        .windows(2)
        .take(STABLE_READ_ATTEMPTS)
        .find_map(|pair| (pair[0] == pair[1]).then_some(pair[0]))
        .ok_or(SnapshotError::Unstable)?;
    let snapshot = decode_snapshot(stable)?;
    if snapshot.identity.pid != process.pid {
        return Err(SnapshotError::WrongPid);
    }
    if snapshot.identity.start_ticks != process.start_ticks {
        return Err(SnapshotError::WrongStartToken);
    }
    Ok(snapshot)
}

fn decode_snapshot(bytes: &[u8]) -> Result<VerifiedSnapshot, SnapshotError> {
    if bytes.len() != SNAPSHOT_BYTES {
        return Err(SnapshotError::WrongLength);
    }
    if &bytes[..8] != SNAPSHOT_MAGIC {
        return Err(SnapshotError::WrongMagic);
    }
    if u32_at(bytes, 8) != SNAPSHOT_VERSION {
        return Err(SnapshotError::WrongVersion);
    }
    if u32_at(bytes, 12) != SNAPSHOT_BYTES_U32 || u32_at(bytes, 20) != 0 {
        return Err(SnapshotError::Malformed);
    }
    let counters = Counters {
        invocations: u64_at(bytes, 32),
        successful_forwards: u64_at(bytes, 40),
        positive_deltas: u64_at(bytes, 48),
        negative_deltas: u64_at(bytes, 56),
        zero_deltas: u64_at(bytes, 64),
    };
    if counters.successful_forwards > counters.invocations
        || counters
            .positive_deltas
            .saturating_add(counters.negative_deltas)
            .saturating_add(counters.zero_deltas)
            != counters.invocations
    {
        return Err(SnapshotError::Malformed);
    }
    Ok(VerifiedSnapshot {
        identity: ProcessIdentity {
            pid: u32_at(bytes, 16),
            start_ticks: u64_at(bytes, 24),
        },
        counters,
    })
}

fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("fixed range"))
}

fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().expect("fixed range"))
}

/// Format the address-free B0.2 report within its existing bound.
///
/// # Errors
///
/// Returns [`SnapshotError::ReportTooLarge`] if the fixed report exceeds 384 bytes.
pub fn format_snapshot(snapshot: VerifiedSnapshot) -> Result<String, SnapshotError> {
    let mut report = String::with_capacity(256);
    writeln!(report, "hereticc_experiment={EXPECTED_EXPERIMENT}").expect("String write");
    writeln!(report, "snapshot_version={SNAPSHOT_VERSION}").expect("String write");
    writeln!(report, "process_identity=expected").expect("String write");
    writeln!(report, "process_id={}", snapshot.identity.pid).expect("String write");
    writeln!(
        report,
        "process_start_ticks={}",
        snapshot.identity.start_ticks
    )
    .expect("String write");
    writeln!(
        report,
        "callback_invocations={}",
        snapshot.counters.invocations
    )
    .expect("String write");
    writeln!(
        report,
        "successful_forwards={}",
        snapshot.counters.successful_forwards
    )
    .expect("String write");
    writeln!(
        report,
        "positive_delta_events={}",
        snapshot.counters.positive_deltas
    )
    .expect("String write");
    writeln!(
        report,
        "negative_delta_events={}",
        snapshot.counters.negative_deltas
    )
    .expect("String write");
    writeln!(
        report,
        "zero_delta_events={}",
        snapshot.counters.zero_deltas
    )
    .expect("String write");
    if report.len() > SNAPSHOT_REPORT_LIMIT {
        return Err(SnapshotError::ReportTooLarge);
    }
    Ok(report)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ConstructorError {
    TooLarge,
    InvalidUtf8,
    Malformed,
    Duplicate,
    Missing(&'static str),
    Mismatch(&'static str),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ConstructorEvidence {
    pub pid: u32,
}

/// Parse and fail-closed validate the actual B0 constructor-report fields.
///
/// # Errors
///
/// Returns a bounded parse, duplicate, missing-field, or mismatch error.
pub fn validate_constructor_report(bytes: &[u8]) -> Result<ConstructorEvidence, ConstructorError> {
    if bytes.len() > CONSTRUCTOR_REPORT_LIMIT {
        return Err(ConstructorError::TooLarge);
    }
    let text = core::str::from_utf8(bytes).map_err(|_| ConstructorError::InvalidUtf8)?;
    let mut fields: Vec<(&str, &str)> = Vec::new();
    for line in text.lines() {
        let (key, value) = line.split_once('=').ok_or(ConstructorError::Malformed)?;
        if key.is_empty() || value.is_empty() || fields.iter().any(|(seen, _)| *seen == key) {
            return Err(if fields.iter().any(|(seen, _)| *seen == key) {
                ConstructorError::Duplicate
            } else {
                ConstructorError::Malformed
            });
        }
        fields.push((key, value));
    }
    let require = |key: &'static str, expected: &str| -> Result<(), ConstructorError> {
        match fields.iter().find(|(candidate, _)| *candidate == key) {
            Some((_, value)) if *value == expected => Ok(()),
            Some(_) => Err(ConstructorError::Mismatch(key)),
            None => Err(ConstructorError::Missing(key)),
        }
    };
    require("hereticc_experiment", EXPECTED_EXPERIMENT)?;
    require("patch_set", EXPECTED_PATCH)?;
    require("expected_firmware", EXPECTED_FIRMWARE)?;
    require("firmware_compatible", "true")?;
    require("report_category", "expected")?;
    require("sd_report_eligible", "true")?;
    require("fingerprint_words_checked", "63")?;
    require("selected_hook", "DATA_HOOK")?;
    require("required_site_prepared", "true")?;
    require("installation_succeeded", "true")?;
    require("forwarding_ready", "true")?;
    require("forwarding_observed", "false")?;
    require("post_interaction_retrieval", "shared-run-snapshot")?;
    require("counter_snapshot_ready", "true")?;
    require("final_status", "Installed")?;
    let pid = fields
        .iter()
        .find(|(key, _)| *key == "counter_snapshot_pid")
        .ok_or(ConstructorError::Missing("counter_snapshot_pid"))?
        .1
        .parse::<u32>()
        .map_err(|_| ConstructorError::Mismatch("counter_snapshot_pid"))?;
    if pid == 0 {
        return Err(ConstructorError::Mismatch("counter_snapshot_pid"));
    }
    Ok(ConstructorEvidence { pid })
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
    Status,
    Snapshot,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProtocolError {
    TooLarge,
    TooManyHeaders,
    Incomplete,
    Timeout,
    Malformed,
    UnsupportedVersion,
    UnsupportedMethod,
    UnknownPath,
    UnexpectedHeader,
    ConflictingLength,
    TransferEncoding,
    BodyForbidden,
    Pipelined,
}

/// Parse one complete, fixed-scope HTTP/1.1 request without performing I/O.
///
/// # Errors
///
/// Returns a framing, bound, deadline, method, path, or header-policy error.
pub fn parse_request(
    bytes: &[u8],
    elapsed_ms: u64,
    request_deadline_ms: u64,
) -> Result<Operation, ProtocolError> {
    if elapsed_ms > request_deadline_ms {
        return Err(ProtocolError::Timeout);
    }
    if bytes.len() > REQUEST_LIMIT {
        return Err(ProtocolError::TooLarge);
    }
    let Some(end) = bytes.windows(4).position(|window| window == b"\r\n\r\n") else {
        return Err(ProtocolError::Incomplete);
    };
    let header_end = end + 4;
    let head = core::str::from_utf8(&bytes[..end]).map_err(|_| ProtocolError::Malformed)?;
    let mut lines = head.split("\r\n");
    let request_line = lines.next().ok_or(ProtocolError::Malformed)?;
    let mut parts = request_line.split(' ');
    let method = parts.next().ok_or(ProtocolError::Malformed)?;
    let path = parts.next().ok_or(ProtocolError::Malformed)?;
    let version = parts.next().ok_or(ProtocolError::Malformed)?;
    if parts.next().is_some() || method.is_empty() || path.is_empty() {
        return Err(ProtocolError::Malformed);
    }
    if version != "HTTP/1.1" {
        return Err(ProtocolError::UnsupportedVersion);
    }
    let operation = match (method, path) {
        ("GET", "/v1/status") => Operation::Status,
        ("POST", "/v1/snapshot") => Operation::Snapshot,
        ("GET" | "POST", _) => return Err(ProtocolError::UnknownPath),
        _ => return Err(ProtocolError::UnsupportedMethod),
    };
    let mut content_length = None;
    let mut header_count = 0;
    for line in lines {
        header_count += 1;
        if header_count > HEADER_LIMIT {
            return Err(ProtocolError::TooManyHeaders);
        }
        if line.starts_with(' ') || line.starts_with('\t') {
            return Err(ProtocolError::Malformed);
        }
        let (name, value) = line.split_once(':').ok_or(ProtocolError::Malformed)?;
        if name.is_empty() || (!value.starts_with(' ') && !value.starts_with('\t')) {
            return Err(ProtocolError::Malformed);
        }
        let value = value.trim_ascii();
        if name.eq_ignore_ascii_case("transfer-encoding") {
            return Err(ProtocolError::TransferEncoding);
        }
        if name.eq_ignore_ascii_case("content-length") {
            let parsed = value
                .parse::<usize>()
                .map_err(|_| ProtocolError::Malformed)?;
            if content_length.replace(parsed).is_some() {
                return Err(ProtocolError::ConflictingLength);
            }
        } else if !name.eq_ignore_ascii_case("host") {
            return Err(ProtocolError::UnexpectedHeader);
        }
    }
    let body_length = content_length.unwrap_or(0);
    if body_length != 0 {
        return Err(ProtocolError::BodyForbidden);
    }
    if bytes.len() != header_end {
        return Err(ProtocolError::Pipelined);
    }
    Ok(operation)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RelayError {
    Constructor(ConstructorError),
    Snapshot(SnapshotError),
    WrongProcess,
    NotArmed,
    Terminal,
    RequestLimit,
    Timeout,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Response {
    Status(String),
    Snapshot(String),
}

pub struct RelayModel {
    state: State,
    limits: Limits,
    started_ms: u64,
    process_seen_ms: Option<u64>,
    observed_pid: Option<u32>,
    identity: Option<ProcessIdentity>,
    requests: u16,
}

impl RelayModel {
    #[must_use]
    pub const fn new(started_ms: u64, limits: Limits) -> Self {
        Self {
            state: State::Waiting,
            limits,
            started_ms,
            process_seen_ms: None,
            observed_pid: None,
            identity: None,
            requests: 0,
        }
    }

    #[must_use]
    pub const fn state(&self) -> State {
        self.state
    }

    /// Record the one candidate process discovered during the bounded wait.
    ///
    /// # Errors
    ///
    /// Fails and transitions terminally for invalid or late process evidence.
    pub fn observe_process(&mut self, pid: u32, now_ms: u64) -> Result<(), RelayError> {
        self.check_live(now_ms)?;
        if self.state != State::Waiting || pid == 0 {
            return self.fail(RelayError::WrongProcess);
        }
        if now_ms.saturating_sub(self.started_ms) > self.limits.process_wait_ms {
            return self.expire();
        }
        if let Some(observed) = self.observed_pid {
            return if observed == pid {
                Ok(())
            } else {
                self.fail(RelayError::WrongProcess)
            };
        }
        self.observed_pid = Some(pid);
        self.process_seen_ms = Some(now_ms);
        Ok(())
    }

    /// Validate constructor and initial snapshot evidence and enter `ARMED`.
    ///
    /// # Errors
    ///
    /// Fails closed for missing, inconsistent, invalid, or late evidence.
    pub fn arm(
        &mut self,
        report: &[u8],
        reader: &mut impl SnapshotReader,
        now_ms: u64,
    ) -> Result<(), RelayError> {
        self.check_live(now_ms)?;
        if self.state != State::Waiting {
            return Err(if self.state.terminal() {
                RelayError::Terminal
            } else {
                RelayError::NotArmed
            });
        }
        let Some(seen) = self.process_seen_ms else {
            return self.fail(RelayError::WrongProcess);
        };
        if now_ms.saturating_sub(seen) > self.limits.arming_ms {
            return self.expire();
        }
        let constructor = match validate_constructor_report(report) {
            Ok(value) => value,
            Err(error) => return self.fail(RelayError::Constructor(error)),
        };
        if Some(constructor.pid) != self.observed_pid {
            return self.fail(RelayError::WrongProcess);
        }
        let expected = ExpectedIdentity {
            pid: constructor.pid,
            start_ticks: None,
        };
        let snapshot =
            match reader.read_verified(expected, now_ms.saturating_add(self.limits.snapshot_ms)) {
                Ok(value) => value,
                Err(error) => return self.fail(RelayError::Snapshot(error)),
            };
        if snapshot.identity.pid != constructor.pid || snapshot.identity.start_ticks == 0 {
            return self.fail(RelayError::WrongProcess);
        }
        self.identity = Some(snapshot.identity);
        self.state = State::Armed;
        Ok(())
    }

    /// Apply one already-parsed operation to the deterministic relay model.
    ///
    /// # Errors
    ///
    /// Returns state, limit, timeout, identity, or snapshot validation failure.
    pub fn handle(
        &mut self,
        operation: Operation,
        reader: &mut impl SnapshotReader,
        now_ms: u64,
    ) -> Result<Response, RelayError> {
        self.check_live(now_ms)?;
        if self.state.terminal() {
            return Err(RelayError::Terminal);
        }
        self.requests = self.requests.saturating_add(1);
        if self.requests > self.limits.max_requests {
            return self.fail(RelayError::RequestLimit);
        }
        match operation {
            Operation::Status => Ok(Response::Status(format!("state={}\n", self.state.label()))),
            Operation::Snapshot if self.state != State::Armed => Err(RelayError::NotArmed),
            Operation::Snapshot => {
                let Some(expected) = self.identity else {
                    return self.fail(RelayError::WrongProcess);
                };
                let snapshot = match reader.read_verified(
                    ExpectedIdentity {
                        pid: expected.pid,
                        start_ticks: Some(expected.start_ticks),
                    },
                    now_ms.saturating_add(self.limits.snapshot_ms),
                ) {
                    Ok(value) => value,
                    Err(error) => return self.fail(RelayError::Snapshot(error)),
                };
                if snapshot.identity != expected {
                    return self.fail(RelayError::WrongProcess);
                }
                if snapshot.counters.invocations == 0 {
                    return self.fail(RelayError::Snapshot(SnapshotError::NoCallbacks));
                }
                if snapshot.counters.successful_forwards == 0 {
                    return self.fail(RelayError::Snapshot(SnapshotError::NoSuccessfulForwards));
                }
                let report = match format_snapshot(snapshot) {
                    Ok(value) => value,
                    Err(error) => return self.fail(RelayError::Snapshot(error)),
                };
                self.state = State::Reported;
                self.identity = None;
                Ok(Response::Snapshot(report))
            }
        }
    }

    /// Apply deterministic monotonic-time expiry without sleeping.
    ///
    /// # Errors
    ///
    /// Returns timeout on expiry or terminal-state rejection after cleanup.
    pub fn tick(&mut self, now_ms: u64) -> Result<(), RelayError> {
        self.check_live(now_ms)
    }

    fn check_live(&mut self, now_ms: u64) -> Result<(), RelayError> {
        if self.state.terminal() {
            return Err(RelayError::Terminal);
        }
        if now_ms.saturating_sub(self.started_ms) > self.limits.overall_ms {
            return self.expire();
        }
        if self.state == State::Waiting
            && self.process_seen_ms.is_none()
            && now_ms.saturating_sub(self.started_ms) > self.limits.process_wait_ms
        {
            return self.expire();
        }
        Ok(())
    }

    fn fail<T>(&mut self, error: RelayError) -> Result<T, RelayError> {
        self.state = State::Failed;
        self.identity = None;
        Err(error)
    }

    fn expire<T>(&mut self) -> Result<T, RelayError> {
        self.state = State::Expired;
        self.identity = None;
        Err(RelayError::Timeout)
    }
}
