//! Bounded B0.1 report publication using the physically validated A4 SD policy.

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
const SLOTS: usize = 2;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Namespace {
    Initial,
    #[allow(dead_code)]
    Events,
}

impl Namespace {
    const fn label(self) -> &'static str {
        match self {
            Self::Initial => "init",
            Self::Events => "events",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Error {
    MissingMount,
    InvalidMount,
    AmbiguousMount,
    TooLarge,
    NoSlot,
    Io,
}

pub(super) fn write_initial_report(report: &str) -> Result<(), ()> {
    write_detected(report.as_bytes(), Namespace::Initial).map_err(|_| ())
}

fn write_detected(report: &[u8], namespace: Namespace) -> Result<(), Error> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(Error::TooLarge);
    }
    let mount = select_mount(&read_mounts()?)?;
    publish(&mount, report, namespace, true, None)
}

fn read_mounts() -> Result<String, Error> {
    let file = File::open(MOUNTS_PATH).map_err(|_| Error::Io)?;
    let mut data = Vec::with_capacity(4096);
    file.take(MOUNTS_LIMIT + 1)
        .read_to_end(&mut data)
        .map_err(|_| Error::Io)?;
    if data.len() as u64 > MOUNTS_LIMIT {
        return Err(Error::TooLarge);
    }
    String::from_utf8(data).map_err(|_| Error::InvalidMount)
}

