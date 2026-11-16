use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use neurogenic_b1_collector_model::{
    CollectionError, CollectionStatus, MAX_REPORT_BYTES, MonotonicClock, MountEvidence,
    ObservationInputs, RESULT_NAME, ValidatedDestination, collect, format_failure_report,
    format_report, publish_once, write_report,
};

#[derive(Clone, Debug)]
struct Clock {
    values: Vec<u64>,
    index: usize,
}

impl Clock {
    fn fixed(value: u64) -> Self {
        Self {
            values: vec![value],
            index: 0,
        }
    }
}

impl MonotonicClock for Clock {
    fn now_ms(&mut self) -> u64 {
        let value = self.values[self.index.min(self.values.len() - 1)];
        self.index = self.index.saturating_add(1);
        value
    }
}

fn stat(pid: u32, start: u64) -> Vec<u8> {
    let mut fields = vec!["0".to_owned(); 20];
    fields[0] = "S".into();
    fields[19] = start.to_string();
    format!("{pid} (MPC Sample) {}\n", fields.join(" ")).into_bytes()
}

fn complete_inputs() -> ObservationInputs {
    ObservationInputs {
        unavailable_sources: BTreeSet::default(),
        service: b"SystemdVersion=252\nType=notify\nKillMode=control-group\nRestart=always\nMainPID=4242\nControlGroup=/system.slice/ac50.service\n".to_vec(),
        executable: b"/usr/bin/MPC Sample\n".to_vec(),
        stat_before: stat(4242, 987_654),
        status: b"Uid:\t1000 1000 1000 1000\nGid:\t1000 1000 1000 1000\n".to_vec(),
        cgroup: b"0::/system.slice/ac50.service\n".to_vec(),
        stat_after: stat(4242, 987_654),
        run_mount: Some(
            b"/run source tmpfs rw,nosuid\nRUN_MODE=755\nRUNTIME_UNIT_DIR_MODE=755\n"
                .to_vec(),
        ),
        network: Some(
            b"usb0 192.168.155.1/28\n192.168.155.0/28 dev usb0\nLINK usb0 UP\n"
                .to_vec(),
        ),
        sockets: Some(b"LISTEN 0 10 192.168.155.1:80\nLISTEN 0 10 0.0.0.0:22\n".to_vec()),
    }
}

fn temp_dir(label: &str) -> PathBuf {
    let unique = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("clock after epoch")
        .as_nanos();
    let path = std::env::temp_dir().join(format!(
        "neurogenic-p3-2d-{label}-{}-{unique}",
        std::process::id()
    ));
    fs::create_dir(&path).expect("create temporary directory");
    path
}

fn mount(path: &std::path::Path) -> MountEvidence<'_> {
    MountEvidence {
        source: "/dev/mmcblk1p1",
        filesystem: "exfat",
        read_write: true,
        mount_path: path,
        exclusive_create_supported: true,
    }
}

#[test]
fn complete_collection_is_bounded_deterministic_and_selected() {
    let inputs = complete_inputs();
    let first = collect(&inputs, &mut Clock::fixed(0), 100).expect("collect");
    let second = collect(&inputs, &mut Clock::fixed(0), 100).expect("collect");
    assert_eq!(first, second);
    assert_eq!(first.status, CollectionStatus::Complete);
    assert!(first.report.len() <= MAX_REPORT_BYTES);
    let text = std::str::from_utf8(&first.report).expect("ASCII report");
    assert!(text.contains("collection_status=complete\n"));
    assert!(text.contains("process_executable=stock-exact\n"));
    assert!(text.contains("listener_exact_ports=80\n"));
    assert!(!text.contains("argv"));
}

