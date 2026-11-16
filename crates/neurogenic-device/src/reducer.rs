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
    /// A stock-engine action to perform while callback-local objects are live.
    pub playback: Playback,
}

/// Bounded playback request emitted by the platform-independent reducer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Playback {
    /// No stock-engine call.
    None,
    /// Trigger this zero-based stock pad.
    NoteOn(u8),
    /// Release this zero-based stock pad.
    NoteOff(u8),
}

impl Outcome {
    const FORWARD: Self = Self {
        flow: Flow::Forward,
        repaint: false,
        restore: false,
        playback: Playback::None,
    };

    const fn consumed(repaint: bool, restore: bool) -> Self {
        Self {
            flow: Flow::Consume,
            repaint,
            restore,
            playback: Playback::None,
        }
    }

    const fn playback(flow: Flow, repaint: bool, restore: bool, playback: Playback) -> Self {
        Self {
            flow,
            repaint,
            restore,
            playback,
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
    pub fn pad(&mut self, pad: i32, pressed: bool, callback_supported: bool) -> Outcome {
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
                // If a release lacks the live SDK objects required for the
                // explicit note-off, forwarding the matching physical release
                // is the only safe fallback. The target is always that pad.
                return Outcome::playback(
                    if callback_supported {
                        Flow::Consume
                    } else {
                        Flow::Forward
                    },
                    close,
                    close,
                    if callback_supported {
                        Playback::NoteOff(pad)
                    } else {
                        Playback::None
                    },
                );
            }
            return Outcome::consumed(false, false);
        }
        if pressed && self.open {
            if !callback_supported {
                return Outcome::FORWARD;
            }
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
                self.active_audition = Some(pad);
                self.pending_repaint = true;
                return Outcome::playback(Flow::Consume, true, false, Playback::NoteOn(pad));
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
            playback: Playback::None,
        }
    }
}
