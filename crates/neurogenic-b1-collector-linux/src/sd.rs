//! Isolated Linux FFI for process-group cleanup and descriptor-relative SD publication.
#![allow(unsafe_code)]

use std::ffi::{CStr, CString};
use std::fs::File;
use std::io::{self, Write};
use std::mem::MaybeUninit;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use neurogenic_b1_collector_model::{MAX_REPORT_BYTES, RESULT_NAME};

use crate::{AcquireError, EXFAT_MAGIC, MAX_DIRECT_READ_BYTES};

const SD_SOURCE: &str = "/dev/mmcblk1p1";
const SD_FS: &str = "exfat";

/// Terminate the separately created child process group.
///
/// ESRCH is accepted because the direct child and its group may already have exited.
pub(crate) fn kill_process_group(group: u32) -> Result<(), AcquireError> {
    let group = i32::try_from(group).map_err(|_| AcquireError::Containment("process-group"))?;
    // SAFETY: a negative, non-minimum PID addresses only the selected child process group.
    if unsafe { libc::kill(-group, libc::SIGKILL) } == 0 {
        return Ok(());
    }
    let error = io::Error::last_os_error();
    if error.raw_os_error() == Some(libc::ESRCH) {
        Ok(())
    } else {
        Err(AcquireError::Containment("process-group-kill"))
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MountRecord {
    pub path: PathBuf,
}

/// Select the only permissible writable exFAT mount record.
///
/// # Errors
///
/// Returns a bounded error for oversized, malformed, ambiguous, or unexpected records.
pub fn select_mount(mounts: &[u8]) -> Result<MountRecord, AcquireError> {
    if mounts.len() > MAX_DIRECT_READ_BYTES {
        return Err(AcquireError::Mount);
    }
    let text = std::str::from_utf8(mounts).map_err(|_| AcquireError::InvalidUtf8("mounts"))?;
    let mut source_rows = 0_usize;
    let mut candidate = None;
    for line in text.lines() {
        let words: Vec<_> = line.split_ascii_whitespace().collect();
        if words.first().copied() != Some(SD_SOURCE) {
            continue;
        }
        source_rows = source_rows.checked_add(1).ok_or(AcquireError::Mount)?;
        if words.len() < 4 || words[2] != SD_FS {
            continue;
        }
        let options: std::collections::BTreeSet<_> = words[3].split(',').collect();
        if !options.contains("rw") || options.contains("ro") || !safe_mount_path(words[1]) {
            continue;
        }
        if candidate.replace(PathBuf::from(words[1])).is_some() {
            return Err(AcquireError::Mount);
        }
    }
    match (source_rows, candidate) {
        (1, Some(path)) => Ok(MountRecord { path }),
        _ => Err(AcquireError::Mount),
    }
}

fn safe_mount_path(path: &str) -> bool {
    let Some(component) = path.strip_prefix("/media/") else {
        return false;
    };
    !component.is_empty()
        && !component.contains('/')
        && component
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte))
}

/// Retained, no-follow descriptor for the validated SD mount.
pub struct SdDestination {
    descriptor: OwnedFd,
    record: MountRecord,
    device: u64,
    inode: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PublicationStage {
    BeforeFileSync,
    BeforeDirectorySync,
}

impl SdDestination {
    /// Open and validate one live exFAT mount without following its final component.
    ///
    /// # Errors
    ///
    /// Returns an error unless the unique mount, descriptor, and live filesystem agree.
    pub fn open(mounts: &[u8]) -> Result<Self, AcquireError> {
        let record = select_mount(mounts)?;
        let descriptor = open_directory(&record.path).map_err(|_| AcquireError::Mount)?;
        if filesystem_magic(&descriptor).map_err(|_| AcquireError::Mount)? != EXFAT_MAGIC {
            return Err(AcquireError::Mount);
        }
        let (device, inode) = descriptor_identity(&descriptor).map_err(|_| AcquireError::Mount)?;
        Ok(Self {
            descriptor,
            record,
            device,
            inode,
        })
    }

