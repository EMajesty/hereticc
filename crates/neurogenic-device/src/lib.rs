//! First NEUROGENIC page implemented through `mpc-sample-sdk`.

pub mod reducer;

use mpc_sample_sdk::event::button;
use mpc_sample_sdk::{ButtonEvent, EncoderEvent, Flow, Mod, PadAction, PadEvent, PaintEvent};
use reducer::{Button, UiState};

/// Device mod state. It contains no retained firmware pointer.
#[derive(Default)]
pub struct NeurogenicDevice {
    ui: UiState,
}

impl NeurogenicDevice {
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

    fn close_if_page_gone(&mut self) {
        if !self.ui.is_open() {
            return;
        }
        let screen = mpc_sample_sdk::platform::page::screen();
        // SAFETY: button callbacks run on the UI path; the SDK recorded this
        // page and checks its vtable before reading visibility.
        if screen == 0 || !unsafe { mpc_sample_sdk::platform::page::visible(screen) } {
            let _ = self.ui.lose_page();
        }
    }

    fn draw(&self, graphics: mpc_sample_sdk::draw::Graphics) {
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
                self.ui.selected_track() + 1,
                self.ui.selected_step() + 1
            );
            graphics.colour(MUTED);
            graphics.text(&status, 10, 38, 300, 18, 12.0);

            for step in 0..reducer::CELLS {
                let x = 8 + i32::try_from(step).unwrap_or(0) * 19;
                let selected = step == usize::from(self.ui.selected_step());
                graphics.colour(if selected { CURSOR } else { PANEL });
                graphics.rect(x, 76, 17, 42);
                if self
                    .ui
                    .step_active(usize::from(self.ui.selected_track()), step)
                {
                    graphics.colour(ACTIVE);
                    graphics.rect(x + 4, 84, 9, 26);
                }
            }

            graphics.colour(MUTED);
            graphics.text("DATA: cursor   PADS: track", 10, 146, 300, 18, 11.0);
            graphics.text("PLUS: toggle   SHIFT+DATA: close", 10, 169, 300, 18, 11.0);
            graphics.colour(TEXT);
            graphics.text("Prototype: edits are memory-only", 10, 210, 300, 16, 10.0);
        }
    }
}

impl Mod for NeurogenicDevice {
    fn on_button(&mut self, event: ButtonEvent) -> Flow {
        self.close_if_page_gone();
        let mapped = match event.code {
            button::DATA => Button::Data,
            button::PLUS => Button::Plus,
            _ => Button::Other,
        };
        let outcome = self.ui.button(mapped, event.pressed, event.shift);
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_encoder(&mut self, event: EncoderEvent) -> Flow {
        self.close_if_page_gone();
        let outcome = self.ui.encoder(event.delta);
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_pad(&mut self, event: PadEvent) -> Flow {
        self.close_if_page_gone();
        let outcome = self.ui.pad(event.pad, event.action == PadAction::Press);
        if outcome.repaint {
            Self::repaint_live_sample_page();
        }
        Self::sdk_flow(outcome.flow)
    }

    fn on_paint(&mut self, graphics: &mut mpc_sample_sdk::draw::Graphics, event: PaintEvent) {
        if !event.sample_page {
            let _ = self.ui.lose_page();
            return;
        }
        let _ = self.ui.take_repaint();
        if self.ui.is_open() {
            self.draw(*graphics);
        }
    }
}

mpc_sample_sdk::export_mod!(NeurogenicDevice);