#[test]
fn unavailable_optional_sources_produce_valid_partial_report() {
    let mut inputs = complete_inputs();
    inputs.run_mount = None;
    inputs.network = None;
    inputs.sockets = None;
    let outcome = collect(&inputs, &mut Clock::fixed(0), 100).expect("partial collect");
    assert_eq!(outcome.status, CollectionStatus::Partial);
    assert!(outcome.unavailable.contains("run"));
    assert!(outcome.unavailable.contains("network"));
    assert!(
        outcome
            .report
            .windows(25)
            .any(|value| value == b"collection_status=partial")
    );
}

#[test]
fn service_and_process_identity_fail_closed() {
    let cases = [
        (
            b"Type=notify\n".to_vec(),
            CollectionError::InvalidService("missing MainPID"),
        ),
        (
            b"MainPID=0\n".to_vec(),
            CollectionError::InvalidService("zero MainPID"),
        ),
    ];
    for (service, expected) in cases {
        let mut inputs = complete_inputs();
        inputs.service = service;
        assert_eq!(collect(&inputs, &mut Clock::fixed(0), 100), Err(expected));
    }
    let mut wrong = complete_inputs();
    wrong.executable = b"/usr/bin/other\n".to_vec();
    assert_eq!(
        collect(&wrong, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidProcess("wrong executable"))
    );
}

#[test]
fn pid_and_start_token_changes_fail_closed() {
    let mut pid = complete_inputs();
    pid.stat_after = stat(4243, 987_654);
    assert_eq!(
        collect(&pid, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidProcess("PID changed"))
    );
    let mut start = complete_inputs();
    start.stat_after = stat(4242, 987_655);
    assert_eq!(
        collect(&start, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidProcess("start token changed"))
    );
}

#[test]
fn malformed_denied_and_oversized_sources_are_bounded() {
    let mut malformed = complete_inputs();
    malformed.stat_before = b"broken\n".to_vec();
    assert_eq!(
        collect(&malformed, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidProcess("malformed initial stat"))
    );
    let mut denied = complete_inputs();
    denied.unavailable_sources.insert("process");
    assert_eq!(
        collect(&denied, &mut Clock::fixed(0), 100),
        Err(CollectionError::SourceUnavailable("process"))
    );
    let mut oversized = complete_inputs();
    oversized.network = Some(vec![b'x'; 16 * 1024 + 1]);
    assert_eq!(
        collect(&oversized, &mut Clock::fixed(0), 100),
        Err(CollectionError::SourceTooLarge("network"))
    );
    let mut invalid = complete_inputs();
    invalid.cgroup = vec![0xff];
    assert_eq!(
        collect(&invalid, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidUtf8("cgroup"))
    );
}

#[test]
fn network_prefix_route_and_absence_are_distinct() {
    let mut absent = complete_inputs();
    absent.network = Some(Vec::new());
    assert_eq!(
        collect(&absent, &mut Clock::fixed(0), 100)
            .expect("partial")
            .status,
        CollectionStatus::Partial
    );
    let mut prefix = complete_inputs();
    prefix.network = Some(b"usb0 192.168.155.1/24\n".to_vec());
    assert_eq!(
        collect(&prefix, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidNetwork("incorrect USB prefix"))
    );
    let mut route = complete_inputs();
    route.network =
        Some(b"usb0 192.168.155.1/28\n192.168.155.0/28 dev eth0\nLINK usb0 UP\n".to_vec());
    assert_eq!(
        collect(&route, &mut Clock::fixed(0), 100),
        Err(CollectionError::InvalidNetwork("USB route mismatch"))
    );
}

