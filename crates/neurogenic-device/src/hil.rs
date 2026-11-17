//! Transport-independent, test-build-only reducer command protocol.
//!
//! The test-only worker owns all mailbox I/O and exposes no firmware objects.

use core::fmt::{self, Write};
use std::fs::{self, OpenOptions};
use std::io::{Read, Write as IoWrite};
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, PoisonError};
use std::time::Duration;

use crate::reducer::{Button, CELLS, Flow, Outcome, UiState};

/// Maximum accepted command length, in bytes.
pub const MAX_COMMAND_BYTES: usize = 64;
/// Fixed upper bound for one encoded response.
pub const MAX_RESPONSE_BYTES: usize = 192;
/// Volatile directory prepared by the HIL deployer.
pub const BRIDGE_DIRECTORY: &str = "/run/neurogenic-hil";
/// Fixed request mailbox.
pub const REQUEST_FILE: &str = "request";
/// Fixed response mailbox.
pub const RESPONSE_FILE: &str = "response";
/// Marker proving that the test-only worker reached its loop.
pub const READY_FILE: &str = "bridge.ready";

/// Protocol parsing or execution failure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Empty,
    TooLong,
    NonAscii,
    Malformed,
    Unknown,
    OutOfRange,
}

/// A bounded response owned by the caller.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Response {
    bytes: [u8; MAX_RESPONSE_BYTES],
    len: usize,
}

impl Response {
    fn new() -> Self {
        Self {
            bytes: [0; MAX_RESPONSE_BYTES],
            len: 0,
        }
    }

    /// Encoded ASCII response.
    #[must_use]
    pub fn as_str(&self) -> &str {
        // SAFETY: `Write` rejects non-ASCII text before copying it.
        unsafe { core::str::from_utf8_unchecked(&self.bytes[..self.len]) }
    }
}

impl Write for Response {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let end = self.len.checked_add(text.len()).ok_or(fmt::Error)?;
        if end > self.bytes.len() || !text.is_ascii() {
            return Err(fmt::Error);
        }
        self.bytes[self.len..end].copy_from_slice(text.as_bytes());
        self.len = end;
        Ok(())
    }
}

fn encode(ui: &UiState, outcome: Outcome) -> Response {
    let mut response = Response::new();
    let flow = match outcome.flow {
        Flow::Forward => "forward",
        Flow::Consume => "consume",
    };
    let bits = ui
        .pattern_bits(usize::from(ui.selected_track()))
        .unwrap_or(0);
    writeln!(
        response,
        "v=1 open={} track={} step={} bits={bits:04x} flow={flow} repaint={} restore={} pending_repaint={}",
        u8::from(ui.is_open()),
        ui.selected_track() + 1,
        ui.selected_step() + 1,
        u8::from(outcome.repaint),
        u8::from(outcome.restore),
        u8::from(ui.pending_repaint()),
    )
    .expect("fixed protocol response fits its static bound");
    response
}

fn neutral() -> Outcome {
    Outcome {
        flow: Flow::Forward,
        repaint: false,
        restore: false,
    }
}

fn virtual_action() -> Outcome {
    Outcome {
        flow: Flow::Consume,
        repaint: true,
        restore: false,
    }
}

#[cfg(feature = "native-sequence-adapter")]
fn native_response(action: &str) -> Response {
    use crate::native_sequence_experiment::{self, Status};
    let accepted = match action {
        "ARM" => native_sequence_experiment::arm(),
        "REMOVE" => native_sequence_experiment::request_remove(),
        "STATUS" => true,
        _ => false,
    };
    let status = match native_sequence_experiment::status() {
        Status::Unavailable => "unavailable",
        Status::Idle => "idle",
        Status::Armed => "armed",
        Status::Accepted => "accepted",
        Status::Observed => "observed",
        Status::RemoveRequested => "remove_requested",
        Status::Removed => "removed",
        Status::Rejected => "rejected",
        Status::Failed => "failed",
    };
    let mut response = Response::new();
    writeln!(
        response,
        "v=1 native={status} accepted={}",
        u8::from(accepted)
    )
    .expect("native response fits");
    response
}

