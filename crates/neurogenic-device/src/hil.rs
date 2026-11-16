//! Transport-independent, test-build-only reducer command protocol.
//!
//! This module deliberately performs no I/O and exposes no firmware objects.
//! A future Level B bridge may feed it one bounded command from a safe UI task.

use core::fmt::{self, Write};

use crate::reducer::{Button, CELLS, Flow, Outcome, UiState};

/// Maximum accepted command length, in bytes.
pub const MAX_COMMAND_BYTES: usize = 64;
/// Fixed upper bound for one encoded response.
pub const MAX_RESPONSE_BYTES: usize = 192;

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
