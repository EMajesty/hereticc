//! Bounded semantic boundary for one future stock-sequencer insertion.
//!
//! This module deliberately constructs no firmware object. The pinned SDK has
//! no stock sequence API, so the native 56-byte value and live command objects
//! must remain owned by a future reviewed SDK adapter on the UI thread.

use neurogenic_core::GRID_STEP_TICKS;

use crate::reducer::UiState;

/// Physical pads and prototype grid steps currently exposed by NEUROGENIC.
pub const PROTOTYPE_CELLS: u8 = 16;
const DEFAULT_NATIVE_VALUE: u32 = 100;
const GRID_STEP_TICKS_I64: i64 = 240;
const _: () = assert!(GRID_STEP_TICKS == GRID_STEP_TICKS_I64 as u64);

/// Validation failure before any stock command can be attempted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RequestError {
    /// Only physical pads 0 through 15 are supported by this prototype.
    PadOutOfRange,
    /// Only grid steps 0 through 15 are supported by this prototype.
    StepOutOfRange,
    /// Stock note velocity must be in the MIDI range 1 through 127.
    VelocityOutOfRange,
    /// The editor overlay is not currently open.
    EditorClosed,
    /// The selected NEUROGENIC step is not active.
    StepInactive,
    /// The selected stock sequence has no usable positive length.
    InvalidSequenceLength,
    /// The requested event starts outside the selected stock sequence.
    OutsideSequence,
}

/// One unresolved note targeting the stock UI's current sequence and track.
///
/// `physical_pad` is intentionally not treated as a MIDI note. The live stock
/// Program/controller must resolve that mapping inside its valid UI callback.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StockNoteRequest {
    physical_pad: u8,
    tick: i64,
    duration: i64,
    velocity: u8,
}

impl StockNoteRequest {
    /// Capture the active selected editor cell into a pointer-free request.
    ///
    /// # Errors
    ///
    /// Rejects a closed editor, an inactive selected step, or an invalid
    /// velocity. The returned value borrows nothing from UI or firmware state.
    pub fn from_active_selection(ui: &UiState, velocity: u8) -> Result<Self, RequestError> {
        if !ui.is_open() {
            return Err(RequestError::EditorClosed);
        }
        let track = ui.selected_track();
        let step = ui.selected_step();
        if !ui.step_active(usize::from(track), usize::from(step)) {
            return Err(RequestError::StepInactive);
        }
        Self::from_grid_step(track, step, velocity)
    }

    /// Translate one zero-based NEUROGENIC step into a 960-PPQ request.
    ///
    /// # Errors
    ///
    /// Rejects pads or steps outside the current 16-cell prototype and MIDI
    /// velocities outside 1 through 127.
    pub fn from_grid_step(physical_pad: u8, step: u8, velocity: u8) -> Result<Self, RequestError> {
        if physical_pad >= PROTOTYPE_CELLS {
            return Err(RequestError::PadOutOfRange);
        }
        if step >= PROTOTYPE_CELLS {
            return Err(RequestError::StepOutOfRange);
        }
        if velocity == 0 || velocity > 127 {
            return Err(RequestError::VelocityOutOfRange);
        }
        let tick = i64::from(step) * GRID_STEP_TICKS_I64;
        Ok(Self {
            physical_pad,
            tick,
            duration: GRID_STEP_TICKS_I64,
            velocity,
        })
    }

    /// Zero-based physical pad requiring live stock resolution.
    #[must_use]
    pub const fn physical_pad(self) -> u8 {
        self.physical_pad
    }

    /// Event position in stock/native 960-PPQ ticks.
    #[must_use]
    pub const fn tick(self) -> i64 {
        self.tick
    }

    /// Fixed one-grid-step duration in ticks.
    #[must_use]
    pub const fn duration(self) -> i64 {
        self.duration
    }

    /// Integer MIDI velocity before native normalization.
    #[must_use]
    pub const fn velocity(self) -> u8 {
        self.velocity
    }