/// Apply one complete bounded command to the existing UI reducer.
///
/// Accepted commands are `OPEN`, `CLOSE`, `MOVE <signed-delta>`,
/// `SELECT <1..16>`, `TOGGLE`, `QUERY`, and `RESET`.
///
/// # Errors
///
/// Returns a bounded parsing error for empty, oversized, non-ASCII, malformed,
/// unknown, or out-of-range input. No partial command is applied.
pub fn apply(ui: &mut UiState, input: &[u8]) -> Result<Response, Error> {
    if input.is_empty() {
        return Err(Error::Empty);
    }
    if input.len() > MAX_COMMAND_BYTES {
        return Err(Error::TooLong);
    }
    if !input.is_ascii() {
        return Err(Error::NonAscii);
    }
    let text = core::str::from_utf8(input)
        .map_err(|_| Error::NonAscii)?
        .trim_end_matches(['\r', '\n']);
    if text.is_empty() {
        return Err(Error::Empty);
    }

    #[cfg(feature = "native-sequence-adapter")]
    if let Some(action) = text.strip_prefix("NATIVE ") {
        if matches!(action, "ARM" | "STATUS" | "REMOVE") {
            return Ok(native_response(action));
        }
        return Err(Error::Unknown);
    }

    let outcome = match text {
        "OPEN" if !ui.is_open() => {
            let press = ui.button(Button::Data, true, true);
            let _ = ui.button(Button::Data, false, true);
            press
        }
        "CLOSE" if ui.is_open() => {
            let press = ui.button(Button::Data, true, true);
            let _ = ui.button(Button::Data, false, true);
            press
        }
        "OPEN" | "CLOSE" | "QUERY" => neutral(),
        "TOGGLE" => {
            let press = ui.button(Button::Plus, true, false);
            let _ = ui.button(Button::Plus, false, false);
            press
        }
        "RESET" => {
            *ui = UiState::default();
            virtual_action()
        }
        _ if text.starts_with("MOVE ") => {
            if text[5..].contains(' ') {
                return Err(Error::Malformed);
            }
            let delta = text[5..].parse::<i32>().map_err(|_| Error::Malformed)?;
            if !(-16..=16).contains(&delta) || delta == 0 {
                return Err(Error::OutOfRange);
            }
            ui.encoder(delta)
        }
        _ if text.starts_with("SELECT ") => {
            if text[7..].contains(' ') {
                return Err(Error::Malformed);
            }
            let track = text[7..].parse::<usize>().map_err(|_| Error::Malformed)?;
            if !(1..=CELLS).contains(&track) {
                return Err(Error::OutOfRange);
            }
            let pad = i32::try_from(track - 1).map_err(|_| Error::OutOfRange)?;
            let press = ui.pad(pad, true);
            let _ = ui.pad(pad, false);
            press
        }
        _ if text.split_ascii_whitespace().count() > 1 => return Err(Error::Malformed),
        _ => return Err(Error::Unknown),
    };

    Ok(encode(ui, outcome))
}

fn error_response(error: Error) -> &'static [u8] {
    match error {
        Error::Empty => b"error=empty\n",
        Error::TooLong => b"error=too_long\n",
        Error::NonAscii => b"error=non_ascii\n",
        Error::Malformed => b"error=malformed\n",
        Error::Unknown => b"error=unknown\n",
        Error::OutOfRange => b"error=out_of_range\n",
    }
}