    /// Publish one bounded report relative to the retained mount descriptor.
    ///
    /// The current mount table and path identity must still match. Creation is
    /// exclusive and intentionally non-atomic; an interrupted final is not retried.
    ///
    /// # Errors
    ///
    /// Returns an error for changed identity, existing output, or write/sync failure.
    pub fn publish(&self, current_mounts: &[u8], report: &[u8]) -> Result<(), AcquireError> {
        if report.len() > MAX_REPORT_BYTES || select_mount(current_mounts)? != self.record {
            return Err(AcquireError::Mount);
        }
        let current = open_directory(&self.record.path).map_err(|_| AcquireError::Mount)?;
        let current_identity = descriptor_identity(&current).map_err(|_| AcquireError::Mount)?;
        if current_identity != (self.device, self.inode)
            || filesystem_magic(&current).map_err(|_| AcquireError::Mount)? != EXFAT_MAGIC
        {
            return Err(AcquireError::Mount);
        }
        self.publish_relative(report, |_| Ok(()))
    }

    fn publish_relative(
        &self,
        report: &[u8],
        mut inject: impl FnMut(PublicationStage) -> Result<(), AcquireError>,
    ) -> Result<(), AcquireError> {
        if report.len() > MAX_REPORT_BYTES {
            return Err(AcquireError::Mount);
        }
        let name = CString::new(RESULT_NAME).map_err(|_| AcquireError::Mount)?;
        let mut file = create_file_at(&self.descriptor, &name).map_err(|error| {
            if error.kind() == io::ErrorKind::AlreadyExists {
                AcquireError::File("report-exists")
            } else {
                AcquireError::File("report-create")
            }
        })?;
        file.write_all(report)
            .map_err(|_| AcquireError::File("report-write"))?;
        inject(PublicationStage::BeforeFileSync)?;
        file.sync_all()
            .map_err(|_| AcquireError::File("report-sync"))?;
        inject(PublicationStage::BeforeDirectorySync)?;
        sync_directory(&self.descriptor).map_err(|_| AcquireError::File("directory-sync"))
    }

    #[cfg(test)]
    fn open_fixture(path: &Path) -> Result<Self, AcquireError> {
        let descriptor = open_directory(path).map_err(|_| AcquireError::Mount)?;
        let (device, inode) = descriptor_identity(&descriptor).map_err(|_| AcquireError::Mount)?;
        Ok(Self {
            descriptor,
            record: MountRecord {
                path: path.to_path_buf(),
            },
            device,
            inode,
        })
    }

    #[cfg(test)]
    fn publish_fixture(&self, report: &[u8]) -> Result<(), AcquireError> {
        let current = open_directory(&self.record.path).map_err(|_| AcquireError::Mount)?;
        if descriptor_identity(&current).map_err(|_| AcquireError::Mount)?
            != (self.device, self.inode)
        {
            return Err(AcquireError::Mount);
        }
        self.publish_relative(report, |_| Ok(()))
    }
}

fn path_cstring(path: &Path) -> io::Result<CString> {
    CString::new(path.as_os_str().as_bytes())
        .map_err(|_| io::Error::from(io::ErrorKind::InvalidInput))
}

fn open_directory(path: &Path) -> io::Result<OwnedFd> {
    let path = path_cstring(path)?;
    // SAFETY: the CString is terminated; flags request a no-follow directory descriptor.
    owned_fd(unsafe {
        libc::open(
            path.as_ptr(),
            libc::O_RDONLY | libc::O_DIRECTORY | libc::O_CLOEXEC | libc::O_NOFOLLOW,
        )
    })
}

fn create_file_at(parent: &OwnedFd, name: &CStr) -> io::Result<File> {
    // SAFETY: parent is open and name is a fixed, NUL-terminated relative filename.
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
        // SAFETY: successful openat returns one newly owned descriptor.
        Ok(unsafe { File::from_raw_fd(fd) })
    }
}

fn owned_fd(fd: libc::c_int) -> io::Result<OwnedFd> {
    if fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        // SAFETY: a successful open returns one newly owned descriptor.
        Ok(unsafe { OwnedFd::from_raw_fd(fd) })
    }
}

fn filesystem_magic(fd: &OwnedFd) -> io::Result<u64> {
    let mut info = MaybeUninit::<libc::statfs>::uninit();
    // SAFETY: info is writable and fd remains open for the call.
    if unsafe { libc::fstatfs(fd.as_raw_fd(), info.as_mut_ptr()) } != 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful fstatfs initialized info.
    u64::try_from(unsafe { info.assume_init() }.f_type)
        .map_err(|_| io::Error::other("negative filesystem magic"))
}

