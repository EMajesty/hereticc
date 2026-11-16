//! Platform-independent state reducer for the first device UI.

/// Number of prototype tracks and steps.
pub const CELLS: usize = 16;
const CELLS_I32: i32 = 16;

/// Controls whose complete press/release pair belongs to the open page.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Button {
    /// DATA encoder push, used with SHIFT to open and close.
    Data,
    /// PLUS, used to toggle the selected step.
    Plus,
    /// A button not owned by this prototype.
    Other,
}

/// Whether the stock application should receive an event.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flow {
    /// Forward the event unchanged.
    Forward,
    /// Consume the event as part of an owned interaction.
    Consume,
}

/// The outcome of one state transition.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Outcome {
    /// Forward or consume the input.
    pub flow: Flow,
    /// Ask the live Sample page for another paint.
    pub repaint: bool,
    /// The overlay closed and stock presentation must be restored.
    pub restore: bool,
}

/// Latest bounded pad-path observation shown by the overlay.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PadStatus {
    /// No pad callback has reached the mod since construction.
    #[default]
    NoCallback,
    /// The callback was owned, but a paired native dispatch was unavailable.
    DispatchUnavailable,
    /// The callback was owned and SDK-native forwarding was requested.
    NativeForwardRequested,
}

impl Outcome {
    const FORWARD: Self = Self {
        flow: Flow::Forward,
        repaint: false,
        restore: false,
    };

    const fn consumed(repaint: bool, restore: bool) -> Self {
        Self {
            flow: Flow::Consume,
            repaint,
            restore,
        }
    }
}

const DATA_OWNED: u8 = 1 << 0;
const PLUS_OWNED: u8 = 1 << 1;

/// Fixed-capacity UI and input-ownership state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UiState {
    open: bool,
    selected_track: u8,
    selected_step: u8,
    patterns: [u16; CELLS],
    owned_pads: u16,
    owned_buttons: u8,
    pending_repaint: bool,
    active_audition: Option<u8>,
    close_pending: bool,
    pad_status: PadStatus,
    pad_capabilities: u8,
}

impl Default for UiState {
    fn default() -> Self {
        Self {
            open: false,
            selected_track: 0,
            selected_step: 0,
            patterns: [0; CELLS],
            owned_pads: 0,
            owned_buttons: 0,
            pending_repaint: false,
            active_audition: None,
            close_pending: false,
            pad_status: PadStatus::NoCallback,
            pad_capabilities: 0,
        }
    }
}

impl UiState {
    /// Whether the overlay currently owns its declared controls.
    #[must_use]
    pub const fn is_open(&self) -> bool {
        self.open
    }

    /// Selected zero-based track.
    #[must_use]
    pub const fn selected_track(&self) -> u8 {
        self.selected_track
    }

    /// Selected zero-based step.
    #[must_use]
    pub const fn selected_step(&self) -> u8 {
        self.selected_step
    }

    /// The pad with a manually dispatched note-on awaiting its release.
    #[must_use]
    pub const fn active_audition(&self) -> Option<u8> {
        self.active_audition
    }

    /// Whether close is deferred until the active audition is released.
    #[must_use]
    pub const fn close_pending(&self) -> bool {
        self.close_pending
    }

    /// Latest pad-path observation; it is not an audibility claim.
    #[must_use]
    pub const fn pad_status(&self) -> PadStatus {
        self.pad_status
    }

    /// Record non-sensitive capability bits from the latest live pad callback.
    pub fn record_pad_callback(
        &mut self,
        paired_note_sites: bool,
        input_present: bool,
        properties_present: bool,
    ) {
        self.pad_capabilities = u8::from(paired_note_sites)
            | (u8::from(input_present) << 1)
            | (u8::from(properties_present) << 2);
    }

    /// `(paired note hooks, input object, property object)` for diagnostics.
    #[must_use]
    pub const fn pad_capabilities(&self) -> (bool, bool, bool) {
        (
            self.pad_capabilities & 1 != 0,
            self.pad_capabilities & 2 != 0,
            self.pad_capabilities & 4 != 0,
        )
    }

    /// Whether a step is active on a track.
    #[must_use]
    pub fn step_active(&self, track: usize, step: usize) -> bool {
        track < CELLS && step < CELLS && self.patterns[track] & (1_u16 << step) != 0
    }

    /// Consume and clear a pending repaint request.
    pub fn take_repaint(&mut self) -> bool {
        core::mem::take(&mut self.pending_repaint)
    }

