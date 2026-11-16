//! Narrow Linux acquisition boundary for the P3.2 lifecycle collector.
//!
//! It permits fixed read-only commands and exact PID-derived proc paths only.

use std::collections::BTreeSet;
use std::fmt::Write as _;
use std::fs;
use std::io::{self, Read};
use std::os::unix::fs::MetadataExt;
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use neurogenic_b1_collector_model::ObservationInputs;

pub mod firmware;
pub mod sd;

pub const MAX_COMMAND_STREAM_BYTES: usize = 16 * 1024;
pub const MAX_DIRECT_READ_BYTES: usize = 16 * 1024;
pub const EXFAT_MAGIC: u64 = 0x2011_bab0;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AcquireError {
    Deadline,
    MissingCommand(&'static str),
    CommandFailed(&'static str),
    Containment(&'static str),
    OutputTooLarge(&'static str),
    InvalidUtf8(&'static str),
    InvalidMainPid,
    File(&'static str),
    Mount,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommandOutput {
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FixedCommand {
    program: &'static str,
    args: &'static [&'static str],
    label: &'static str,
}

pub trait CommandRunner {
    /// Run one internally selected command within an absolute deadline.
    ///
    /// # Errors
    ///
    /// Returns a bounded acquisition error for spawn, timeout, status, or output failures.
    fn run(
        &mut self,
        command: FixedCommand,
        deadline: Instant,
    ) -> Result<CommandOutput, AcquireError>;
}

pub trait ExactFileReader {
    /// Read one exact path with a byte and time bound.
    ///
    /// # Errors
    ///
    /// Returns a bounded acquisition error for I/O, size, or deadline failures.
    fn read(
        &mut self,
        path: &Path,
        limit: usize,
        deadline: Instant,
    ) -> Result<Vec<u8>, AcquireError>;
    /// Read one exact symbolic-link value.
    ///
    /// # Errors
    ///
    /// Returns a bounded acquisition error for I/O or deadline failures.
    fn read_link(&mut self, path: &Path, deadline: Instant) -> Result<PathBuf, AcquireError>;
    /// Read mode metadata for one exact path.
    ///
    /// # Errors
    ///
    /// Returns a bounded acquisition error for I/O or deadline failures.
    fn mode(&mut self, path: &Path, deadline: Instant) -> Result<u32, AcquireError>;
}

#[derive(Default)]
pub struct SystemFileReader;

impl ExactFileReader for SystemFileReader {
    fn read(
        &mut self,
        path: &Path,
        limit: usize,
        deadline: Instant,
    ) -> Result<Vec<u8>, AcquireError> {
        check_deadline(deadline)?;
        let mut file = fs::File::open(path).map_err(|_| AcquireError::File("open"))?;
        let mut output = Vec::with_capacity(limit.min(4096));
        file.by_ref()
            .take(u64::try_from(limit).unwrap_or(u64::MAX) + 1)
            .read_to_end(&mut output)
            .map_err(|_| AcquireError::File("read"))?;
        check_deadline(deadline)?;
        if output.len() > limit {
            return Err(AcquireError::OutputTooLarge("file"));
        }
        Ok(output)
    }

    fn read_link(&mut self, path: &Path, deadline: Instant) -> Result<PathBuf, AcquireError> {
        check_deadline(deadline)?;
        let result = fs::read_link(path).map_err(|_| AcquireError::File("readlink"))?;
        check_deadline(deadline)?;
        Ok(result)
    }

    fn mode(&mut self, path: &Path, deadline: Instant) -> Result<u32, AcquireError> {
        check_deadline(deadline)?;
        let result = fs::metadata(path).map_err(|_| AcquireError::File("metadata"))?;
        check_deadline(deadline)?;
        Ok(result.mode() & 0o7777)
    }
}

pub struct BoundedCommandRunner;

fn terminate_group(
    child: &mut std::process::Child,
    process_group: u32,
) -> Result<(), AcquireError> {
    let termination = sd::kill_process_group(process_group);
    let reaped = child
        .wait()
        .map_err(|_| AcquireError::Containment("child-reap"));
    termination.and(reaped.map(|_| ()))
}

impl CommandRunner for BoundedCommandRunner {
    fn run(
        &mut self,
        command: FixedCommand,
        deadline: Instant,
    ) -> Result<CommandOutput, AcquireError> {
        check_deadline(deadline)?;
        let mut child = Command::new(command.program)
            .args(command.args)
            .env_clear()
            .current_dir("/")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .process_group(0)
            .spawn()
            .map_err(|error| {
                if error.kind() == io::ErrorKind::NotFound {
                    AcquireError::MissingCommand(command.label)
                } else {
                    AcquireError::CommandFailed(command.label)
                }
            })?;
        let process_group = child.id();
        let Some(stdout) = child.stdout.take() else {
            terminate_group(&mut child, process_group)?;
            return Err(AcquireError::Containment("stdout-pipe"));
        };
        let Some(stderr) = child.stderr.take() else {
            terminate_group(&mut child, process_group)?;
            return Err(AcquireError::Containment("stderr-pipe"));
        };
        let (limit_tx, limit_rx) = mpsc::channel();
        let stdout_tx = limit_tx.clone();
        let stdout_thread =
            thread::spawn(move || bounded_drain(stdout, MAX_COMMAND_STREAM_BYTES, &stdout_tx));
        let stderr_thread =
            thread::spawn(move || bounded_drain(stderr, MAX_COMMAND_STREAM_BYTES, &limit_tx));
        let status = loop {
            let Ok(status) = child.try_wait() else {
                terminate_group(&mut child, process_group)?;
                let _ = stdout_thread.join();
                let _ = stderr_thread.join();
                return Err(AcquireError::Containment("child-wait"));
            };
            if let Some(status) = status {
                // Fixed utilities must not leave descendants holding inherited pipes.
                // Kill the now-childless group before joining the drain workers.
                sd::kill_process_group(process_group)?;
                break status;
            }
            if limit_rx.try_recv().is_ok() {
                terminate_group(&mut child, process_group)?;
                let _ = stdout_thread.join();
                let _ = stderr_thread.join();
                return Err(AcquireError::OutputTooLarge(command.label));
            }
            if Instant::now() >= deadline {
                terminate_group(&mut child, process_group)?;
                let _ = stdout_thread.join();
                let _ = stderr_thread.join();
                return Err(AcquireError::Deadline);
            }
            thread::sleep(Duration::from_millis(2));
        };
        let stdout = join_stream(stdout_thread, command.label)?;
        let stderr = join_stream(stderr_thread, command.label)?;
        if !status.success() {
            return Err(AcquireError::CommandFailed(command.label));
        }
        Ok(CommandOutput { stdout, stderr })
    }
}

fn join_stream(
    thread: thread::JoinHandle<Result<Vec<u8>, AcquireError>>,
    label: &'static str,
) -> Result<Vec<u8>, AcquireError> {
    match thread
        .join()
        .map_err(|_| AcquireError::CommandFailed(label))?
    {
        Err(AcquireError::OutputTooLarge(_)) => Err(AcquireError::OutputTooLarge(label)),
        result => result,
    }
}

fn bounded_drain(
    mut reader: impl Read,
    limit: usize,
    limit_tx: &mpsc::Sender<()>,
) -> Result<Vec<u8>, AcquireError> {
    let mut kept = Vec::with_capacity(limit.min(4096));
    let mut buffer = [0_u8; 1024];
    let mut total = 0_usize;
    loop {
        let read = reader
            .read(&mut buffer)
            .map_err(|_| AcquireError::CommandFailed("pipe"))?;
        if read == 0 {
            break;
        }
        total = total.saturating_add(read);
        if total > limit {
            let _ = limit_tx.send(());
        }
        if kept.len() < limit {
            let take = read.min(limit - kept.len());
            kept.extend_from_slice(&buffer[..take]);
        }
    }
    if total > limit {
        Err(AcquireError::OutputTooLarge("command"))
    } else {
        Ok(kept)
    }
}

fn check_deadline(deadline: Instant) -> Result<(), AcquireError> {
    if Instant::now() >= deadline {
        Err(AcquireError::Deadline)
    } else {
        Ok(())
    }
}

const SYSTEMCTL_SHOW: FixedCommand = FixedCommand {
    program: "/usr/bin/systemctl",
    args: &[
        "show",
        "ac50.service",
        "--no-pager",
        "--property=Type,KillMode,Restart,MainPID,ControlGroup",
    ],
    label: "systemctl-show",
};
const SYSTEMD_VERSION: FixedCommand = FixedCommand {
    program: "/usr/bin/systemd",
    args: &["--version"],
    label: "systemd-version",
};
const IP_ADDRESS: FixedCommand = FixedCommand {
    program: "/usr/sbin/ip",
    args: &["-o", "-4", "addr", "show"],
    label: "ip-address",
};
const IP_ROUTE: FixedCommand = FixedCommand {
    program: "/usr/sbin/ip",
    args: &["-4", "route", "show", "table", "main"],
    label: "ip-route",
};
const IP_LINK: FixedCommand = FixedCommand {
    program: "/usr/sbin/ip",
    args: &["-o", "link", "show"],
    label: "ip-link",
};
/// Acquire only selected service, exact-process, runtime, and USB evidence.
///
/// # Errors
///
/// Returns an error for missing/invalid critical service or process evidence,
/// bounded command failures, file failures, or deadline expiry.
pub fn acquire(
    runner: &mut impl CommandRunner,
    files: &mut impl ExactFileReader,
    deadline: Instant,
) -> Result<ObservationInputs, AcquireError> {
    let service_output = runner.run(SYSTEMCTL_SHOW, deadline)?;
    let version_output = runner.run(SYSTEMD_VERSION, deadline)?;
    let mut service = service_output.stdout;
    let version = first_systemd_version(&version_output.stdout)?;
    service.extend_from_slice(format!("SystemdVersion={version}\n").as_bytes());
    let main_pid = main_pid(&service)?;
    let proc_root = PathBuf::from(format!("/proc/{main_pid}"));

    let stat_before = files.read(&proc_root.join("stat"), MAX_DIRECT_READ_BYTES, deadline)?;
    let executable = files.read_link(&proc_root.join("exe"), deadline)?;
    let status = files.read(&proc_root.join("status"), MAX_DIRECT_READ_BYTES, deadline)?;
    let cgroup = files.read(&proc_root.join("cgroup"), MAX_DIRECT_READ_BYTES, deadline)?;

    let run_mount = optional_file(files, Path::new("/proc/mounts"), deadline)?;
    let run_mode = optional_mode(files, Path::new("/run"), deadline)?;
    let runtime_mode = optional_mode(files, Path::new("/run/systemd/system"), deadline)?;
    let run_mount = normalize_run(run_mount, run_mode, runtime_mode);

    let network = optional_network(runner, deadline)?;
    let stat_after = files.read(&proc_root.join("stat"), MAX_DIRECT_READ_BYTES, deadline)?;

    let mut unavailable_sources = BTreeSet::new();
    if run_mount.is_none() {
        unavailable_sources.insert("run");
    }
    if network.is_none() {
        unavailable_sources.insert("network");
    }
    Ok(ObservationInputs {
        unavailable_sources,
        service,
        executable: executable.to_string_lossy().into_owned().into_bytes(),
        stat_before,
        status,
        cgroup,
        stat_after,
        run_mount,
        network,
        sockets: None,
    })
}

fn optional_network(
    runner: &mut impl CommandRunner,
    deadline: Instant,
) -> Result<Option<Vec<u8>>, AcquireError> {
    match acquire_network(runner, deadline) {
        Ok(value) => Ok(Some(value)),
        Err(AcquireError::MissingCommand(_) | AcquireError::CommandFailed(_)) => Ok(None),
        Err(error) => Err(error),
    }
}

fn first_systemd_version(bytes: &[u8]) -> Result<u32, AcquireError> {
    let text = std::str::from_utf8(bytes).map_err(|_| AcquireError::InvalidUtf8("systemd"))?;
    text.lines()
        .next()
        .and_then(|line| line.strip_prefix("systemd "))
        .and_then(|tail| tail.split_whitespace().next())
        .and_then(|value| value.parse().ok())
        .ok_or(AcquireError::CommandFailed("systemd-version"))
}

fn main_pid(service: &[u8]) -> Result<u32, AcquireError> {
    let text = std::str::from_utf8(service).map_err(|_| AcquireError::InvalidUtf8("service"))?;
    let pid = text
        .lines()
        .find_map(|line| line.strip_prefix("MainPID="))
        .and_then(|value| value.parse().ok())
        .ok_or(AcquireError::InvalidMainPid)?;
    if pid == 0 {
        Err(AcquireError::InvalidMainPid)
    } else {
        Ok(pid)
    }
}

fn optional_file(
    files: &mut impl ExactFileReader,
    path: &Path,
    deadline: Instant,
) -> Result<Option<Vec<u8>>, AcquireError> {
    optional_source(files.read(path, MAX_DIRECT_READ_BYTES, deadline))
}

fn optional_mode(
    files: &mut impl ExactFileReader,
    path: &Path,
    deadline: Instant,
) -> Result<Option<u32>, AcquireError> {
    optional_source(files.mode(path, deadline))
}

fn optional_source<T>(result: Result<T, AcquireError>) -> Result<Option<T>, AcquireError> {
    match result {
        Ok(value) => Ok(Some(value)),
        Err(AcquireError::File(_)) => Ok(None),
        Err(error) => Err(error),
    }
}

fn normalize_run(
    raw: Option<Vec<u8>>,
    run_mode: Option<u32>,
    runtime_mode: Option<u32>,
) -> Option<Vec<u8>> {
    let raw = raw?;
    let text = std::str::from_utf8(&raw).ok()?;
    let row = text
        .lines()
        .find(|line| line.split_whitespace().nth(1) == Some("/run"))?;
    Some(
        format!(
            "{row}\nRUN_MODE={:o}\nRUNTIME_UNIT_DIR_MODE={:o}\n",
            run_mode?, runtime_mode?
        )
        .into_bytes(),
    )
}

fn acquire_network(
    runner: &mut impl CommandRunner,
    deadline: Instant,
) -> Result<Vec<u8>, AcquireError> {
    let address = runner.run(IP_ADDRESS, deadline)?.stdout;
    let route = runner.run(IP_ROUTE, deadline)?.stdout;
    let link = runner.run(IP_LINK, deadline)?.stdout;
    normalize_network(&address, &route, &link)
}

fn normalize_network(address: &[u8], route: &[u8], link: &[u8]) -> Result<Vec<u8>, AcquireError> {
    let address = std::str::from_utf8(address).map_err(|_| AcquireError::InvalidUtf8("address"))?;
    let route = std::str::from_utf8(route).map_err(|_| AcquireError::InvalidUtf8("route"))?;
    let link = std::str::from_utf8(link).map_err(|_| AcquireError::InvalidUtf8("link"))?;
    let mut output = String::new();
    let mut relevant_interfaces = BTreeSet::new();
    for line in address.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        if let Some(index) = words
            .iter()
            .position(|word| word.starts_with("192.168.155.1/"))
        {
            let interface = words
                .get(1)
                .ok_or(AcquireError::CommandFailed("ip-address"))?;
            relevant_interfaces.insert(*interface);
            let _ = writeln!(output, "{interface} {}", words[index]);
        }
    }
    for line in route
        .lines()
        .filter(|line| line.starts_with("192.168.155.0/28 "))
    {
        if let Some(interface) = line
            .split_whitespace()
            .collect::<Vec<_>>()
            .windows(2)
            .find(|pair| pair[0] == "dev")
            .map(|pair| pair[1])
        {
            relevant_interfaces.insert(interface);
        }
        output.push_str(line);
        output.push('\n');
    }
    for line in link.lines() {
        let words: Vec<_> = line.split_whitespace().collect();
        let Some(interface) = words.get(1).map(|value| value.trim_end_matches(':')) else {
            continue;
        };
        if !relevant_interfaces.contains(interface) {
            continue;
        }
        let state = words
            .windows(2)
            .find(|pair| pair[0] == "state")
            .map_or("UNKNOWN", |pair| pair[1]);
        let _ = writeln!(output, "LINK {interface} {state}");
    }
    Ok(output.into_bytes())
}

/// Select exactly one safe writable exFAT SD mount with matching filesystem magic.
///
/// # Errors
///
/// Returns [`AcquireError::Mount`] for absent, duplicate, unsafe, read-only, or
/// wrong-filesystem evidence and an encoding error for invalid UTF-8.
pub fn validate_sd_mount(mounts: &[u8], filesystem_magic: u64) -> Result<PathBuf, AcquireError> {
    if mounts.len() > MAX_DIRECT_READ_BYTES || filesystem_magic != EXFAT_MAGIC {
        return Err(AcquireError::Mount);
    }
    let text = std::str::from_utf8(mounts).map_err(|_| AcquireError::InvalidUtf8("mounts"))?;
    let matches: Vec<_> = text
        .lines()
        .filter_map(|line| {
            let words: Vec<_> = line.split_whitespace().collect();
            if words.len() < 4 || words[0] != "/dev/mmcblk1p1" || words[2] != "exfat" {
                return None;
            }
            let options: BTreeSet<_> = words[3].split(',').collect();
            if !options.contains("rw") || options.contains("ro") {
                return None;
            }
            let component = words[1].strip_prefix("/media/")?;
            if component.is_empty()
                || component.contains('/')
                || !component
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte))
            {
                return None;
            }
            Some(PathBuf::from(words[1]))
        })
        .collect();
    if matches.len() == 1 {
        Ok(matches[0].clone())
    } else {
        Err(AcquireError::Mount)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    struct MockRunner {
        outputs: BTreeMap<&'static str, Result<CommandOutput, AcquireError>>,
        seen: Vec<&'static str>,
    }

    impl CommandRunner for MockRunner {
        fn run(
            &mut self,
            command: FixedCommand,
            _deadline: Instant,
        ) -> Result<CommandOutput, AcquireError> {
            self.seen.push(command.label);
            self.outputs
                .get(command.label)
                .cloned()
                .expect("expected command")
        }
    }

    #[derive(Default)]
    struct MockFiles {
        files: BTreeMap<PathBuf, Result<Vec<u8>, AcquireError>>,
        links: BTreeMap<PathBuf, Result<PathBuf, AcquireError>>,
        modes: BTreeMap<PathBuf, Result<u32, AcquireError>>,
        seen: Vec<PathBuf>,
    }

    impl ExactFileReader for MockFiles {
        fn read(
            &mut self,
            path: &Path,
            _limit: usize,
            _deadline: Instant,
        ) -> Result<Vec<u8>, AcquireError> {
            self.seen.push(path.to_path_buf());
            self.files
                .get(path)
                .cloned()
                .unwrap_or(Err(AcquireError::File("missing fixture")))
        }

        fn read_link(&mut self, path: &Path, _deadline: Instant) -> Result<PathBuf, AcquireError> {
            self.seen.push(path.to_path_buf());
            self.links
                .get(path)
                .cloned()
                .unwrap_or(Err(AcquireError::File("missing fixture")))
        }

        fn mode(&mut self, path: &Path, _deadline: Instant) -> Result<u32, AcquireError> {
            self.seen.push(path.to_path_buf());
            self.modes
                .get(path)
                .cloned()
                .unwrap_or(Err(AcquireError::File("missing fixture")))
        }
    }

    #[allow(clippy::unnecessary_wraps)] // Keeps fixture entries concise and typed as runner results.
    fn output(stdout: &[u8]) -> Result<CommandOutput, AcquireError> {
        Ok(CommandOutput {
            stdout: stdout.to_vec(),
            stderr: Vec::new(),
        })
    }

    fn stat(pid: u32, start: u64) -> Vec<u8> {
        let mut fields = vec!["0".to_owned(); 20];
        fields[0] = "S".into();
        fields[19] = start.to_string();
        format!("{pid} (MPC Sample) {}\n", fields.join(" ")).into_bytes()
    }

    fn complete_mocks() -> (MockRunner, MockFiles) {
        let runner = MockRunner {
            outputs: BTreeMap::from([
                (
                    "systemctl-show",
                    output(b"Type=notify\nKillMode=control-group\nRestart=always\nMainPID=42\nControlGroup=/system.slice/ac50.service\n"),
                ),
                ("systemd-version", output(b"systemd 252 (252.1)\n")),
                ("ip-address", output(b"3: usb0    inet 192.168.155.1/28 scope global usb0\n")),
                ("ip-route", output(b"192.168.155.0/28 dev usb0 scope link\n")),
                ("ip-link", output(b"3: usb0: <BROADCAST,UP> mtu 1500 state UP\n")),
            ]),
            seen: Vec::new(),
        };
        let root = PathBuf::from("/proc/42");
        let mut files = MockFiles::default();
        files.files.insert(root.join("stat"), Ok(stat(42, 900)));
        files.files.insert(
            root.join("status"),
            Ok(b"Uid:\t1000 1000 1000 1000\nGid:\t1000 1000 1000 1000\n".to_vec()),
        );
        files.files.insert(
            root.join("cgroup"),
            Ok(b"0::/system.slice/ac50.service\n".to_vec()),
        );
        files
            .links
            .insert(root.join("exe"), Ok(PathBuf::from("/usr/bin/MPC Sample")));
        files.files.insert(
            PathBuf::from("/proc/mounts"),
            Ok(b"tmpfs /run tmpfs rw,nosuid,nodev 0 0\n".to_vec()),
        );
        files.modes.insert(PathBuf::from("/run"), Ok(0o755));
        files
            .modes
            .insert(PathBuf::from("/run/systemd/system"), Ok(0o755));
        (runner, files)
    }

    #[test]
    fn acquisition_uses_only_selected_main_pid_and_fixed_commands() {
        let (mut runner, mut files) = complete_mocks();
        let observations = acquire(
            &mut runner,
            &mut files,
            Instant::now() + Duration::from_secs(1),
        )
        .expect("acquire");
        assert!(observations.unavailable_sources.is_empty());
        assert_eq!(
            runner.seen,
            [
                "systemctl-show",
                "systemd-version",
                "ip-address",
                "ip-route",
                "ip-link",
            ]
        );
        assert!(files.seen.iter().all(|path| {
            path.starts_with("/proc/42")
                || matches!(
                    path.to_str(),
                    Some("/proc/mounts" | "/run" | "/run/systemd/system")
                )
        }));
        assert_eq!(
            files
                .seen
                .iter()
                .filter(|path| path.ends_with("stat"))
                .count(),
            2
        );
    }

    #[test]
    fn invalid_main_pid_stops_before_proc_access() {
        let (mut runner, mut files) = complete_mocks();
        runner
            .outputs
            .insert("systemctl-show", output(b"MainPID=0\n"));
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("zero PID"),
            AcquireError::InvalidMainPid
        );
        assert_eq!(files.seen.len(), 0);
    }

    #[test]
    fn critical_command_format_deadline_and_permission_failures_are_terminal() {
        let (mut runner, mut files) = complete_mocks();
        runner.outputs.insert(
            "systemctl-show",
            Err(AcquireError::MissingCommand("systemctl-show")),
        );
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("missing critical utility"),
            AcquireError::MissingCommand("systemctl-show")
        );

        let (mut runner, mut files) = complete_mocks();
        runner
            .outputs
            .insert("systemd-version", output(b"not-systemd\n"));
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("unsupported systemd output"),
            AcquireError::CommandFailed("systemd-version")
        );

        let (mut runner, mut files) = complete_mocks();
        runner
            .outputs
            .insert("systemd-version", Err(AcquireError::Deadline));
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("critical deadline"),
            AcquireError::Deadline
        );

        let (mut runner, mut files) = complete_mocks();
        files.files.insert(
            PathBuf::from("/proc/42/status"),
            Err(AcquireError::File("permission")),
        );
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("critical permission failure"),
            AcquireError::File("permission")
        );
    }

    #[test]
    fn unavailable_optional_commands_produce_partial_inputs() {
        let (mut runner, mut files) = complete_mocks();
        runner.outputs.insert(
            "ip-address",
            Err(AcquireError::MissingCommand("ip-address")),
        );
        let observations = acquire(
            &mut runner,
            &mut files,
            Instant::now() + Duration::from_secs(1),
        )
        .expect("partial acquisition");
        assert!(observations.unavailable_sources.contains("network"));
    }

    #[test]
    fn optional_network_deadline_and_malformed_results_are_terminal() {
        for label in ["ip-address", "ip-route", "ip-link"] {
            let (mut runner, mut files) = complete_mocks();
            runner.outputs.insert(label, Err(AcquireError::Deadline));
            assert_eq!(
                acquire(
                    &mut runner,
                    &mut files,
                    Instant::now() + Duration::from_secs(1)
                )
                .expect_err("deadline must remain terminal"),
                AcquireError::Deadline
            );
        }
        let (mut runner, mut files) = complete_mocks();
        runner.outputs.insert("ip-address", output(&[0xff]));
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("malformed optional source is terminal"),
            AcquireError::InvalidUtf8("address")
        );

        let (mut runner, mut files) = complete_mocks();
        files
            .files
            .insert(PathBuf::from("/proc/mounts"), Err(AcquireError::Deadline));
        assert_eq!(
            acquire(
                &mut runner,
                &mut files,
                Instant::now() + Duration::from_secs(1)
            )
            .expect_err("optional run deadline must remain terminal"),
            AcquireError::Deadline
        );
    }

    #[test]
    fn network_normalization_excludes_unrelated_interfaces() {
        let normalized = normalize_network(
            b"2: eth0 inet 10.0.0.2/24 scope global eth0\n3: usb0 inet 192.168.155.1/28 scope global usb0\n",
            b"10.0.0.0/24 dev eth0\n192.168.155.0/28 dev usb0 scope link\n",
            b"2: eth0: <UP> mtu 1500 state UP\n3: usb0: <UP> mtu 1500 state UP\n",
        )
        .expect("normalize selected USB evidence");
        let text = String::from_utf8(normalized).expect("ASCII output");
        assert!(text.contains("usb0 192.168.155.1/28"));
        assert!(text.contains("192.168.155.0/28 dev usb0"));
        assert!(text.contains("LINK usb0 UP"));
        assert!(!text.contains("eth0"));
        assert!(!text.contains("10.0.0"));
    }

    #[test]
    fn direct_reader_enforces_size_utf8_is_left_to_parser_and_expired_deadline() {
        let root = std::env::temp_dir().join(format!(
            "neurogenic-acquire-{}",
            Instant::now().elapsed().as_nanos()
        ));
        fs::write(&root, b"12345").expect("fixture");
        let mut reader = SystemFileReader;
        assert_eq!(
            reader.read(&root, 4, Instant::now() + Duration::from_secs(1)),
            Err(AcquireError::OutputTooLarge("file"))
        );
        assert_eq!(
            reader.read(&root, 8, Instant::now()),
            Err(AcquireError::Deadline)
        );
        fs::remove_file(root).expect("cleanup");
    }

    #[test]
    fn command_runner_times_out_kills_and_reaps_exact_child() {
        let program: &'static str = Box::leak(
            std::env::var("PATH")
                .expect("PATH")
                .split(':')
                .map(Path::new)
                .map(|path| path.join("sleep"))
                .find(|path| path.is_file())
                .expect("sleep")
                .to_string_lossy()
                .into_owned()
                .into_boxed_str(),
        );
        let mut runner = BoundedCommandRunner;
        assert_eq!(
            runner.run(
                FixedCommand {
                    program,
                    args: &["5"],
                    label: "sleep-test",
                },
                Instant::now() + Duration::from_millis(20),
            ),
            Err(AcquireError::Deadline)
        );
    }

    #[test]
    fn command_runner_kills_group_descendant_retaining_output_pipe() {
        let started = Instant::now();
        let mut runner = BoundedCommandRunner;
        let result = runner.run(
            FixedCommand {
                program: "/bin/sh",
                args: &["-c", "while :; do :; done & exit 0"],
                label: "descendant-test",
            },
            Instant::now() + Duration::from_millis(500),
        );
        assert_eq!(
            result,
            Ok(CommandOutput {
                stdout: vec![],
                stderr: vec![]
            })
        );
        assert!(started.elapsed() < Duration::from_secs(1));
    }

    #[test]
    fn command_runner_rejects_missing_and_oversized_output() {
        let mut runner = BoundedCommandRunner;
        assert_eq!(
            runner.run(
                FixedCommand {
                    program: "/definitely/missing/neurogenic-command",
                    args: &[],
                    label: "missing-test",
                },
                Instant::now() + Duration::from_secs(1),
            ),
            Err(AcquireError::MissingCommand("missing-test"))
        );
        let program: &'static str = Box::leak(
            std::env::var("PATH")
                .expect("PATH")
                .split(':')
                .map(Path::new)
                .map(|path| path.join("seq"))
                .find(|path| path.is_file())
                .expect("seq")
                .to_string_lossy()
                .into_owned()
                .into_boxed_str(),
        );
        assert_eq!(
            runner.run(
                FixedCommand {
                    program,
                    args: &["1", "10000"],
                    label: "oversize-test",
                },
                Instant::now() + Duration::from_secs(2),
            ),
            Err(AcquireError::OutputTooLarge("oversize-test"))
        );

        let false_program: &'static str = Box::leak(
            std::env::var("PATH")
                .expect("PATH")
                .split(':')
                .map(Path::new)
                .map(|path| path.join("false"))
                .find(|path| path.is_file())
                .expect("false")
                .to_string_lossy()
                .into_owned()
                .into_boxed_str(),
        );
        assert_eq!(
            runner.run(
                FixedCommand {
                    program: false_program,
                    args: &[],
                    label: "exit-failure-test",
                },
                Instant::now() + Duration::from_secs(1),
            ),
            Err(AcquireError::CommandFailed("exit-failure-test"))
        );
    }

    #[test]
    fn sd_mount_validation_requires_unique_exact_exfat_identity() {
        let valid = b"/dev/mmcblk1p1 /media/MPCSAMPLE exfat rw,nosuid,nodev 0 0\n";
        assert_eq!(
            validate_sd_mount(valid, EXFAT_MAGIC).expect("valid mount"),
            PathBuf::from("/media/MPCSAMPLE")
        );
        for invalid in [
            b"/dev/mmcblk1p1 /media/MPCSAMPLE exfat ro 0 0\n".as_slice(),
            b"/dev/mmcblk1p1 /mnt/MPCSAMPLE exfat rw 0 0\n".as_slice(),
            b"/dev/other /media/MPCSAMPLE exfat rw 0 0\n".as_slice(),
            b"/dev/mmcblk1p1 /media/BAD/NAME exfat rw 0 0\n".as_slice(),
        ] {
            assert_eq!(
                validate_sd_mount(invalid, EXFAT_MAGIC),
                Err(AcquireError::Mount)
            );
        }
        let duplicate = [valid.as_slice(), valid.as_slice()].concat();
        assert_eq!(
            validate_sd_mount(&duplicate, EXFAT_MAGIC),
            Err(AcquireError::Mount)
        );
        assert_eq!(validate_sd_mount(valid, 0), Err(AcquireError::Mount));
    }
}
