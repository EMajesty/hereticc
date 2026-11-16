use std::collections::VecDeque;

use neurogenic_b1_relay_model::{
    ConstructorError, Counters, ExpectedIdentity, Limits, Operation, ProcessIdentity,
    ProcessObservation, ProtocolError, RelayError, RelayModel, Response, SNAPSHOT_BYTES,
    SNAPSHOT_REPORT_LIMIT, SnapshotError, SnapshotReader, State, VerifiedSnapshot, format_snapshot,
    parse_request, validate_constructor_report, validate_snapshot_reads,
};

const PID: u32 = 4242;
const START: u64 = 987_654;

fn report() -> String {
    format!(
        "hereticc_experiment=m03b-b1p0\n\
upstream_sdk_commit=e62ffd33e33b0a37379fb0f5e47ae48cfdc53a39\n\
patch_set=m03b-b1p0-snapshot-identity-v4\n\
expected_firmware=1.3.0.12\n\
firmware_compatible=true\n\
report_category=expected\n\
sd_report_eligible=true\n\
fingerprint_words_checked=63\n\
selected_hook=DATA_HOOK\n\
required_site_prepared=true\n\
stock_code_modification_attempted=true\n\
installation_succeeded=true\n\
forwarding_ready=true\n\
forwarding_observed=false\n\
post_interaction_retrieval=shared-run-snapshot\n\
counter_snapshot_ready=true\n\
counter_snapshot_pid={PID}\n\
text_permissions_restored=unknown\n\
final_status=Installed\n"
    )
}

fn replace_field(source: &str, key: &str, value: &str) -> String {
    source
        .lines()
        .map(|line| {
            line.strip_prefix(&format!("{key}="))
                .map_or_else(|| line.to_owned(), |_| format!("{key}={value}"))
        })
        .collect::<Vec<_>>()
        .join("\n")
        + "\n"
}

fn snapshot_bytes(pid: u32, start: u64, counters: Counters) -> [u8; SNAPSHOT_BYTES] {
    let mut bytes = [0; SNAPSHOT_BYTES];
    bytes[..8].copy_from_slice(b"HRTCB1P0");
    bytes[8..12].copy_from_slice(&2_u32.to_le_bytes());
    bytes[12..16].copy_from_slice(&72_u32.to_le_bytes());
    bytes[16..20].copy_from_slice(&pid.to_le_bytes());
    bytes[24..32].copy_from_slice(&start.to_le_bytes());
    for (offset, value) in [
        (32, counters.invocations),
        (40, counters.successful_forwards),
        (48, counters.positive_deltas),
        (56, counters.negative_deltas),
        (64, counters.zero_deltas),
    ] {
        bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }
    bytes
}

fn initial() -> VerifiedSnapshot {
    VerifiedSnapshot {
        identity: ProcessIdentity {
            pid: PID,
            start_ticks: START,
        },
        counters: Counters::default(),
    }
}

fn forwarded() -> VerifiedSnapshot {
    VerifiedSnapshot {
        identity: initial().identity,
        counters: Counters {
            invocations: 3,
            successful_forwards: 3,
            positive_deltas: 1,
            negative_deltas: 1,
            zero_deltas: 1,
        },
    }
}

struct Reader {
    results: VecDeque<Result<VerifiedSnapshot, SnapshotError>>,
    deadlines: Vec<u64>,
}

impl Reader {
    fn new(results: impl IntoIterator<Item = Result<VerifiedSnapshot, SnapshotError>>) -> Self {
        Self {
            results: results.into_iter().collect(),
            deadlines: Vec::new(),
        }
    }
}

impl SnapshotReader for Reader {
    fn read_verified(
        &mut self,
        expected: ExpectedIdentity,
        deadline_ms: u64,
    ) -> Result<VerifiedSnapshot, SnapshotError> {
        self.deadlines.push(deadline_ms);
        let value = self.results.pop_front().ok_or(SnapshotError::Source)??;
        if value.identity.pid != expected.pid {
            return Err(SnapshotError::WrongPid);
        }
        if expected
            .start_ticks
            .is_some_and(|start_ticks| value.identity.start_ticks != start_ticks)
        {
            return Err(SnapshotError::WrongStartToken);
        }
        Ok(value)
    }
}