fn descriptor_identity(fd: &OwnedFd) -> io::Result<(u64, u64)> {
    let mut info = MaybeUninit::<libc::stat>::uninit();
    // SAFETY: info is writable and fd remains open for the call.
    if unsafe { libc::fstat(fd.as_raw_fd(), info.as_mut_ptr()) } != 0 {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful fstat initialized info.
    let info = unsafe { info.assume_init() };
    Ok((info.st_dev, info.st_ino))
}

fn sync_directory(fd: &OwnedFd) -> io::Result<()> {
    // SAFETY: fd remains open for the duration of fsync.
    if unsafe { libc::fsync(fd.as_raw_fd()) } == 0 {
        Ok(())
    } else {
        Err(io::Error::last_os_error())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::symlink;

    fn fixture(name: &str) -> PathBuf {
        let path = std::env::temp_dir().join(format!("p32f-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir(&path).expect("fixture");
        path
    }

    #[test]
    fn mount_selection_is_exact_unique_and_safe() {
        let valid = b"/dev/mmcblk1p1 /media/MPCSAMPLE exfat rw,nosuid 0 0\n";
        assert_eq!(
            select_mount(valid).expect("valid").path,
            Path::new("/media/MPCSAMPLE")
        );
        for invalid in [
            b"/dev/mmcblk1p1 /media/MPCSAMPLE exfat ro 0 0\n".as_slice(),
            b"/dev/mmcblk1p1 /media/A/B exfat rw 0 0\n".as_slice(),
            b"/dev/mmcblk1p1 /media/MPCSAMPLE ext4 rw 0 0\n".as_slice(),
            b"/dev/other /media/MPCSAMPLE exfat rw 0 0\n".as_slice(),
        ] {
            assert_eq!(select_mount(invalid), Err(AcquireError::Mount));
        }
        assert_eq!(
            select_mount(&[valid.as_slice(), valid.as_slice()].concat()),
            Err(AcquireError::Mount)
        );
    }

    #[test]
    fn descriptor_publication_is_exclusive_and_detects_replacement() {
        let root = fixture("publish");
        let destination = SdDestination::open_fixture(&root).expect("open");
        destination.publish_fixture(b"report\n").expect("publish");
        assert_eq!(
            std::fs::read(root.join(RESULT_NAME)).expect("read"),
            b"report\n"
        );
        assert!(destination.publish_fixture(b"other\n").is_err());
        let moved = root.with_extension("moved");
        std::fs::rename(&root, &moved).expect("rename");
        std::fs::create_dir(&root).expect("replacement");
        assert_eq!(
            destination.publish_fixture(b"again\n"),
            Err(AcquireError::Mount)
        );
        std::fs::remove_dir_all(root).expect("cleanup replacement");
        std::fs::remove_dir_all(moved).expect("cleanup original");
    }

    #[test]
    fn interrupted_sync_stages_leave_nonretryable_incomplete_final() {
        for stage in [
            PublicationStage::BeforeFileSync,
            PublicationStage::BeforeDirectorySync,
        ] {
            let root = fixture(if stage == PublicationStage::BeforeFileSync {
                "file-sync"
            } else {
                "directory-sync"
            });
            let destination = SdDestination::open_fixture(&root).expect("open");
            let error = destination
                .publish_relative(b"report\n", |current| {
                    if current == stage {
                        Err(AcquireError::File("injected-sync"))
                    } else {
                        Ok(())
                    }
                })
                .expect_err("injected failure");
            assert_eq!(error, AcquireError::File("injected-sync"));
            assert!(root.join(RESULT_NAME).exists());
            assert!(destination.publish_fixture(b"retry\n").is_err());
            std::fs::remove_dir_all(root).expect("cleanup");
        }
    }

    #[test]
    fn no_follow_rejects_symlink_mount() {
        let root = fixture("symlink-target");
        let link = root.with_extension("link");
        let _ = std::fs::remove_file(&link);
        symlink(&root, &link).expect("symlink");
        assert!(SdDestination::open_fixture(&link).is_err());
        std::fs::remove_file(link).expect("cleanup link");
        std::fs::remove_dir_all(root).expect("cleanup root");
    }
}
