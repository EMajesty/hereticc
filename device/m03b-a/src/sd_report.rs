//! Bounded direct reporting to the verified removable SD data partition.

use std::ffi::{CStr, CString};
use std::fs::File;
use std::io::{self, Read, Write};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

const MOUNTS_PATH: &str = "/proc/mounts";
const MOUNTS_LIMIT: u64 = 64 * 1024;
const SD_SOURCE: &str = "/dev/mmcblk1p1";
const SD_FS: &str = "exfat";
const EXFAT_MAGIC: libc::c_long = 0x2011_bab0;
const RESULT_DIRECTORY: &CStr = c"hereticc-results";
const MAX_REPORT_BYTES: usize = 768;
const UNEXPECTED_SLOT_COUNT: usize = 4;
const EXPECTED_SLOT_COUNT: usize = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum ReportCategory {
    Expected,
    Unexpected,
}

impl ReportCategory {
    const fn label(self) -> &'static str {
        match self {
            Self::Expected => "expected",
            Self::Unexpected => "unexpected",
        }
    }

    const fn slots(self) -> usize {
        match self {
            Self::Expected => EXPECTED_SLOT_COUNT,
            Self::Unexpected => UNEXPECTED_SLOT_COUNT,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum MountError {
    Io,
    TooLarge,
    Missing,
    Ambiguous,
    Unexpected,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SdError {
    Mount(MountError),
    ReportTooLarge,
    Io,
    NoFreeSlot,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum FaultPoint {
    PartialWrite,
    FileSync,
    Finalize,
    DirectorySync,
}

pub(crate) fn write_to_detected_sd(report: &str, category: ReportCategory) -> Result<(), ()> {
    write_to_detected_sd_inner(report, category).map_err(|_| ())
}

fn write_to_detected_sd_inner(report: &str, category: ReportCategory) -> Result<(), SdError> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(SdError::ReportTooLarge);
    }
    let mounts = read_bounded(MOUNTS_PATH).map_err(SdError::Mount)?;
    let mount = select_mount(&mounts).map_err(SdError::Mount)?;
    publish(&mount, report.as_bytes(), category, None, true)
}

fn read_bounded(path: &str) -> Result<String, MountError> {
    let file = File::open(path).map_err(|_| MountError::Io)?;
    let mut bytes = Vec::with_capacity(4096);
    file.take(MOUNTS_LIMIT + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| MountError::Io)?;
    if bytes.len() as u64 > MOUNTS_LIMIT {
        return Err(MountError::TooLarge);
    }
    String::from_utf8(bytes).map_err(|_| MountError::Unexpected)
}

fn select_mount(mounts: &str) -> Result<PathBuf, MountError> {
    let mut source_records = 0usize;
    let mut candidate = None;
    for line in mounts.lines() {
        let fields: Vec<_> = line.split_ascii_whitespace().collect();
        if fields.first().copied() != Some(SD_SOURCE) {
            continue;
        }
        source_records = source_records.checked_add(1).ok_or(MountError::Ambiguous)?;
        if fields.len() < 4 {
            continue;
        }
        let mount = fields[1];
        let options = fields[3];
        if fields[2] == SD_FS
            && valid_mount_path(mount)
            && option_present(options, "rw")
            && !option_present(options, "ro")
        {
            match candidate {
                None => candidate = Some(PathBuf::from(mount)),
                Some(_) => return Err(MountError::Ambiguous),
            }
        }
    }
    match (source_records, candidate) {
        (0, _) => Err(MountError::Missing),
        (1, Some(path)) => Ok(path),
        (1, None) => Err(MountError::Unexpected),
        _ => Err(MountError::Ambiguous),
    }
}

fn valid_mount_path(path: &str) -> bool {
    let Some(name) = path.strip_prefix("/media/") else {
        return false;
    };
    !name.is_empty()
        && !name.contains('/')
        && name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

fn option_present(options: &str, wanted: &str) -> bool {
    options.split(',').any(|option| option == wanted)
}

fn publish(
    mount: &Path,
    report: &[u8],
    category: ReportCategory,
    fault: Option<FaultPoint>,
    verify_fs: bool,
) -> Result<(), SdError> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(SdError::ReportTooLarge);
    }
    let mount_fd = open_directory(mount).map_err(|_| SdError::Io)?;
    if verify_fs && filesystem_magic(&mount_fd).map_err(|_| SdError::Io)? != EXFAT_MAGIC {
        return Err(SdError::Mount(MountError::Unexpected));
    }
    make_results_directory(&mount_fd).map_err(|_| SdError::Io)?;
    let results_fd = open_directory_at(&mount_fd, RESULT_DIRECTORY).map_err(|_| SdError::Io)?;

    for slot in 0..category.slots() {
        let final_name = CString::new(format!("m03b-a4-{}-{slot:02}.txt", category.label()))
            .map_err(|_| SdError::Io)?;
        let partial_name = CString::new(format!(".m03b-a4-{}-{slot:02}.partial", category.label()))
            .map_err(|_| SdError::Io)?;
        if exists_at(&results_fd, &final_name).map_err(|_| SdError::Io)?
            || exists_at(&results_fd, &partial_name).map_err(|_| SdError::Io)?
        {
            continue;
        }
        let mut partial = match create_file_at(&results_fd, &partial_name) {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(_) => return Err(SdError::Io),
        };
        let write_result = if fault == Some(FaultPoint::PartialWrite) {
            let prefix = report.len().min(1);
            partial
                .write_all(&report[..prefix])
                .and_then(|()| Err(io::Error::other("injected partial write")))
        } else {
            partial.write_all(report)
        };
        if write_result.is_err()
            || fault == Some(FaultPoint::FileSync)
            || partial.sync_all().is_err()
        {
            drop(partial);
            unlink_at(&results_fd, &partial_name);
            return Err(SdError::Io);
        }
        drop(partial);
        if fault == Some(FaultPoint::Finalize)
            || rename_noreplace(&results_fd, &partial_name, &final_name).is_err()
        {
            unlink_at(&results_fd, &partial_name);
            return Err(SdError::Io);
        }
        if fault == Some(FaultPoint::DirectorySync) || sync_directory(&results_fd).is_err() {
            unlink_at(&results_fd, &final_name);
            let _ = sync_directory(&results_fd);
            return Err(SdError::Io);
        }
        return Ok(());
    }
    Err(SdError::NoFreeSlot)
}

fn path_cstring(path: &Path) -> io::Result<CString> {
    CString::new(path.as_os_str().as_bytes())
        .map_err(|_| io::Error::from(io::ErrorKind::InvalidInput))
}

fn open_directory(path: &Path) -> io::Result<OwnedFd> {
    let path = path_cstring(path)?;
    // SAFETY: `path` is NUL-terminated and flags request a read-only directory descriptor.
    let fd = unsafe {
        libc::open(
            path.as_ptr(),
            libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    };
    owned_fd(fd)
}

fn open_directory_at(parent: &OwnedFd, name: &CStr) -> io::Result<OwnedFd> {
    // SAFETY: both descriptors and the NUL-terminated relative name are valid for this call.
    let fd = unsafe {
        libc::openat(
            parent.as_raw_fd(),
            name.as_ptr(),
            libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    };
    owned_fd(fd)
}

fn owned_fd(fd: libc::c_int) -> io::Result<OwnedFd> {
    if fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        // SAFETY: a nonnegative result from `open`/`openat` is a newly owned descriptor.
        Ok(unsafe { OwnedFd::from_raw_fd(fd) })
    }
}

fn filesystem_magic(fd: &OwnedFd) -> io::Result<libc::c_long> {
    let mut info = std::mem::MaybeUninit::<libc::statfs>::uninit();
    // SAFETY: `info` points to writable storage and `fd` is open.
    if unsafe { libc::fstatfs(fd.as_raw_fd(), info.as_mut_ptr()) } != 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful `fstatfs` initialized the structure.
    Ok(unsafe { info.assume_init() }.f_type)
}

fn make_results_directory(parent: &OwnedFd) -> io::Result<()> {
    // SAFETY: descriptor and fixed NUL-terminated name are valid.
    if unsafe { libc::mkdirat(parent.as_raw_fd(), RESULT_DIRECTORY.as_ptr(), 0o700) } == 0 {
        return Ok(());
    }
    let error = io::Error::last_os_error();
    if error.kind() == io::ErrorKind::AlreadyExists {
        Ok(())
    } else {
        Err(error)
    }
}

fn exists_at(parent: &OwnedFd, name: &CStr) -> io::Result<bool> {
    let mut stat = std::mem::MaybeUninit::<libc::stat>::uninit();
    // SAFETY: pointers are valid; no result fields are read on failure.
    if unsafe {
        libc::fstatat(
            parent.as_raw_fd(),
            name.as_ptr(),
            stat.as_mut_ptr(),
            libc::AT_SYMLINK_NOFOLLOW,
        )
    } == 0
    {
        Ok(true)
    } else {
        let error = io::Error::last_os_error();
        if error.kind() == io::ErrorKind::NotFound {
            Ok(false)
        } else {
            Err(error)
        }
    }
}

fn create_file_at(parent: &OwnedFd, name: &CStr) -> io::Result<File> {
    // SAFETY: descriptor and relative name are valid; O_EXCL prevents replacement.
    let fd = unsafe {
        libc::openat(
            parent.as_raw_fd(),
            name.as_ptr(),
            libc::O_WRONLY | libc::O_CREAT | libc::O_EXCL | libc::O_CLOEXEC | libc::O_NOFOLLOW,
            0o600,
        )
    };
    if fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        // SAFETY: a successful `openat` returns one newly owned descriptor.
        Ok(unsafe { File::from_raw_fd(fd) })
    }
}

fn rename_noreplace(parent: &OwnedFd, old: &CStr, new: &CStr) -> io::Result<()> {
    // SAFETY: both names are relative to the same valid directory descriptor.
    let result = unsafe {
        libc::renameat2(
            parent.as_raw_fd(),
            old.as_ptr(),
            parent.as_raw_fd(),
            new.as_ptr(),
            libc::RENAME_NOREPLACE,
        )
    };
    if result == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

fn sync_directory(directory: &OwnedFd) -> io::Result<()> {
    // SAFETY: the descriptor remains open for the duration of the call.
    if unsafe { libc::fsync(directory.as_raw_fd()) } == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

fn unlink_at(parent: &OwnedFd, name: &CStr) {
    // SAFETY: descriptor and relative name are valid; failures are intentionally best-effort cleanup.
    let _ = unsafe { libc::unlinkat(parent.as_raw_fd(), name.as_ptr(), 0) };
}

#[cfg(test)]
mod tests {
    use super::{FaultPoint, MountError, ReportCategory, SdError, publish, select_mount};
    use std::fs;
    use std::os::unix::fs::symlink;
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT: AtomicU64 = AtomicU64::new(0);
    const VALID: &str = "/dev/mmcblk1p1 /media/MPCSAMPLE exfat rw,nosuid,nodev,noexec 0 0\n";

    struct Fixture(PathBuf);

    impl Fixture {
        fn new() -> Self {
            let id = NEXT.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("hereticc-m03b-a2-{}-{id}", std::process::id()));
            fs::create_dir(&path).expect("create fixture");
            Self(path)
        }

        fn path(&self) -> &Path {
            &self.0
        }

        fn results(&self) -> PathBuf {
            self.0.join("hereticc-results")
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn selects_one_expected_writable_mount() {
        assert_eq!(select_mount(VALID).unwrap(), Path::new("/media/MPCSAMPLE"));
    }

    #[test]
    fn rejects_missing_read_only_ambiguous_and_unexpected_mounts() {
        assert_eq!(select_mount("").unwrap_err(), MountError::Missing);
        assert_eq!(
            select_mount("/dev/mmcblk1p1 /media/MPCSAMPLE exfat ro,noexec 0 0\n").unwrap_err(),
            MountError::Unexpected
        );
        assert_eq!(
            select_mount(&format!("{VALID}{VALID}")).unwrap_err(),
            MountError::Ambiguous
        );
        for bad in [
            "/dev/mmcblk1p1 /mnt/MPCSAMPLE exfat rw 0 0\n",
            "/dev/mmcblk1p1 /media/MPCSAMPLE ext4 rw 0 0\n",
            "/dev/mmcblk1p1 /media/A/B exfat rw 0 0\n",
            "/dev/mmcblk1p1 /media/BAD\\040NAME exfat rw 0 0\n",
        ] {
            assert_eq!(select_mount(bad).unwrap_err(), MountError::Unexpected);
        }
    }

    #[test]
    fn writes_exclusively_and_repeated_invocation_uses_bounded_slots() {
        let fixture = Fixture::new();
        publish(
            fixture.path(),
            b"first\n",
            ReportCategory::Unexpected,
            None,
            false,
        )
        .unwrap();
        publish(
            fixture.path(),
            b"second\n",
            ReportCategory::Unexpected,
            None,
            false,
        )
        .unwrap();
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-unexpected-00.txt")).unwrap(),
            b"first\n"
        );
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-unexpected-01.txt")).unwrap(),
            b"second\n"
        );
        assert_eq!(fs::read_dir(fixture.path()).unwrap().count(), 1);
    }

    #[test]
    fn existing_names_are_not_overwritten_and_slots_are_bounded() {
        let fixture = Fixture::new();
        fs::create_dir(fixture.results()).unwrap();
        for slot in 0..4 {
            fs::write(
                fixture
                    .results()
                    .join(format!("m03b-a4-unexpected-{slot:02}.txt")),
                b"existing",
            )
            .unwrap();
        }
        assert_eq!(
            publish(
                fixture.path(),
                b"new",
                ReportCategory::Unexpected,
                None,
                false,
            )
            .unwrap_err(),
            SdError::NoFreeSlot
        );
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-unexpected-00.txt")).unwrap(),
            b"existing"
        );
    }

    #[test]
    fn unexpected_slots_cannot_consume_or_fall_back_to_expected_slots() {
        let fixture = Fixture::new();
        for slot in 0..4 {
            publish(
                fixture.path(),
                format!("unexpected-{slot}").as_bytes(),
                ReportCategory::Unexpected,
                None,
                false,
            )
            .unwrap();
        }
        assert_eq!(
            publish(
                fixture.path(),
                b"overflow",
                ReportCategory::Unexpected,
                None,
                false,
            ),
            Err(SdError::NoFreeSlot)
        );
        assert!(!fixture.results().join("m03b-a4-expected-00.txt").exists());
        publish(
            fixture.path(),
            b"stock",
            ReportCategory::Expected,
            None,
            false,
        )
        .unwrap();
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-expected-00.txt")).unwrap(),
            b"stock"
        );
    }

    #[test]
    fn expected_slots_do_not_overwrite_or_fall_back_to_unexpected_slots() {
        let fixture = Fixture::new();
        for slot in 0..2 {
            publish(
                fixture.path(),
                format!("stock-{slot}").as_bytes(),
                ReportCategory::Expected,
                None,
                false,
            )
            .unwrap();
        }
        assert_eq!(
            publish(
                fixture.path(),
                b"third",
                ReportCategory::Expected,
                None,
                false,
            ),
            Err(SdError::NoFreeSlot)
        );
        assert!(!fixture.results().join("m03b-a4-unexpected-00.txt").exists());
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-expected-00.txt")).unwrap(),
            b"stock-0"
        );
    }

    #[test]
    fn namespace_specific_existing_and_partial_names_are_protected() {
        let fixture = Fixture::new();
        fs::create_dir(fixture.results()).unwrap();
        fs::write(
            fixture.results().join("m03b-a4-expected-00.txt"),
            b"existing",
        )
        .unwrap();
        fs::write(
            fixture.results().join(".m03b-a4-expected-01.partial"),
            b"partial",
        )
        .unwrap();
        assert_eq!(
            publish(
                fixture.path(),
                b"new",
                ReportCategory::Expected,
                None,
                false,
            ),
            Err(SdError::NoFreeSlot)
        );
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-expected-00.txt")).unwrap(),
            b"existing"
        );
        assert_eq!(
            fs::read(fixture.results().join(".m03b-a4-expected-01.partial")).unwrap(),
            b"partial"
        );
    }

    #[test]
    fn symlinked_slot_is_skipped_without_touching_its_target() {
        let fixture = Fixture::new();
        let outside = Fixture::new();
        fs::create_dir(fixture.results()).unwrap();
        let target = outside.path().join("target.txt");
        fs::write(&target, b"outside").unwrap();
        symlink(&target, fixture.results().join("m03b-a4-expected-00.txt")).unwrap();

        publish(
            fixture.path(),
            b"stock",
            ReportCategory::Expected,
            None,
            false,
        )
        .unwrap();

        assert_eq!(fs::read(target).unwrap(), b"outside");
        assert_eq!(
            fs::read(fixture.results().join("m03b-a4-expected-01.txt")).unwrap(),
            b"stock"
        );
    }

    #[test]
    fn rejects_symlinked_result_directory() {
        let fixture = Fixture::new();
        let outside = Fixture::new();
        symlink(outside.path(), fixture.results()).unwrap();
        assert_eq!(
            publish(
                fixture.path(),
                b"report",
                ReportCategory::Unexpected,
                None,
                false,
            ),
            Err(SdError::Io)
        );
        assert_eq!(fs::read_dir(outside.path()).unwrap().count(), 0);
    }

    #[test]
    fn partial_write_sync_and_finalization_fail_without_valid_report() {
        for fault in [
            FaultPoint::PartialWrite,
            FaultPoint::FileSync,
            FaultPoint::Finalize,
            FaultPoint::DirectorySync,
        ] {
            let fixture = Fixture::new();
            assert_eq!(
                publish(
                    fixture.path(),
                    b"report",
                    ReportCategory::Unexpected,
                    Some(fault),
                    false,
                ),
                Err(SdError::Io)
            );
            let entries: Vec<_> = fs::read_dir(fixture.results())
                .unwrap()
                .map(|entry| entry.unwrap().file_name())
                .collect();
            assert!(entries.is_empty(), "unexpected files: {entries:?}");
        }
    }

    #[test]
    fn enforces_maximum_report_size() {
        let fixture = Fixture::new();
        publish(
            fixture.path(),
            &[b'x'; 768],
            ReportCategory::Unexpected,
            None,
            false,
        )
        .unwrap();
        let other = Fixture::new();
        assert_eq!(
            publish(
                other.path(),
                &[b'x'; 769],
                ReportCategory::Unexpected,
                None,
                false,
            ),
            Err(SdError::ReportTooLarge)
        );
        assert_eq!(fs::read_dir(other.path()).unwrap().count(), 0);
    }
}
