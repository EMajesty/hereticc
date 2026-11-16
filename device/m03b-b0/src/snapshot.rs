//! Fixed-size shared counter snapshot and host-side reader.

use core::sync::atomic::{AtomicU64, Ordering};
use std::fmt::Write as _;
use std::fs::File;
use std::io::{self, Read};
use std::os::fd::{AsRawFd as _, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use crate::EventCounters;

const MAGIC: [u8; 8] = *b"HRTCB1P0";
const VERSION: u32 = 2;
const SNAPSHOT_BYTES: usize = 72;
const SNAPSHOT_BYTES_U32: u32 = 72;
const SNAPSHOT_BYTES_OFF_T: libc::off_t = 72;
const READ_ATTEMPTS: usize = 8;
const EXPECTED_EXECUTABLE: &str = "/usr/bin/MPC Sample";
const REPORT_BYTES: usize = 384;

/// Atomics shared by the callback and an optional external reader.
#[repr(C, align(8))]
pub(crate) struct CounterBlock {
    magic: [u8; 8],
    version: u32,
    size: u32,
    pid: u32,
    reserved: u32,
    process_start_ticks: u64,
    pub(crate) invocations: AtomicU64,
    pub(crate) successful_forwards: AtomicU64,
    pub(crate) positive_deltas: AtomicU64,
    pub(crate) negative_deltas: AtomicU64,
    pub(crate) zero_deltas: AtomicU64,
}

const _: () = assert!(core::mem::size_of::<CounterBlock>() == SNAPSHOT_BYTES);

impl CounterBlock {
    pub(crate) const fn local() -> Self {
        Self::new(0, 0)
    }

    const fn new(pid: u32, process_start_ticks: u64) -> Self {
        Self {
            magic: MAGIC,
            version: VERSION,
            size: SNAPSHOT_BYTES_U32,
            pid,
            reserved: 0,
            process_start_ticks,
            invocations: AtomicU64::new(0),
            successful_forwards: AtomicU64::new(0),
            positive_deltas: AtomicU64::new(0),
            negative_deltas: AtomicU64::new(0),
            zero_deltas: AtomicU64::new(0),
        }
    }

    pub(crate) fn snapshot(&self) -> EventCounters {
        EventCounters {
            invocations: self.invocations.load(Ordering::Relaxed),
            successful_forwards: self.successful_forwards.load(Ordering::Relaxed),
            positive_deltas: self.positive_deltas.load(Ordering::Relaxed),
            negative_deltas: self.negative_deltas.load(Ordering::Relaxed),
            zero_deltas: self.zero_deltas.load(Ordering::Relaxed),
        }
    }

    #[cfg(test)]
    pub(crate) fn reset(&self) {
        for counter in self.all() {
            counter.store(0, Ordering::Relaxed);
        }
    }

    #[cfg(test)]
    pub(crate) fn all(&self) -> [&AtomicU64; 5] {
        [
            &self.invocations,
            &self.successful_forwards,
            &self.positive_deltas,
            &self.negative_deltas,
            &self.zero_deltas,
        ]
    }
}

/// A process-bound shared snapshot mapping. It is intentionally never unmapped.
pub(crate) struct SharedMapping {
    pub(crate) counters: &'static CounterBlock,
    #[cfg_attr(not(test), allow(dead_code))]
    pub(crate) path: PathBuf,
}

/// Create one exclusive, process-specific mapping under `/run`.
#[cfg_attr(test, allow(dead_code))]
pub(crate) fn create_shared(pid: u32) -> Result<SharedMapping, SnapshotError> {
    let stat = std::fs::read_to_string("/proc/self/stat").map_err(|_| SnapshotError::Access)?;
    let process_start_ticks = parse_process_start(&stat, pid)?;
    create_shared_in(Path::new("/run"), pid, process_start_ticks)
}

fn create_shared_in(
    directory: &Path,
    pid: u32,
    process_start_ticks: u64,
) -> Result<SharedMapping, SnapshotError> {
    let path = directory.join(format!("hereticc-m03b-b1p0-counters-{pid}.bin"));
    let path_c = std::ffi::CString::new(path.as_os_str().as_bytes())
        .map_err(|_| SnapshotError::InvalidPath)?;
    // SAFETY: the path is NUL terminated; exclusive/no-follow flags prevent replacement.
    let raw = unsafe {
        libc::open(
            path_c.as_ptr(),
            libc::O_RDWR | libc::O_CREAT | libc::O_EXCL | libc::O_CLOEXEC | libc::O_NOFOLLOW,
            0o600,
        )
    };
    if raw < 0 {
        return Err(SnapshotError::Access);
    }
    // SAFETY: successful open returned one owned descriptor.
    let fd = unsafe { OwnedFd::from_raw_fd(raw) };
    // SAFETY: valid descriptor and fixed representable size.
    if unsafe { libc::ftruncate(fd.as_raw_fd(), SNAPSHOT_BYTES_OFF_T) } != 0 {
        return Err(SnapshotError::Access);
    }
    // SAFETY: maps exactly the file length as shared read/write memory.
    let mapped = unsafe {
        libc::mmap(
            core::ptr::null_mut(),
            SNAPSHOT_BYTES,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_SHARED,
            fd.as_raw_fd(),
            0,
        )
    };
    if mapped == libc::MAP_FAILED {
        return Err(SnapshotError::Access);
    }
    let block = mapped.cast::<CounterBlock>();
    // SAFETY: fresh exclusive file/mapping, correctly aligned and exactly sized.
    unsafe { block.write(CounterBlock::new(pid, process_start_ticks)) };
    // Publish the immutable header before an external reader can accept it.
    // SAFETY: the mapped range is valid; synchronous flush occurs only in the constructor.
    if unsafe { libc::msync(mapped, SNAPSHOT_BYTES, libc::MS_SYNC) } != 0 {
        // SAFETY: no callback can know this mapping before this function returns.
        unsafe { libc::munmap(mapped, SNAPSHOT_BYTES) };
        return Err(SnapshotError::Access);
    }
    // SAFETY: mapping remains for process lifetime and is never unmapped after publication.
    let counters = unsafe { &*block };
    Ok(SharedMapping { counters, path })
}

/// One externally read and identity-verified counter snapshot.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VerifiedSnapshot {
    /// PID recorded by the expected stock process.
    pub pid: u32,
    /// Linux process start time in clock ticks since boot.
    pub process_start_ticks: u64,
    /// Aggregate counters.
    pub counters: EventCounters,
}

