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

/// Pointer-free cell captured by a transactional PLUS press.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToggleRequest {
    pub track: u8,
    pub step: u8,
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
        }
    }
}

impl UiState {
    /// Enter the native Step Edit presentation without synthesizing input.
    ///
    /// Called only after the verified Step paint callback established a live
    /// editing context. Existing committed bits and receipts are preserved.
    #[cfg(feature = "native-playable-grid")]
    pub(crate) fn activate_native_grid(&mut self) {
        self.open = true;
        self.pending_repaint = true;
    }

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

    /// Active-step bitset for one zero-based track.
    #[must_use]
    pub const fn pattern_bits(&self, track: usize) -> Option<u16> {
        if track < CELLS {
            Some(self.patterns[track])
        } else {
            None
        }
    }

    /// Whether a step is active on a track.
    #[must_use]
    pub fn step_active(&self, track: usize, step: usize) -> bool {
        track < CELLS && step < CELLS && self.patterns[track] & (1_u16 << step) != 0
    }

    /// Commit one stock-verified cell without changing selection or ownership.
    ///
    /// This is reserved for the native-grid coordinator after presence or
    /// absence has been observed. It deliberately does not perform optimistic
    /// toggling.
    #[cfg(feature = "native-sequence-adapter")]
    pub(crate) fn commit_step(&mut self, track: u8, step: u8, active: bool) -> bool {
        let track = usize::from(track);
        let step = usize::from(step);
        if track >= CELLS || step >= CELLS {
            return false;
        }
        let mask = 1_u16 << step;
        if active {
            self.patterns[track] |= mask;
        } else {
            self.patterns[track] &= !mask;
        }
        self.pending_repaint = true;
        true
    }

    /// Consume and clear a pending repaint request.
    pub fn take_repaint(&mut self) -> bool {
        core::mem::take(&mut self.pending_repaint)
    }

    /// Whether the UI has a repaint pending, without acknowledging it.
    #[must_use]
    pub const fn pending_repaint(&self) -> bool {
        self.pending_repaint
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
            self.open = !self.open;
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

    /// Apply a button without optimistically changing a native-backed cell.
    ///
    /// The existing [`Self::button`] remains the memory-only M0.3e behavior.
    /// This variant owns the same complete event pairs but returns one copied
    /// toggle intent for the Step-paint native coordinator.
    pub fn transaction_button(
        &mut self,
        button: Button,
        pressed: bool,
        shift: bool,
    ) -> (Outcome, Option<ToggleRequest>) {
        let owner = match button {
            Button::Data => DATA_OWNED,
            Button::Plus => PLUS_OWNED,
            Button::Other => return (Outcome::FORWARD, None),
        };

        if !pressed && self.owned_buttons & owner != 0 {
            self.owned_buttons &= !owner;
            return (Outcome::consumed(false, false), None);
        }
        if button == Button::Data && pressed && shift && self.owned_buttons & owner == 0 {
            self.owned_buttons |= owner;
            self.open = !self.open;
            self.pending_repaint = true;
            return (Outcome::consumed(true, !self.open), None);
        }
        if button == Button::Plus && pressed && self.open {
            let request = if self.owned_buttons & owner == 0 {
                self.owned_buttons |= owner;
                Some(ToggleRequest {
                    track: self.selected_track,
                    step: self.selected_step,
                })
            } else {
                None
            };
            return (Outcome::consumed(false, false), request);
        }
        if self.owned_buttons & owner != 0 {
            (Outcome::consumed(false, false), None)
        } else {
            (Outcome::FORWARD, None)
        }
    }

    #[cfg(feature = "native-playable-grid")]
    pub(crate) fn invalidate_native_grid(&mut self) {
        self.open = false;
        self.patterns = [0; CELLS];
        self.pending_repaint = false;
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

    /// Apply a zero-based physical-pad event. Invalid pads always forward.
    pub fn pad(&mut self, pad: i32, pressed: bool) -> Outcome {
        let Ok(pad) = u8::try_from(pad) else {
            return Outcome::FORWARD;
        };
        if usize::from(pad) >= CELLS {
            return Outcome::FORWARD;
        }
        let owner = 1_u16 << pad;
        if !pressed && self.owned_pads & owner != 0 {
            self.owned_pads &= !owner;
            return Outcome::consumed(false, false);
        }
        if pressed && self.open {
            if self.owned_pads & owner == 0 {
                self.owned_pads |= owner;
                self.selected_track = pad;
                self.pending_repaint = true;
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
        self.pending_repaint = true;
        Outcome {
            flow: Flow::Forward,
            repaint: true,
            restore: true,
        }
    }
}

#[cfg(all(test, feature = "native-playable-grid"))]
mod native_presentation_tests {
    use super::{Button, Flow, UiState};

    #[test]
    fn verified_step_context_opens_without_a_synthetic_chord() {
        let mut ui = UiState::default();
        assert!(!ui.is_open());
        ui.activate_native_grid();
        assert!(ui.is_open());
        assert!(ui.take_repaint());
        assert_eq!(ui.encoder(1).flow, Flow::Consume);
    }

    #[test]
    fn stock_view_toggle_preserves_committed_native_bits() {
        let mut ui = UiState::default();
        ui.activate_native_grid();
        assert!(ui.commit_step(0, 0, true));
        let (hidden, request) = ui.transaction_button(Button::Data, true, true);
        assert!(hidden.restore);
        assert!(request.is_none());
        assert!(!ui.is_open());
        let _ = ui.transaction_button(Button::Data, false, true);
        let (shown, request) = ui.transaction_button(Button::Data, true, true);
        assert!(!shown.restore);
        assert!(request.is_none());
        assert!(ui.is_open());
        assert!(ui.step_active(0, 0));
    }
}