fn armed(
    reader_results: impl IntoIterator<Item = Result<VerifiedSnapshot, SnapshotError>>,
) -> (RelayModel, Reader) {
    let mut model = RelayModel::new(0, Limits::default());
    model.observe_process(PID, 10).unwrap();
    let mut reader = Reader::new(reader_results);
    model.arm(report().as_bytes(), &mut reader, 20).unwrap();
    (model, reader)
}

#[test]
fn constructor_report_matches_actual_sufficient_fields() {
    assert_eq!(
        validate_constructor_report(report().as_bytes())
            .unwrap()
            .pid,
        PID
    );
}

#[test]
fn constructor_missing_and_duplicate_evidence_fail_closed() {
    let missing = report().replace("forwarding_ready=true\n", "");
    assert_eq!(
        validate_constructor_report(missing.as_bytes()),
        Err(ConstructorError::Missing("forwarding_ready"))
    );
    let duplicate = format!("{}selected_hook=DATA_HOOK\n", report());
    assert_eq!(
        validate_constructor_report(duplicate.as_bytes()),
        Err(ConstructorError::Duplicate)
    );
}

#[test]
fn wrong_firmware_hook_installation_and_snapshot_readiness_fail() {
    for (key, value) in [
        ("firmware_compatible", "false"),
        ("fingerprint_words_checked", "62"),
        ("selected_hook", "Q_LINK_HOOK"),
        ("required_site_prepared", "false"),
        ("installation_succeeded", "false"),
        ("forwarding_ready", "false"),
        ("counter_snapshot_ready", "false"),
        ("final_status", "PreparationFailed"),
    ] {
        let invalid = replace_field(&report(), key, value);
        assert!(matches!(
            validate_constructor_report(invalid.as_bytes()),
            Err(ConstructorError::Mismatch(field)) if field == key
        ));
    }
}

#[test]
fn malformed_oversized_and_invalid_pid_reports_fail() {
    assert_eq!(
        validate_constructor_report(b"not-a-field\n"),
        Err(ConstructorError::Malformed)
    );
    assert_eq!(
        validate_constructor_report(&vec![b'x'; 769]),
        Err(ConstructorError::TooLarge)
    );
    assert!(matches!(
        validate_constructor_report(
            replace_field(&report(), "counter_snapshot_pid", "0").as_bytes()
        ),
        Err(ConstructorError::Mismatch("counter_snapshot_pid"))
    ));
}

#[test]
fn snapshot_accepts_exact_stable_v2_contract() {
    let bytes = snapshot_bytes(PID, START, forwarded().counters);
    let result = validate_snapshot_reads(
        &[&bytes, &bytes],
        ProcessObservation {
            pid: PID,
            start_ticks: START,
            executable: "/usr/bin/MPC Sample",
        },
    )
    .unwrap();
    assert_eq!(result, forwarded());
}

#[test]
fn snapshot_rejects_wrong_magic_version_and_lengths() {
    let valid = snapshot_bytes(PID, START, Counters::default());
    let mut magic = valid;
    magic[0] = b'X';
    let mut version = valid;
    version[8..12].copy_from_slice(&1_u32.to_le_bytes());
    let process = ProcessObservation {
        pid: PID,
        start_ticks: START,
        executable: "/usr/bin/MPC Sample",
    };
    assert_eq!(
        validate_snapshot_reads(&[&magic, &magic], process),
        Err(SnapshotError::WrongMagic)
    );
    assert_eq!(
        validate_snapshot_reads(&[&version, &version], process),
        Err(SnapshotError::WrongVersion)
    );
    assert_eq!(
        validate_snapshot_reads(&[&valid[..71], &valid[..71]], process),
        Err(SnapshotError::WrongLength)
    );
    let oversized = [0_u8; 73];
    assert_eq!(
        validate_snapshot_reads(&[&oversized, &oversized], process),
        Err(SnapshotError::WrongLength)
    );
}