    /// Apply one decoded button event.
    pub fn button(&mut self, button: Button, pressed: bool, shift: bool) -> Outcome {
        let owner = match button {
            Button::Data => DATA_OWNED,
            Button::Plus => PLUS_OWNED,
            Button::Other => return Outcome::FORWARD,
        };

        if !pressed && self.owned_buttons & owner != 0 {
            self.owned_buttons &= !owner;
            return Outcome::consumed(false, false);
        }

        if button == Button::Data && pressed && shift && self.owned_buttons & owner == 0 {
            self.owned_buttons |= owner;
            if self.open && self.active_audition.is_some() {
                self.close_pending = true;
                self.pending_repaint = true;
                return Outcome::consumed(true, false);
            }
            self.open = !self.open;
            self.close_pending = false;
            self.pending_repaint = true;
            return Outcome::consumed(true, !self.open);
        }

        if button == Button::Plus && pressed && self.open {
            if self.owned_buttons & owner == 0 {
                self.owned_buttons |= owner;
                let track = usize::from(self.selected_track);
                self.patterns[track] ^= 1_u16 << self.selected_step;
                self.pending_repaint = true;
            }
            return Outcome::consumed(true, false);
        }

        if self.owned_buttons & owner != 0 {
            Outcome::consumed(false, false)
        } else {
            Outcome::FORWARD
        }
    }

    /// Apply a DATA encoder delta.
    pub fn encoder(&mut self, delta: i32) -> Outcome {
        if !self.open || delta == 0 {
            return Outcome::FORWARD;
        }
        let selected = i32::from(self.selected_step)
            .wrapping_add(delta)
            .rem_euclid(CELLS_I32);
        self.selected_step = u8::try_from(selected).unwrap_or(0);
        self.pending_repaint = true;
        Outcome::consumed(true, false)
    }

    /// Apply a zero-based pad event. Invalid pads always forward.
    pub fn pad(&mut self, pad: i32, pressed: bool, native_dispatch_available: bool) -> Outcome {
        let Ok(pad) = u8::try_from(pad) else {
            return Outcome::FORWARD;
        };
        if usize::from(pad) >= CELLS {
            return Outcome::FORWARD;
        }
        let owner = 1_u16 << pad;
        if !pressed && self.owned_pads & owner != 0 {
            self.owned_pads &= !owner;
            if self.active_audition == Some(pad) {
                self.active_audition = None;
                let close = self.close_pending;
                self.close_pending = false;
                if close {
                    self.open = false;
                    self.pending_repaint = true;
                }
                // The physical release follows the same SDK-native path as
                // its forwarded press. No callback-local object is retained.
                return Outcome {
                    flow: Flow::Forward,
                    repaint: close,
                    restore: close,
                };
            }
            return Outcome::consumed(false, false);
        }
        if pressed && self.open {
            if self.owned_pads & owner == 0 {
                // The first held pad is the prototype's sole audition voice.
                // Further pad pairs are consumed but do not trigger or change
                // selection, avoiding an untracked stock voice.
                if self.active_audition.is_some() {
                    self.owned_pads |= owner;
                    return Outcome::consumed(false, false);
                }
                self.owned_pads |= owner;
                self.selected_track = pad;
                self.pending_repaint = true;
                if !native_dispatch_available {
                    self.pad_status = PadStatus::DispatchUnavailable;
                    return Outcome::consumed(true, false);
                }
                self.active_audition = Some(pad);
                self.pad_status = PadStatus::NativeForwardRequested;
                // The SDK calls the displaced stock handler after this
                // callback returns, preserving its exact arguments. This is
                // the supported path for the physical pad, including P0.
                return Outcome {
                    flow: Flow::Forward,
                    repaint: true,
                    restore: false,
                };
            }
            return Outcome::consumed(true, false);
        }
        if self.owned_pads & owner != 0 {
            Outcome::consumed(false, false)
        } else {
            Outcome::FORWARD
        }
    }

    /// Close after losing the Sample page. Owned presses remain draining so
    /// their releases cannot leak to stock after their presses were consumed.
    pub fn lose_page(&mut self) -> Outcome {
        if !self.open {
            return Outcome::FORWARD;
        }
        self.open = false;
        self.close_pending = false;
        self.pending_repaint = true;
        Outcome {
            flow: Flow::Forward,
            repaint: true,
            restore: true,
        }
    }
}