/// Snapshot reader failure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SnapshotError {
    /// Path could not be represented safely.
    InvalidPath,
    /// File or process metadata was unavailable or denied.
    Access,
    /// Header, size, version, or counter invariants were invalid.
    Malformed,
    /// Recorded process disappeared before identity verification.
    ProcessGone,
    /// Recorded process was not exact `/usr/bin/MPC Sample`.
    WrongProcess,
    /// Counters did not stabilize within the bounded read attempts.
    Unstable,
    /// Formatted report exceeded its fixed bound.
    ReportTooLarge,
}

/// Read a fixed snapshot and verify its live process identity.
///
/// # Errors
///
/// Returns a structured error for inaccessible, malformed, unstable, vanished,
/// or unexpected-process data.
pub fn read_verified(path: &Path, proc_root: &Path) -> Result<VerifiedSnapshot, SnapshotError> {
    let path_c = std::ffi::CString::new(path.as_os_str().as_bytes())
        .map_err(|_| SnapshotError::InvalidPath)?;
    // SAFETY: path is NUL terminated; no-follow prevents redirected reads.
    let raw = unsafe {
        libc::open(
            path_c.as_ptr(),
            libc::O_RDONLY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    };
    if raw < 0 {
        return Err(SnapshotError::Access);
    }
    // SAFETY: successful open returned one owned descriptor.
    let mut file = unsafe { File::from_raw_fd(raw) };
    if file.metadata().map_err(|_| SnapshotError::Access)?.len() != SNAPSHOT_BYTES as u64 {
        return Err(SnapshotError::Malformed);
    }
    let mut first = [0_u8; SNAPSHOT_BYTES];
    let mut second = [0_u8; SNAPSHOT_BYTES];
    for _ in 0..READ_ATTEMPTS {
        read_exact_at_start(&mut file, &mut first)?;
        read_exact_at_start(&mut file, &mut second)?;
        if first == second {
            let snapshot = decode(&first)?;
            verify_process(proc_root, snapshot.pid, snapshot.process_start_ticks)?;
            return Ok(snapshot);
        }
    }
    Err(SnapshotError::Unstable)
}

fn read_exact_at_start(file: &mut File, buffer: &mut [u8]) -> Result<(), SnapshotError> {
    use std::io::Seek as _;
    file.rewind().map_err(|_| SnapshotError::Access)?;
    file.read_exact(buffer).map_err(|error| {
        if error.kind() == io::ErrorKind::UnexpectedEof {
            SnapshotError::Malformed
        } else {
            SnapshotError::Access
        }
    })
}

fn decode(bytes: &[u8; SNAPSHOT_BYTES]) -> Result<VerifiedSnapshot, SnapshotError> {
    if bytes[..8] != MAGIC
        || u32_at(bytes, 8) != VERSION
        || u32_at(bytes, 12) as usize != SNAPSHOT_BYTES
        || u32_at(bytes, 20) != 0
    {
        return Err(SnapshotError::Malformed);
    }
    let counters = EventCounters {
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
        pid: u32_at(bytes, 16),
        process_start_ticks: u64_at(bytes, 24),
        counters,
    })
}

fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("fixed range"))
}

fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().expect("fixed range"))
}

fn verify_process(
    proc_root: &Path,
    pid: u32,
    expected_start_ticks: u64,
) -> Result<(), SnapshotError> {
    let executable = proc_root.join(pid.to_string()).join("exe");
    match std::fs::read_link(executable) {
        Ok(path) if path == Path::new(EXPECTED_EXECUTABLE) => Ok(()),
        Ok(_) => Err(SnapshotError::WrongProcess),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Err(SnapshotError::ProcessGone),
        Err(_) => Err(SnapshotError::Access),
    }?;
    let stat =
        std::fs::read_to_string(proc_root.join(pid.to_string()).join("stat")).map_err(|error| {
            if error.kind() == io::ErrorKind::NotFound {
                SnapshotError::ProcessGone
            } else {
                SnapshotError::Access
            }
        })?;
    if parse_process_start(&stat, pid)? == expected_start_ticks {
        Ok(())
    } else {
        Err(SnapshotError::WrongProcess)
    }
}

fn parse_process_start(stat: &str, expected_pid: u32) -> Result<u64, SnapshotError> {
    let (pid, rest) = stat.split_once(" (").ok_or(SnapshotError::Malformed)?;
    if pid.parse::<u32>().map_err(|_| SnapshotError::Malformed)? != expected_pid {
        return Err(SnapshotError::Malformed);
    }
    let close = rest.rfind(") ").ok_or(SnapshotError::Malformed)?;
    rest[close + 2..]
        .split_ascii_whitespace()
        .nth(19)
        .ok_or(SnapshotError::Malformed)?
        .parse::<u64>()
        .map_err(|_| SnapshotError::Malformed)
}

