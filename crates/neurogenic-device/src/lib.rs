//! Stable NEUROGENIC editor page implemented through `mpc-sample-sdk`.

#[cfg(feature = "hil-test-interface")]
pub mod hil;
#[cfg(feature = "native-sequence-adapter")]
pub mod native_grid;
#[cfg(feature = "native-sequence-adapter")]
pub mod native_sequence_experiment;
pub mod native_stock_sequence;
pub mod reducer;
pub mod stock_sequence;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, TryLockError};

use mpc_sample_sdk::event::button;
use mpc_sample_sdk::{ButtonEvent, EncoderEvent, Flow, Mod, PadAction, PadEvent, PaintEvent};
use reducer::{Button, UiState};

/// Device mod state. It contains no retained firmware pointer.
pub struct NeurogenicDevice {
    ui: Arc<Mutex<UiState>>,
    overlay_open: Arc<AtomicBool>,
    #[cfg(feature = "hil-test-interface")]
    hil_page_owned: Arc<AtomicBool>,
}

impl Default for NeurogenicDevice {
    fn default() -> Self {
        Self {
            ui: Arc::new(Mutex::new(UiState::default())),
            overlay_open: Arc::new(AtomicBool::new(false)),
            #[cfg(feature = "hil-test-interface")]
            hil_page_owned: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl NeurogenicDevice {
    fn with_ui<R>(&self, apply: impl FnOnce(&mut UiState) -> R) -> Option<R> {
        let mut ui = match self.ui.try_lock() {
            Ok(ui) => ui,
            Err(TryLockError::WouldBlock) => return None,
            Err(TryLockError::Poisoned(error)) => error.into_inner(),
        };
        let result = apply(&mut ui);
        self.overlay_open.store(ui.is_open(), Ordering::Release);
        Some(result)
    }

    fn contention_button_flow(&self, button: Button, pressed: bool, shift: bool) -> Flow {
        let open = self.overlay_open.load(Ordering::Acquire);
        let owned = match button {
            Button::Plus => open,
            Button::Data => shift || (open && !pressed),
            Button::Other => false,
        };
        if owned { Flow::Consume } else { Flow::Forward }
    }

    fn contention_encoder_flow(&self, delta: i32) -> Flow {
        if self.overlay_open.load(Ordering::Acquire) && delta != 0 {
            Flow::Consume
        } else {
            Flow::Forward
        }
    }

    fn contention_pad_flow(&self, pad: i32) -> Flow {
        if self.overlay_open.load(Ordering::Acquire)
            && usize::try_from(pad).is_ok_and(|pad| pad < reducer::CELLS)
        {
            Flow::Consume
        } else {
            Flow::Forward
        }
    }

    fn sdk_flow(flow: reducer::Flow) -> Flow {
        match flow {
            reducer::Flow::Forward => Flow::Forward,
            reducer::Flow::Consume => Flow::Consume,
        }
    }

    fn repaint_live_sample_page() {
        let screen = mpc_sample_sdk::platform::page::screen();
        if screen == 0 {
            return;
        }
        // SAFETY: called only from a UI callback. `screen` and the last painted
        // component are SDK-recorded objects for the currently visible page.
        if unsafe { mpc_sample_sdk::platform::page::visible(screen) } {
            // SAFETY: the visible Sample page still owns its painted component.
            unsafe { mpc_sample_sdk::draw::repaint_painted() };
        }
    }

    fn close_if_page_gone(&self) {
        if !self.with_ui(|ui| ui.is_open()).unwrap_or(false) {
            return;
        }
        let screen = mpc_sample_sdk::platform::page::screen();
        // SAFETY: callbacks run on the UI path; the SDK recorded this page and
        // checks its vtable before reading visibility.
        if screen == 0 || !unsafe { mpc_sample_sdk::platform::page::visible(screen) } {
            self.observe_page_loss();
        }
    }

    fn observe_page_loss(&self) {
        // A Level B OPEN deliberately exercises only the loaded reducer; it
        // does not claim to install a real SDK Sample-page context. Keep that
        // bounded test session independent from firmware paint/input traffic.
        // Production builds do not contain this exception.
        #[cfg(feature = "hil-test-interface")]
        if self.hil_page_owned.load(Ordering::Acquire) {
            return;
        }
        let _ = self.with_ui(UiState::lose_page);
    }

    fn draw(ui: &UiState, graphics: mpc_sample_sdk::draw::Graphics) {
        const BG: u32 = 0xff0c_1018;
        const PANEL: u32 = 0xff1a_2230;
        const TEXT: u32 = 0xfff2_f5f8;
        const MUTED: u32 = 0xff8d_9aaa;
        const ACCENT: u32 = 0xff35_e0a1;
        const ACTIVE: u32 = 0xfff0_a340;
        const CURSOR: u32 = 0xff57_8cff;

        // SAFETY: `graphics` exists only for the current SDK paint callback.
        unsafe {
            graphics.colour(BG);
            graphics.rect(0, 0, 320, 240);
            graphics.colour(ACCENT);
            graphics.rect(0, 0, 320, 4);
            graphics.colour(TEXT);
            graphics.text("NEUROGENIC", 10, 10, 190, 22, 18.0);

            let status = format!(
                "TRACK {:02}   STEP {:02}",
                ui.selected_track() + 1,
                ui.selected_step() + 1
            );
            graphics.colour(MUTED);
            graphics.text(&status, 10, 38, 300, 18, 12.0);

            for step in 0..reducer::CELLS {
                let x = 8 + i32::try_from(step).unwrap_or(0) * 19;
                let selected = step == usize::from(ui.selected_step());
                graphics.colour(if selected { CURSOR } else { PANEL });
                graphics.rect(x, 76, 17, 42);
                if ui.step_active(usize::from(ui.selected_track()), step) {
                    graphics.colour(ACTIVE);
                    graphics.rect(x + 4, 84, 9, 26);
                }
            }

            graphics.colour(MUTED);
            graphics.text("DATA: cursor   PADS: track", 10, 146, 300, 18, 11.0);
            graphics.text("PLUS: toggle   SHIFT+DATA: close", 10, 169, 300, 18, 11.0);
            graphics.text(
                "16 physical pads; banks remain stock-owned",
                10,
                190,
                300,
                16,
                10.0,
            );
            graphics.colour(TEXT);
            graphics.text("Prototype: edits are memory-only", 10, 210, 300, 16, 10.0);
        }
    }
}

impl Mod for NeurogenicDevice {
    fn on_start(&mut self) {
        #[cfg(feature = "native-sequence-adapter")]
        let _ = native_sequence_experiment::install();
        #[cfg(feature = "hil-test-interface")]
        hil::start_bridge(
            Arc::clone(&self.ui),
            Arc::clone(&self.overlay_open),
            Arc::clone(&self.hil_page_owned),
        );
    }

    fn on_button(&mut self, event: ButtonEvent) -> Flow {
        self.close_if_page_gone();
        let mapped = match event.code {
            button::DATA => Button::Data,
            button::PLUS => Button::Plus,
            _ => Button::Other,
        };
        let Some(outcome) = self.with_ui(|ui| ui.button(mapped, event.pressed, event.shift)) else {
            return self.contention_button_flow(mapped, event.pressed, event.shift);
        };
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_encoder(&mut self, event: EncoderEvent) -> Flow {
        self.close_if_page_gone();
        let Some(outcome) = self.with_ui(|ui| ui.encoder(event.delta)) else {
            return self.contention_encoder_flow(event.delta);
        };
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_pad(&mut self, event: PadEvent) -> Flow {
        self.close_if_page_gone();
        let Some(outcome) = self.with_ui(|ui| ui.pad(event.pad, event.action == PadAction::Press))
        else {
            return self.contention_pad_flow(event.pad);
        };
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_paint(&mut self, graphics: &mut mpc_sample_sdk::draw::Graphics, event: PaintEvent) {
        if !event.sample_page {
            self.observe_page_loss();
            return;
        }
        let _ = self.with_ui(|ui| {
            let _ = ui.take_repaint();
            if ui.is_open() {
                Self::draw(ui, *graphics);
            }
        });
    }
}

#[cfg(test)]
mod contention_tests {
    use super::*;

    #[test]
    fn contention_never_forwards_controls_owned_by_an_open_overlay() {
        let device = NeurogenicDevice::default();
        device.overlay_open.store(true, Ordering::Release);
        let guard = device.ui.lock().expect("fixture lock");
        assert!(device.with_ui(|_| ()).is_none());
        assert_eq!(device.contention_pad_flow(0), Flow::Consume);
        assert_eq!(device.contention_encoder_flow(1), Flow::Consume);
        assert_eq!(
            device.contention_button_flow(Button::Plus, true, false),
            Flow::Consume
        );
        assert_eq!(
            device.contention_button_flow(Button::Plus, false, false),
            Flow::Consume
        );
        assert_eq!(
            device.contention_button_flow(Button::Data, false, false),
            Flow::Consume
        );
        assert_eq!(
            device.contention_button_flow(Button::Other, true, false),
            Flow::Forward
        );
        drop(guard);
    }

    #[test]
    fn closed_overlay_only_fail_closes_the_toggle_chord() {
        let device = NeurogenicDevice::default();
        assert_eq!(
            device.contention_button_flow(Button::Data, true, true),
            Flow::Consume
        );
        assert_eq!(
            device.contention_button_flow(Button::Data, true, false),
            Flow::Forward
        );
        assert_eq!(
            device.contention_button_flow(Button::Plus, true, false),
            Flow::Forward
        );
        assert_eq!(device.contention_pad_flow(0), Flow::Forward);
    }
}

#[cfg(all(test, feature = "hil-test-interface"))]
mod hil_page_context_tests {
    use super::*;

    #[test]
    fn synthetic_open_survives_background_page_loss_until_close() {
        let device = NeurogenicDevice::default();
        {
            let mut ui = device.ui.lock().expect("state");
            assert!(
                hil::apply(&mut ui, b"OPEN")
                    .expect("open")
                    .as_str()
                    .contains("open=1")
            );
        }
        device.overlay_open.store(true, Ordering::Release);
        device.hil_page_owned.store(true, Ordering::Release);

        // Models either asynchronous closer: a non-Sample paint or a
        // visibility check made by an SDK input callback.
        device.observe_page_loss();
        {
            let mut ui = device.ui.lock().expect("state");
            assert!(
                hil::apply(&mut ui, b"SELECT 4")
                    .expect("select")
                    .as_str()
                    .contains("track=4")
            );
            assert!(
                hil::apply(&mut ui, b"MOVE 3")
                    .expect("move")
                    .as_str()
                    .contains("step=4")
            );
            assert!(
                hil::apply(&mut ui, b"TOGGLE")
                    .expect("toggle")
                    .as_str()
                    .contains("bits=0008")
            );
            assert!(ui.is_open());
        }

        device.hil_page_owned.store(false, Ordering::Release);
        device.observe_page_loss();
        assert!(!device.ui.lock().expect("state").is_open());
    }

    #[test]
    fn hil_page_context_does_not_claim_unowned_physical_controls() {
        let device = NeurogenicDevice::default();
        device.hil_page_owned.store(true, Ordering::Release);
        assert_eq!(
            device.contention_button_flow(Button::Other, true, false),
            Flow::Forward
        );
        assert_eq!(device.contention_pad_flow(0), Flow::Forward);
    }
}

#[cfg(feature = "device-export")]
mpc_sample_sdk::export_mod!(NeurogenicDevice);