fn process_mailbox(
    directory: &Path,
    ui: &Mutex<UiState>,
    overlay_open: &AtomicBool,
    hil_page_owned: &AtomicBool,
) {
    let request = directory.join(REQUEST_FILE);
    let response = directory.join(RESPONSE_FILE);
    if response.exists() {
        return;
    }
    let Ok(metadata) = fs::symlink_metadata(&request) else {
        return;
    };
    if !metadata.file_type().is_file() || metadata.len() > MAX_COMMAND_BYTES as u64 {
        let _ = fs::remove_file(&request);
        publish(&response, b"error=invalid_request\n");
        return;
    }
    let mut bytes = [0_u8; MAX_COMMAND_BYTES + 1];
    let read = OpenOptions::new()
        .read(true)
        .open(&request)
        .and_then(|mut file| file.read(&mut bytes));
    let _ = fs::remove_file(&request);
    match read {
        Ok(length) if length <= MAX_COMMAND_BYTES => {
            // Publish ownership before taking the reducer lock when OPEN can
            // transition closed -> open. A colliding physical callback then
            // fails closed instead of observing the previous ownership bit.
            let command = bytes[..length]
                .strip_suffix(b"\n")
                .unwrap_or(&bytes[..length]);
            let command = command.strip_suffix(b"\r").unwrap_or(command);
            let opening = command == b"OPEN";
            let claiming_page = opening && !overlay_open.swap(true, Ordering::AcqRel);
            if claiming_page {
                hil_page_owned.store(true, Ordering::Release);
            }
            let mut state = ui.lock().unwrap_or_else(PoisonError::into_inner);
            match apply(&mut state, &bytes[..length]) {
                Ok(result) => {
                    overlay_open.store(state.is_open(), Ordering::Release);
                    if command == b"CLOSE"
                        || command == b"RESET"
                        || (claiming_page && !state.is_open())
                    {
                        hil_page_owned.store(false, Ordering::Release);
                    }
                    publish(&response, result.as_str().as_bytes());
                }
                Err(error) => {
                    if claiming_page {
                        hil_page_owned.store(false, Ordering::Release);
                        overlay_open.store(state.is_open(), Ordering::Release);
                    }
                    publish(&response, error_response(error));
                }
            }
        }
        _ => publish(&response, b"error=invalid_request\n"),
    }
}

fn publish(path: &Path, bytes: &[u8]) {
    if bytes.len() > MAX_RESPONSE_BYTES {
        return;
    }
    let pending = path.with_extension("pending");
    let Ok(mut output) = OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(&pending)
    else {
        return;
    };
    if output.write_all(bytes).is_ok()
        && output.sync_data().is_ok()
        && fs::rename(&pending, path).is_ok()
    {
        return;
    }
    let _ = fs::remove_file(pending);
}

fn bridge_loop(
    directory: &Path,
    ui: &Mutex<UiState>,
    overlay_open: &AtomicBool,
    hil_page_owned: &AtomicBool,
    stop: &AtomicBool,
) {
    if !directory.is_dir() {
        return;
    }
    publish(
        &directory.join(READY_FILE),
        b"schema=neurogenic-hil-bridge-v1\n",
    );
    while !stop.load(Ordering::Acquire) && directory.is_dir() {
        process_mailbox(directory, ui, overlay_open, hil_page_owned);
        std::thread::sleep(Duration::from_millis(20));
    }
}

/// Start the test-only volatile mailbox worker once.
///
/// The worker performs all file I/O. SDK callbacks only use `try_lock`, so the
/// bridge cannot block a UI or audio callback and never observes firmware data.
pub fn start_bridge(
    ui: Arc<Mutex<UiState>>,
    overlay_open: Arc<AtomicBool>,
    hil_page_owned: Arc<AtomicBool>,
) {
    static STARTED: AtomicBool = AtomicBool::new(false);
    if STARTED.swap(true, Ordering::AcqRel) {
        return;
    }
    let _ = std::thread::Builder::new()
        .name("neurogenic-hil".to_owned())
        .spawn(move || {
            let stop = AtomicBool::new(false);
            bridge_loop(
                Path::new(BRIDGE_DIRECTORY),
                &ui,
                &overlay_open,
                &hil_page_owned,
                &stop,
            );
        });
}