#[test]
fn network_observation_order_and_identical_duplicates_do_not_matter() {
    let lines = [
        "usb0 192.168.155.1/28",
        "192.168.155.0/28 dev usb0",
        "LINK usb0 UP",
    ];
    let permutations = [
        [0, 1, 2],
        [0, 2, 1],
        [1, 0, 2],
        [1, 2, 0],
        [2, 0, 1],
        [2, 1, 0],
    ];
    let expected = collect(&complete_inputs(), &mut Clock::fixed(0), 100)
        .expect("baseline")
        .report;
    for permutation in permutations {
        let mut inputs = complete_inputs();
        let network = permutation
            .iter()
            .map(|index| lines[*index])
            .chain([lines[0], lines[1], lines[2]])
            .collect::<Vec<_>>()
            .join("\n")
            + "\n";
        inputs.network = Some(network.into_bytes());
        assert_eq!(
            collect(&inputs, &mut Clock::fixed(0), 100)
                .expect("permutation")
                .report,
            expected
        );
    }
}

#[test]
fn network_contradictions_and_malformed_lines_fail_closed() {
    for (network, expected) in [
        (
            "usb0 192.168.155.1/28\neth0 192.168.155.1/28\n192.168.155.0/28 dev usb0\nLINK usb0 UP\n",
            "contradictory USB interfaces",
        ),
        (
            "usb0 192.168.155.1/28\n192.168.155.0/28 dev usb0\nLINK usb0 UP\nLINK usb0 DOWN\n",
            "contradictory link states",
        ),
        (
            "usb0 192.168.155.1/28\n192.168.155.0/28 dev usb0\nLINK usb0 MAYBE\n",
            "malformed link state",
        ),
    ] {
        let mut inputs = complete_inputs();
        inputs.network = Some(network.as_bytes().to_vec());
        assert_eq!(
            collect(&inputs, &mut Clock::fixed(0), 100),
            Err(CollectionError::InvalidNetwork(expected))
        );
    }
}

#[test]
fn deadline_is_checked_at_start_and_each_stage() {
    assert_eq!(
        collect(&complete_inputs(), &mut Clock::fixed(100), 100),
        Err(CollectionError::DeadlineExpired)
    );
    let mut late = Clock {
        values: vec![0, 1, 2, 3, 100],
        index: 0,
    };
    assert_eq!(
        collect(&complete_inputs(), &mut late, 100),
        Err(CollectionError::DeadlineExpired)
    );
}

#[test]
fn report_formatter_rejects_value_and_total_bounds() {
    let mut value = BTreeMap::new();
    value.insert("bad", "x".repeat(129));
    assert_eq!(format_report(&value), Err(CollectionError::ReportBounds));
    let mut fields = BTreeMap::new();
    for index in 0..33 {
        let key: &'static str = Box::leak(format!("field_{index}").into_boxed_str());
        fields.insert(key, "x".into());
    }
    assert_eq!(format_report(&fields), Err(CollectionError::ReportBounds));
}

#[test]
fn failure_report_is_fixed_bounded_and_contains_no_error_detail() {
    let report = format_failure_report().expect("fixed failure report");
    let text = std::str::from_utf8(&report).expect("ASCII");
    assert!(text.contains("collection_status=failed\n"));
    assert!(!text.contains("error"));
    assert!(report.len() <= MAX_REPORT_BYTES);
}