#[test]
fn snapshot_rejects_counter_inconsistency_and_unstable_reads() {
    let inconsistent = snapshot_bytes(
        PID,
        START,
        Counters {
            invocations: 1,
            successful_forwards: 2,
            positive_deltas: 1,
            ..Counters::default()
        },
    );
    let process = ProcessObservation {
        pid: PID,
        start_ticks: START,
        executable: "/usr/bin/MPC Sample",
    };
    assert_eq!(
        validate_snapshot_reads(&[&inconsistent, &inconsistent], process),
        Err(SnapshotError::Malformed)
    );
    let one = snapshot_bytes(PID, START, Counters::default());
    let two = snapshot_bytes(PID, START, forwarded().counters);
    assert_eq!(
        validate_snapshot_reads(&[&one, &two], process),
        Err(SnapshotError::Unstable)
    );
}

#[test]
fn snapshot_rejects_wrong_executable_pid_and_start_token() {
    let bytes = snapshot_bytes(PID, START, Counters::default());
    for (process, expected) in [
        (
            ProcessObservation {
                pid: PID,
                start_ticks: START,
                executable: "/bin/not-stock",
            },
            SnapshotError::WrongExecutable,
        ),
        (
            ProcessObservation {
                pid: PID + 1,
                start_ticks: START,
                executable: "/usr/bin/MPC Sample",
            },
            SnapshotError::WrongPid,
        ),
        (
            ProcessObservation {
                pid: PID,
                start_ticks: START + 1,
                executable: "/usr/bin/MPC Sample",
            },
            SnapshotError::WrongStartToken,
        ),
    ] {
        assert_eq!(
            validate_snapshot_reads(&[&bytes, &bytes], process),
            Err(expected)
        );
    }
}

#[test]
fn valid_arming_requires_report_process_and_initial_snapshot() {
    let (model, _) = armed([Ok(initial())]);
    assert_eq!(model.state(), State::Armed);
}

#[test]
fn arming_rejects_missing_process_wrong_pid_and_reader_failure() {
    let mut model = RelayModel::new(0, Limits::default());
    let mut reader = Reader::new([Ok(initial())]);
    assert_eq!(
        model.arm(report().as_bytes(), &mut reader, 1),
        Err(RelayError::WrongProcess)
    );
    assert_eq!(model.state(), State::Failed);

    let mut model = RelayModel::new(0, Limits::default());
    model.observe_process(PID + 1, 1).unwrap();
    assert_eq!(
        model.arm(report().as_bytes(), &mut reader, 2),
        Err(RelayError::WrongProcess)
    );
    assert_eq!(model.state(), State::Failed);

    let mut model = RelayModel::new(0, Limits::default());
    model.observe_process(PID, 1).unwrap();
    let mut failed = Reader::new([Err(SnapshotError::WrongExecutable)]);
    assert_eq!(
        model.arm(report().as_bytes(), &mut failed, 2),
        Err(RelayError::Snapshot(SnapshotError::WrongExecutable))
    );
    assert_eq!(model.state(), State::Failed);
}

#[test]
fn ambiguous_process_discovery_fails_closed() {
    let mut model = RelayModel::new(0, Limits::default());
    model.observe_process(PID, 1).unwrap();
    model.observe_process(PID, 2).unwrap();
    assert_eq!(
        model.observe_process(PID + 1, 3),
        Err(RelayError::WrongProcess)
    );
    assert_eq!(model.state(), State::Failed);
}

#[test]
fn process_arming_and_overall_timeouts_expire() {
    let limits = Limits {
        process_wait_ms: 10,
        arming_ms: 5,
        overall_ms: 20,
        ..Limits::default()
    };
    let mut waiting = RelayModel::new(0, limits);
    assert_eq!(waiting.tick(11), Err(RelayError::Timeout));
    assert_eq!(waiting.state(), State::Expired);

    let mut arming = RelayModel::new(0, limits);
    arming.observe_process(PID, 2).unwrap();
    let mut reader = Reader::new([Ok(initial())]);
    assert_eq!(
        arming.arm(report().as_bytes(), &mut reader, 8),
        Err(RelayError::Timeout)
    );

    let (mut armed, mut reader) = armed([Ok(initial())]);
    assert_eq!(armed.tick(120_001), Err(RelayError::Timeout));
    assert_eq!(
        armed.handle(Operation::Status, &mut reader, 120_002),
        Err(RelayError::Terminal)
    );
}