/// Format a bounded, address-free post-interaction report.
///
/// # Errors
///
/// Returns [`SnapshotError::ReportTooLarge`] if fixed fields exceed the bound.
pub fn format_snapshot(snapshot: VerifiedSnapshot) -> Result<String, SnapshotError> {
    let mut report = String::with_capacity(256);
    writeln!(report, "hereticc_experiment=m03b-b1p0").expect("String write");
    writeln!(report, "snapshot_version={VERSION}").expect("String write");
    writeln!(report, "process_identity=expected").expect("String write");
    writeln!(report, "process_id={}", snapshot.pid).expect("String write");
    writeln!(
        report,
        "process_start_ticks={}",
        snapshot.process_start_ticks
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
    if report.len() > REPORT_BYTES {
        return Err(SnapshotError::ReportTooLarge);
    }
    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::unix::fs::symlink;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT: AtomicU64 = AtomicU64::new(0);

    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            let id = NEXT.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("neurogenic-b02-{}-{id}", std::process::id()));
            fs::create_dir(&path).unwrap();
            Self(path)
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn proc_identity(root: &Path, pid: u32, target: &str, start_ticks: u64) {
        let directory = root.join(pid.to_string());
        fs::create_dir_all(&directory).unwrap();
        symlink(target, directory.join("exe")).unwrap();
        let mut fields = vec!["0".to_owned(); 20];
        fields[0] = "S".to_owned();
        fields[19] = start_ticks.to_string();
        fs::write(
            directory.join("stat"),
            format!("{pid} (MPC Sample helper name) {}\n", fields.join(" ")),
        )
        .unwrap();
    }

    #[test]
    fn shared_mapping_round_trips_after_concurrent_updates() {
        let fixture = Fixture::new();
        let mapping = create_shared_in(&fixture.0, 42, 9001).unwrap();
        std::thread::scope(|scope| {
            for _ in 0..4 {
                scope.spawn(|| {
                    for _ in 0..250 {
                        mapping.counters.invocations.fetch_add(1, Ordering::Relaxed);
                        mapping
                            .counters
                            .positive_deltas
                            .fetch_add(1, Ordering::Relaxed);
                    }
                });
            }
        });
        proc_identity(&fixture.0, 42, EXPECTED_EXECUTABLE, 9001);
        let snapshot = read_verified(&mapping.path, &fixture.0).unwrap();
        assert_eq!(snapshot.counters.invocations, 1_000);
        assert_eq!(snapshot.counters.positive_deltas, 1_000);
    }

    #[test]
    fn rejects_wrong_missing_and_malformed_process_data() {
        let fixture = Fixture::new();
        let mapping = create_shared_in(&fixture.0, 43, 1234).unwrap();
        assert_eq!(
            read_verified(&mapping.path, &fixture.0),
            Err(SnapshotError::ProcessGone)
        );
        proc_identity(&fixture.0, 43, "/bin/not-stock", 1234);
        assert_eq!(
            read_verified(&mapping.path, &fixture.0),
            Err(SnapshotError::WrongProcess)
        );

        let short = fixture.0.join("short");
        fs::write(&short, b"bad").unwrap();
        assert_eq!(
            read_verified(&short, &fixture.0),
            Err(SnapshotError::Malformed)
        );

        mapping
            .counters
            .successful_forwards
            .store(1, Ordering::Relaxed);
        assert_eq!(
            read_verified(&mapping.path, &fixture.0),
            Err(SnapshotError::Malformed)
        );
    }

    #[test]
    fn inaccessible_snapshot_is_explicit() {
        let fixture = Fixture::new();
        assert_eq!(
            read_verified(&fixture.0.join("missing"), &fixture.0),
            Err(SnapshotError::Access)
        );
        symlink("missing-target", fixture.0.join("linked")).unwrap();
        assert_eq!(
            read_verified(&fixture.0.join("linked"), &fixture.0),
            Err(SnapshotError::Access)
        );

        let mapping = create_shared_in(&fixture.0, 44, 99).unwrap();
        let process = fixture.0.join("44");
        fs::create_dir(&process).unwrap();
        fs::write(process.join("exe"), b"not-a-link").unwrap();
        assert_eq!(
            read_verified(&mapping.path, &fixture.0),
            Err(SnapshotError::Access)
        );
    }

    #[test]
    fn report_is_bounded_and_contains_no_addresses_or_payloads() {
        let snapshot = VerifiedSnapshot {
            pid: u32::MAX,
            process_start_ticks: u64::MAX,
            counters: EventCounters {
                invocations: u64::MAX,
                successful_forwards: u64::MAX,
                positive_deltas: u64::MAX,
                negative_deltas: 0,
                zero_deltas: 0,
            },
        };
        let report = format_snapshot(snapshot).unwrap();
        assert!(report.len() <= REPORT_BYTES);
        assert!(!report.contains("0x"));
        assert!(!report.contains("delta="));
    }

    #[test]
    fn process_start_parser_handles_names_spaces_and_rejects_stale_or_malformed_data() {
        assert_eq!(
            parse_process_start(
                "7 (name with ) spaces) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 456\n",
                7
            ),
            Ok(456)
        );
        assert_eq!(
            parse_process_start("8 (name) S", 8),
            Err(SnapshotError::Malformed)
        );

        let fixture = Fixture::new();
        let mapping = create_shared_in(&fixture.0, 45, 111).unwrap();
        proc_identity(&fixture.0, 45, EXPECTED_EXECUTABLE, 222);
        assert_eq!(
            read_verified(&mapping.path, &fixture.0),
            Err(SnapshotError::WrongProcess)
        );
    }
}