fn select_mount(mounts: &str) -> Result<PathBuf, Error> {
    let mut records = 0_usize;
    let mut candidate = None;
    for line in mounts.lines() {
        let fields: Vec<_> = line.split_ascii_whitespace().collect();
        if fields.first().copied() != Some(SD_SOURCE) {
            continue;
        }
        records = records.checked_add(1).ok_or(Error::AmbiguousMount)?;
        if fields.len() >= 4
            && fields[2] == SD_FS
            && valid_mount_path(fields[1])
            && fields[3].split(',').any(|item| item == "rw")
            && !fields[3].split(',').any(|item| item == "ro")
            && candidate.replace(PathBuf::from(fields[1])).is_some()
        {
            return Err(Error::AmbiguousMount);
        }
    }
    match (records, candidate) {
        (0, _) => Err(Error::MissingMount),
        (1, Some(path)) => Ok(path),
        (1, None) => Err(Error::InvalidMount),
        _ => Err(Error::AmbiguousMount),
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Fault {
    Write,
    FileSync,
    Rename,
    DirectorySync,
}

fn publish(
    mount: &Path,
    report: &[u8],
    namespace: Namespace,
    verify_fs: bool,
    fault: Option<Fault>,
) -> Result<(), Error> {
    if report.len() > MAX_REPORT_BYTES {
        return Err(Error::TooLarge);
    }
    let mount_fd = open_directory(mount)?;
    if verify_fs && filesystem_magic(&mount_fd)? != EXFAT_MAGIC {
        return Err(Error::InvalidMount);
    }
    make_directory(&mount_fd)?;
    let results_fd = open_directory_at(&mount_fd, RESULT_DIRECTORY)?;

    for slot in 0..SLOTS {
        let final_name = filename(namespace, slot, false)?;
        let partial_name = filename(namespace, slot, true)?;
        if exists(&results_fd, &final_name)? || exists(&results_fd, &partial_name)? {
            continue;
        }
        let mut partial = match create_exclusive(&results_fd, &partial_name) {
            Ok(file) => file,
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => continue,
            Err(_) => return Err(Error::Io),
        };
        if fault == Some(Fault::Write) || partial.write_all(report).is_err() {
            drop(partial);
            unlink(&results_fd, &partial_name);
            return Err(Error::Io);
        }
        if fault == Some(Fault::FileSync) || partial.sync_all().is_err() {
            drop(partial);
            unlink(&results_fd, &partial_name);
            return Err(Error::Io);
        }
        drop(partial);
        if fault == Some(Fault::Rename)
            || rename_noreplace(&results_fd, &partial_name, &final_name).is_err()
        {
            unlink(&results_fd, &partial_name);
            return Err(Error::Io);
        }
        if fault == Some(Fault::DirectorySync) || sync_directory(&results_fd).is_err() {
            unlink(&results_fd, &final_name);
            let _ = sync_directory(&results_fd);
            return Err(Error::Io);
        }
        return Ok(());
    }
    Err(Error::NoSlot)
}

fn filename(namespace: Namespace, slot: usize, partial: bool) -> Result<CString, Error> {
    let name = if partial {
        format!(".m03b-b01-{}-{slot:02}.partial", namespace.label())
    } else {
        format!("m03b-b01-{}-{slot:02}.txt", namespace.label())
    };
    CString::new(name).map_err(|_| Error::Io)
}

fn open_directory(path: &Path) -> Result<OwnedFd, Error> {
    let path = CString::new(path.as_os_str().as_bytes()).map_err(|_| Error::Io)?;
    // SAFETY: path is NUL-terminated and no-follow directory flags are fixed.
    owned(unsafe {
        libc::open(
            path.as_ptr(),
            libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    })
}

fn open_directory_at(parent: &OwnedFd, name: &CStr) -> Result<OwnedFd, Error> {
    // SAFETY: descriptor and fixed relative name are valid.
    owned(unsafe {
        libc::openat(
            parent.as_raw_fd(),
            name.as_ptr(),
            libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    })
}

fn owned(fd: libc::c_int) -> Result<OwnedFd, Error> {
    if fd < 0 {
        Err(Error::Io)
    } else {
        // SAFETY: successful open returned a new owned descriptor.
        Ok(unsafe { OwnedFd::from_raw_fd(fd) })
    }
}

fn filesystem_magic(fd: &OwnedFd) -> Result<libc::c_long, Error> {
    let mut value = std::mem::MaybeUninit::<libc::statfs>::uninit();
    // SAFETY: output storage and descriptor are valid.
    if unsafe { libc::fstatfs(fd.as_raw_fd(), value.as_mut_ptr()) } != 0 {
        return Err(Error::Io);
    }
    // SAFETY: successful fstatfs initialized the value.
    Ok(unsafe { value.assume_init() }.f_type)
}

fn make_directory(parent: &OwnedFd) -> Result<(), Error> {
    // SAFETY: fixed name and descriptor are valid.
    if unsafe { libc::mkdirat(parent.as_raw_fd(), RESULT_DIRECTORY.as_ptr(), 0o700) } == 0
        || io::Error::last_os_error().kind() == io::ErrorKind::AlreadyExists
    {
        Ok(())
    } else {
        Err(Error::Io)
    }
}

fn exists(parent: &OwnedFd, name: &CStr) -> Result<bool, Error> {
    let mut stat = std::mem::MaybeUninit::<libc::stat>::uninit();
    // SAFETY: no result fields are read on failure.
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
    } else if io::Error::last_os_error().kind() == io::ErrorKind::NotFound {
        Ok(false)
    } else {
        Err(Error::Io)
    }
}

fn create_exclusive(parent: &OwnedFd, name: &CStr) -> io::Result<File> {
    // SAFETY: fixed flags prevent following or replacing a path.
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
        // SAFETY: successful open returned a new owned descriptor.
        Ok(unsafe { File::from_raw_fd(fd) })
    }
}

fn rename_noreplace(parent: &OwnedFd, old: &CStr, new: &CStr) -> io::Result<()> {
    // SAFETY: both names are in one pinned directory.
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
    // SAFETY: descriptor remains valid through this call.
    if unsafe { libc::fsync(directory.as_raw_fd()) } == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

fn unlink(parent: &OwnedFd, name: &CStr) {
    // SAFETY: best-effort removal is constrained to the pinned directory.
    let _ = unsafe { libc::unlinkat(parent.as_raw_fd(), name.as_ptr(), 0) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::unix::fs::symlink;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT: AtomicU64 = AtomicU64::new(0);
    const VALID: &str = "/dev/mmcblk1p1 /media/MPCSAMPLE exfat rw,noexec 0 0\n";

    struct Fixture(PathBuf);

    impl Fixture {
        fn new() -> Self {
            let id = NEXT.fetch_add(1, Ordering::Relaxed);
            let path =
                std::env::temp_dir().join(format!("hereticc-b01-{}-{id}", std::process::id()));
            fs::create_dir(&path).unwrap();
            Self(path)
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
    fn mount_selection_is_exact_and_unambiguous() {
        assert_eq!(select_mount(VALID).unwrap(), Path::new("/media/MPCSAMPLE"));
        assert_eq!(select_mount(""), Err(Error::MissingMount));
        assert_eq!(
            select_mount(&format!("{VALID}{VALID}")),
            Err(Error::AmbiguousMount)
        );
        assert_eq!(
            select_mount("/dev/mmcblk1p1 /media/X exfat ro 0 0\n"),
            Err(Error::InvalidMount)
        );
    }

    #[test]
    fn namespaces_have_independent_bounded_slots_and_no_overwrite() {
        let fixture = Fixture::new();
        for namespace in [Namespace::Initial, Namespace::Events] {
            publish(&fixture.0, b"one", namespace, false, None).unwrap();
            publish(&fixture.0, b"two", namespace, false, None).unwrap();
            assert_eq!(
                publish(&fixture.0, b"three", namespace, false, None),
                Err(Error::NoSlot)
            );
        }
        assert_eq!(
            fs::read(fixture.results().join("m03b-b01-init-00.txt")).unwrap(),
            b"one"
        );
        assert_eq!(
            fs::read(fixture.results().join("m03b-b01-events-00.txt")).unwrap(),
            b"one"
        );
    }

    #[test]
    fn symlinks_and_filesystem_failures_do_not_publish_success() {
        let fixture = Fixture::new();
        let outside = Fixture::new();
        symlink(&outside.0, fixture.results()).unwrap();
        assert_eq!(
            publish(&fixture.0, b"x", Namespace::Initial, false, None),
            Err(Error::Io)
        );
        assert_eq!(fs::read_dir(&outside.0).unwrap().count(), 0);

        for fault in [
            Fault::Write,
            Fault::FileSync,
            Fault::Rename,
            Fault::DirectorySync,
        ] {
            let fixture = Fixture::new();
            assert_eq!(
                publish(&fixture.0, b"x", Namespace::Initial, false, Some(fault)),
                Err(Error::Io)
            );
            if fixture.results().exists() {
                assert_eq!(fs::read_dir(fixture.results()).unwrap().count(), 0);
            }
        }
    }

    #[test]
    fn report_size_is_bounded() {
        let fixture = Fixture::new();
        assert_eq!(
            publish(
                &fixture.0,
                &vec![0; MAX_REPORT_BYTES + 1],
                Namespace::Initial,
                false,
                None
            ),
            Err(Error::TooLarge)
        );
    }
}