#[test]
fn status_is_bounded_and_discloses_no_process_identity() {
    let (mut model, mut reader) = armed([Ok(initial())]);
    assert_eq!(
        model.handle(Operation::Status, &mut reader, 30).unwrap(),
        Response::Status("state=ARMED\n".to_owned())
    );
}

#[test]
fn snapshot_before_arming_is_rejected_without_consuming_reader() {
    let mut model = RelayModel::new(0, Limits::default());
    let mut reader = Reader::new([Ok(forwarded())]);
    assert_eq!(
        model.handle(Operation::Snapshot, &mut reader, 1),
        Err(RelayError::NotArmed)
    );
    assert_eq!(model.state(), State::Waiting);
}

#[test]
fn successful_retrieval_is_one_shot_and_terminal() {
    let (mut model, mut reader) = armed([Ok(initial()), Ok(forwarded())]);
    let Response::Snapshot(body) = model.handle(Operation::Snapshot, &mut reader, 30).unwrap()
    else {
        panic!("snapshot response expected");
    };
    assert!(body.contains("callback_invocations=3\n"));
    assert!(body.contains("successful_forwards=3\n"));
    assert!(body.len() <= SNAPSHOT_REPORT_LIMIT);
    assert_eq!(model.state(), State::Reported);
    assert_eq!(
        model.handle(Operation::Snapshot, &mut reader, 31),
        Err(RelayError::Terminal)
    );
    assert_eq!(
        model.handle(Operation::Status, &mut reader, 32),
        Err(RelayError::Terminal)
    );
}

#[test]
fn post_interaction_identity_and_validation_failures_are_terminal() {
    for error in [
        SnapshotError::WrongPid,
        SnapshotError::WrongStartToken,
        SnapshotError::WrongExecutable,
        SnapshotError::Malformed,
        SnapshotError::Unstable,
        SnapshotError::Deadline,
    ] {
        let (mut model, mut reader) = armed([Ok(initial()), Err(error)]);
        assert_eq!(
            model.handle(Operation::Snapshot, &mut reader, 30),
            Err(RelayError::Snapshot(error))
        );
        assert_eq!(model.state(), State::Failed);
    }
}

#[test]
fn zero_callbacks_or_forwards_are_b1_failures() {
    let zero_callbacks = initial();
    let zero_forwards = VerifiedSnapshot {
        identity: initial().identity,
        counters: Counters {
            invocations: 1,
            positive_deltas: 1,
            ..Counters::default()
        },
    };
    for (snapshot, expected) in [
        (zero_callbacks, SnapshotError::NoCallbacks),
        (zero_forwards, SnapshotError::NoSuccessfulForwards),
    ] {
        let (mut model, mut reader) = armed([Ok(initial()), Ok(snapshot)]);
        assert_eq!(
            model.handle(Operation::Snapshot, &mut reader, 30),
            Err(RelayError::Snapshot(expected))
        );
        assert_eq!(model.state(), State::Failed);
    }
}

#[test]
fn changed_identity_returned_by_adapter_fails_closed() {
    let changed = VerifiedSnapshot {
        identity: ProcessIdentity {
            pid: PID,
            start_ticks: START + 1,
        },
        counters: forwarded().counters,
    };
    let (mut model, mut reader) = armed([Ok(initial()), Ok(changed)]);
    assert_eq!(
        model.handle(Operation::Snapshot, &mut reader, 30),
        Err(RelayError::Snapshot(SnapshotError::WrongStartToken))
    );
    assert_eq!(model.state(), State::Failed);
}

#[test]
fn request_count_is_bounded() {
    let limits = Limits {
        max_requests: 2,
        ..Limits::default()
    };
    let mut model = RelayModel::new(0, limits);
    model.observe_process(PID, 1).unwrap();
    let mut reader = Reader::new([Ok(initial())]);
    model.arm(report().as_bytes(), &mut reader, 2).unwrap();
    assert!(model.handle(Operation::Status, &mut reader, 3).is_ok());
    assert!(model.handle(Operation::Status, &mut reader, 4).is_ok());
    assert_eq!(
        model.handle(Operation::Status, &mut reader, 5),
        Err(RelayError::RequestLimit)
    );
    assert_eq!(model.state(), State::Failed);
}