#[test]
fn publication_is_no_clobber_and_uses_fixed_name() {
    let root = temp_dir("publish");
    let destination = ValidatedDestination::inspect_exact(&root, &mount(&root)).expect("validate");
    let path = publish_once(&destination, b"bounded\n").expect("publish");
    assert_eq!(
        path.file_name().and_then(|name| name.to_str()),
        Some(RESULT_NAME)
    );
    assert_eq!(fs::read(&path).expect("read report"), b"bounded\n");
    assert_eq!(
        publish_once(&destination, b"replacement\n")
            .expect_err("must not overwrite")
            .kind(),
        std::io::ErrorKind::AlreadyExists
    );
    assert_eq!(fs::read(&path).expect("original remains"), b"bounded\n");
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn publication_rejects_symlink_non_directory_and_oversize() {
    let root = temp_dir("invalid");
    let file = root.join("file");
    fs::write(&file, b"x").expect("write fixture");
    assert!(ValidatedDestination::inspect_exact(&file, &mount(&file)).is_err());
    #[cfg(unix)]
    {
        std::os::unix::fs::symlink(&root, root.join("link")).expect("symlink");
        let link = root.join("link");
        assert!(ValidatedDestination::inspect_exact(&link, &mount(&link)).is_err());
    }
    let destination = ValidatedDestination::inspect_exact(&root, &mount(&root)).expect("validate");
    assert_eq!(
        publish_once(&destination, &vec![b'x'; MAX_REPORT_BYTES + 1])
            .expect_err("oversize")
            .kind(),
        std::io::ErrorKind::InvalidInput
    );
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn publication_rejects_sd_mount_identity_mismatch() {
    let root = temp_dir("mount");
    let wrong_source = MountEvidence {
        source: "/dev/other",
        ..mount(&root)
    };
    let wrong_filesystem = MountEvidence {
        filesystem: "ext4",
        ..mount(&root)
    };
    let read_only = MountEvidence {
        read_write: false,
        ..mount(&root)
    };
    assert!(ValidatedDestination::inspect_exact(&root, &wrong_source).is_err());
    assert!(ValidatedDestination::inspect_exact(&root, &wrong_filesystem).is_err());
    assert!(ValidatedDestination::inspect_exact(&root, &read_only).is_err());
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn publication_rejects_changed_destination_and_unsupported_exclusive_create() {
    let root = temp_dir("changed");
    let destination = ValidatedDestination::inspect_exact(&root, &mount(&root)).expect("validate");
    let moved = root.with_extension("moved");
    fs::rename(&root, &moved).expect("move original");
    fs::create_dir(&root).expect("replacement directory");
    assert_eq!(
        publish_once(&destination, b"new\n")
            .expect_err("identity change must fail")
            .kind(),
        std::io::ErrorKind::InvalidInput
    );
    let unsupported = MountEvidence {
        exclusive_create_supported: false,
        ..mount(&root)
    };
    assert!(ValidatedDestination::inspect_exact(&root, &unsupported).is_err());
    fs::remove_dir_all(root).expect("cleanup replacement");
    fs::remove_dir_all(moved).expect("cleanup original");
}

#[test]
fn preexisting_reserved_partial_marker_blocks_publication() {
    let root = temp_dir("partial");
    fs::write(root.join(".b1-p3-2c-report.partial"), b"old").expect("partial fixture");
    let destination = ValidatedDestination::inspect_exact(&root, &mount(&root)).expect("validate");
    assert_eq!(
        publish_once(&destination, b"new\n")
            .expect_err("partial marker must block")
            .kind(),
        std::io::ErrorKind::AlreadyExists
    );
    assert!(!root.join(RESULT_NAME).exists());
    fs::remove_dir_all(root).expect("cleanup");
}

#[test]
fn bounded_writer_handles_short_writes_and_reports_full_or_zero_progress() {
    struct ShortWriter(Vec<u8>);
    impl std::io::Write for ShortWriter {
        fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
            let count = bytes.len().min(2);
            self.0.extend_from_slice(&bytes[..count]);
            Ok(count)
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    struct FailingWriter(std::io::ErrorKind);
    impl std::io::Write for FailingWriter {
        fn write(&mut self, _bytes: &[u8]) -> std::io::Result<usize> {
            if self.0 == std::io::ErrorKind::WriteZero {
                Ok(0)
            } else {
                Err(std::io::Error::new(self.0, "injected"))
            }
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let mut short = ShortWriter(Vec::new());
    write_report(&mut short, b"abcdef").expect("short writes complete");
    assert_eq!(short.0, b"abcdef");
    for kind in [
        std::io::ErrorKind::WriteZero,
        std::io::ErrorKind::StorageFull,
    ] {
        assert_eq!(
            write_report(&mut FailingWriter(kind), b"report")
                .expect_err("injected failure")
                .kind(),
            kind
        );
    }
}