    /// Resolve source-pad identity and sequence bounds without creating raw
    /// native bytes or retaining the resolver's firmware objects.
    ///
    /// # Errors
    ///
    /// Rejects a nonpositive sequence length or an event start outside it.
    pub fn resolve(
        self,
        midi_note: u8,
        sequence_length: i64,
    ) -> Result<NativeTag3ConstructorArgs, RequestError> {
        if sequence_length <= 0 {
            return Err(RequestError::InvalidSequenceLength);
        }
        if self.tick < 0 || self.tick >= sequence_length {
            return Err(RequestError::OutsideSequence);
        }
        Ok(NativeTag3ConstructorArgs {
            midi_note,
            tick: self.tick,
            duration: self.duration,
            default_value: DEFAULT_NATIVE_VALUE,
            flags: 0,
            velocity: f32::from(self.velocity) / 127.0,
        })
    }
}

/// Source-confirmed arguments to the stock tag-3 note constructor.
///
/// This is not the native 56-byte event and is never safe to transmute into
/// one. Construction, container ownership, command dispatch, and Undo remain
/// responsibilities of the unavailable firmware-specific SDK boundary.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NativeTag3ConstructorArgs {
    /// Live Program-resolved MIDI note/pad routing value.
    pub midi_note: u8,
    /// Native event position at 960 PPQ.
    pub tick: i64,
    /// Native note duration at 960 PPQ.
    pub duration: i64,
    /// Default integer constructor value used by the proven upstream path.
    pub default_value: u32,
    /// Initial native flags used by the proven upstream path.
    pub flags: u32,
    /// Velocity normalized to the stock constructor's floating-point input.
    pub velocity: f32,
}

/// Terminal state of one bounded insertion attempt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AttemptState {
    /// No stock command has been called.
    Ready,
    /// The adapter accepted the request once.
    Submitted,
    /// The request was cancelled before submission.
    Cancelled,
    /// The adapter returned an error; retries are refused because command
    /// application may be ambiguous at this boundary.
    Failed,
}

/// Failure owned by the one-shot boundary.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SubmitError<E> {
    /// This request is already terminal and cannot be submitted again.
    Terminal(AttemptState),
    /// The stock adapter rejected or could not complete the request.
    Adapter(E),
}

/// A future UI-thread implementation must create and dispatch the native event
/// synchronously, retain no firmware pointer, and use the stock command/Undo
/// boundary. The current pinned SDK provides no implementation.
pub trait StockSequenceEditPort {
    /// Adapter-specific bounded result. Acceptance is not proof of playback.
    type Receipt;
    /// Adapter-specific failure.
    type Error;

    /// Submit one request against the live current sequence/current track.
    ///
    /// # Errors
    ///
    /// Returns an adapter-defined error if the live context is unavailable,
    /// stale, invalid, or the native command cannot be completed safely.
    fn insert_current_track(
        &mut self,
        request: StockNoteRequest,
    ) -> Result<Self::Receipt, Self::Error>;
}

/// At-most-once owner for one native insertion request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OneShotInsertion {
    request: StockNoteRequest,
    state: AttemptState,
}

impl OneShotInsertion {
    /// Create a request that has not touched firmware.
    #[must_use]
    pub const fn new(request: StockNoteRequest) -> Self {
        Self {
            request,
            state: AttemptState::Ready,
        }
    }

    /// Current terminal/ready state.
    #[must_use]
    pub const fn state(self) -> AttemptState {
        self.state
    }

    /// Cancel before submission. Cancellation after any attempt is refused.
    pub fn cancel(&mut self) -> bool {
        if self.state != AttemptState::Ready {
            return false;
        }
        self.state = AttemptState::Cancelled;
        true
    }

    /// Call the stock adapter at most once.
    ///
    /// The state becomes terminal before the call. An adapter error is not
    /// retried automatically because the native command's application status
    /// could otherwise be ambiguous.
    ///
    /// # Errors
    ///
    /// Returns `Terminal` after submission/cancellation/failure, or the single
    /// adapter error from the first attempt.
    pub fn submit<P: StockSequenceEditPort>(
        &mut self,
        port: &mut P,
    ) -> Result<P::Receipt, SubmitError<P::Error>> {
        if self.state != AttemptState::Ready {
            return Err(SubmitError::Terminal(self.state));
        }
        self.state = AttemptState::Failed;
        match port.insert_current_track(self.request) {
            Ok(receipt) => {
                self.state = AttemptState::Submitted;
                Ok(receipt)
            }
            Err(error) => Err(SubmitError::Adapter(error)),
        }
    }
}