#[test]
fn valid_protocol_requests_are_narrow() {
    assert_eq!(
        parse_request(
            b"GET /v1/status HTTP/1.1\r\nHost: 192.168.155.1\r\n\r\n",
            1,
            2_000
        ),
        Ok(Operation::Status)
    );
    assert_eq!(
        parse_request(
            b"POST /v1/snapshot HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
            1,
            2_000
        ),
        Ok(Operation::Snapshot)
    );
}

#[test]
fn protocol_rejects_methods_paths_and_traversal() {
    for (request, error) in [
        (
            "PUT /v1/status HTTP/1.1\r\n\r\n",
            ProtocolError::UnsupportedMethod,
        ),
        (
            "GET /v1/unknown HTTP/1.1\r\n\r\n",
            ProtocolError::UnknownPath,
        ),
        (
            "GET /v1/../run HTTP/1.1\r\n\r\n",
            ProtocolError::UnknownPath,
        ),
        (
            "POST /software-update HTTP/1.1\r\n\r\n",
            ProtocolError::UnknownPath,
        ),
    ] {
        assert_eq!(parse_request(request.as_bytes(), 0, 1), Err(error));
    }
}

#[test]
fn protocol_rejects_bodies_conflicting_lengths_and_chunking() {
    assert_eq!(
        parse_request(
            b"POST /v1/snapshot HTTP/1.1\r\nContent-Length: 1\r\n\r\nx",
            0,
            1
        ),
        Err(ProtocolError::BodyForbidden)
    );
    assert_eq!(
        parse_request(
            b"POST /v1/snapshot HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
            0,
            1
        ),
        Err(ProtocolError::ConflictingLength)
    );
    assert_eq!(
        parse_request(
            b"POST /v1/snapshot HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
            0,
            1
        ),
        Err(ProtocolError::TransferEncoding)
    );
}

#[test]
fn protocol_rejects_pipelining_malformed_incomplete_and_timeout() {
    assert_eq!(
        parse_request(
            b"GET /v1/status HTTP/1.1\r\n\r\nGET /v1/status HTTP/1.1\r\n\r\n",
            0,
            1
        ),
        Err(ProtocolError::Pipelined)
    );
    assert_eq!(
        parse_request(b"GET  /v1/status HTTP/1.1\r\n\r\n", 0, 1),
        Err(ProtocolError::Malformed)
    );
    assert_eq!(
        parse_request(b"GET /v1/status HTTP/1.1\r\n", 0, 1),
        Err(ProtocolError::Incomplete)
    );
    assert_eq!(
        parse_request(b"GET /v1/status HTTP/1.1\r\n\r\n", 2, 1),
        Err(ProtocolError::Timeout)
    );
}

#[test]
fn protocol_enforces_request_header_and_version_bounds() {
    assert_eq!(
        parse_request(&vec![b'x'; 1_025], 0, 1),
        Err(ProtocolError::TooLarge)
    );
    let many = format!(
        "GET /v1/status HTTP/1.1\r\n{}\r\n",
        "Host: x\r\n".repeat(17)
    );
    assert_eq!(
        parse_request(many.as_bytes(), 0, 1),
        Err(ProtocolError::TooManyHeaders)
    );
    assert_eq!(
        parse_request(b"GET /v1/status HTTP/1.0\r\n\r\n", 0, 1),
        Err(ProtocolError::UnsupportedVersion)
    );
    assert_eq!(
        parse_request(b"GET /v1/status HTTP/1.1\r\nUser-Agent: x\r\n\r\n", 0, 1),
        Err(ProtocolError::UnexpectedHeader)
    );
}

#[test]
fn formatted_report_remains_bounded_at_integer_maxima() {
    let snapshot = VerifiedSnapshot {
        identity: ProcessIdentity {
            pid: u32::MAX,
            start_ticks: u64::MAX,
        },
        counters: Counters {
            invocations: u64::MAX,
            successful_forwards: u64::MAX,
            positive_deltas: u64::MAX,
            negative_deltas: 0,
            zero_deltas: 0,
        },
    };
    assert!(format_snapshot(snapshot).unwrap().len() <= SNAPSHOT_REPORT_LIMIT);
}