#[cfg(test)]
mod bridge_tests {
    use super::{READY_FILE, REQUEST_FILE, RESPONSE_FILE, UiState, bridge_loop};
    use std::fs;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Mutex};
    use std::time::{Duration, Instant};

    fn temporary_directory() -> std::path::PathBuf {
        let path = std::env::temp_dir().join(format!(
            "neurogenic-hil-{}-{:?}",
            std::process::id(),
            std::thread::current().id()
        ));
        let _ = fs::remove_dir_all(&path);
        fs::create_dir(&path).expect("fixture directory");
        path
    }

    fn wait(path: &std::path::Path) -> Vec<u8> {
        let deadline = Instant::now() + Duration::from_secs(2);
        while Instant::now() < deadline {
            if let Ok(bytes) = fs::read(path) {
                return bytes;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        panic!("mailbox response timeout");
    }

    #[test]
    fn mailbox_applies_real_reducer_and_rejects_invalid_input() {
        let directory = temporary_directory();
        let ui = Arc::new(Mutex::new(UiState::default()));
        let stop = Arc::new(AtomicBool::new(false));
        let overlay_open = Arc::new(AtomicBool::new(false));
        let hil_page_owned = Arc::new(AtomicBool::new(false));
        let worker_dir = directory.clone();
        let worker_ui = Arc::clone(&ui);
        let worker_stop = Arc::clone(&stop);
        let worker_open = Arc::clone(&overlay_open);
        let worker_page = Arc::clone(&hil_page_owned);
        let worker = std::thread::spawn(move || {
            bridge_loop(
                &worker_dir,
                &worker_ui,
                &worker_open,
                &worker_page,
                &worker_stop,
            );
        });
        wait(&directory.join(READY_FILE));

        fs::write(directory.join(REQUEST_FILE), b"OPEN\n").expect("request");
        let response = wait(&directory.join(RESPONSE_FILE));
        assert!(response.starts_with(b"v=1 open=1"));
        assert!(overlay_open.load(Ordering::Acquire));
        assert!(hil_page_owned.load(Ordering::Acquire));
        fs::remove_file(directory.join(RESPONSE_FILE)).expect("consume response");

        fs::write(directory.join(REQUEST_FILE), b"SHELL\n").expect("request");
        assert_eq!(wait(&directory.join(RESPONSE_FILE)), b"error=unknown\n");
        fs::remove_file(directory.join(RESPONSE_FILE)).expect("consume response");

        fs::write(directory.join(REQUEST_FILE), b"CLOSE\n").expect("request");
        let response = wait(&directory.join(RESPONSE_FILE));
        assert!(response.starts_with(b"v=1 open=0"));
        assert!(!overlay_open.load(Ordering::Acquire));
        assert!(!hil_page_owned.load(Ordering::Acquire));

        stop.store(true, Ordering::Release);
        worker.join().expect("worker");
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn unpublished_temporary_request_is_ignored_and_stale_response_blocks_work() {
        let directory = temporary_directory();
        let ui = Mutex::new(UiState::default());
        let open = AtomicBool::new(false);
        let page = AtomicBool::new(false);

        fs::write(directory.join("request.pending"), b"OPEN\n").expect("partial fixture");
        super::process_mailbox(&directory, &ui, &open, &page);
        assert!(!ui.lock().expect("state").is_open());

        fs::write(directory.join(RESPONSE_FILE), b"stale\n").expect("stale response");
        fs::write(directory.join(REQUEST_FILE), b"OPEN\n").expect("request");
        super::process_mailbox(&directory, &ui, &open, &page);
        assert!(!ui.lock().expect("state").is_open());
        assert!(directory.join(REQUEST_FILE).exists());

        fs::remove_dir_all(directory).expect("cleanup");
    }
}
