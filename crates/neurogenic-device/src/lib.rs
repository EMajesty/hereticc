//! First NEUROGENIC page implemented through `mpc-sample-sdk`.

pub mod reducer;

use mpc_sample_sdk::event::button;
use mpc_sample_sdk::{ButtonEvent, EncoderEvent, Flow, Mod, PadAction, PadEvent, PaintEvent};
use reducer::{Button, PadStatus, Playback, UiState};

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
                "TRACK/PAD {:02}   STEP {:02}",
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
            let note = if let Some(pad) = self.ui.active_audition() {
                format!("PAD {:02}: NOTE ON - RELEASE TO STOP", pad + 1)
            } else {
                format!("PAD {:02}: ready to audition", self.ui.selected_track() + 1)
            };
            graphics.text(&note, 10, 132, 300, 18, 11.0);
            let pad_status = match self.ui.pad_status() {
                PadStatus::NoCallback => "PAD CB: none",
                PadStatus::DispatchUnavailable => "PAD CB: owned; native unavailable",
                PadStatus::NativeAttempted => "PAD CB: native dispatch attempted",
            };
            let (hooks, input, properties) = self.ui.pad_capabilities();
            let capability = format!(
                "{}  H{} I{} P{}",
                pad_status,
                u8::from(hooks),
                u8::from(input),
                u8::from(properties)
            );
            graphics.text(&capability, 10, 190, 300, 16, 10.0);
            graphics.text(
                "DATA: cursor   PADS: select + audition",
                10,
                154,
                300,
                18,
                11.0,
            );
            graphics.text("PLUS: toggle   SHIFT+DATA: close", 10, 169, 300, 18, 11.0);
            graphics.colour(TEXT);
            graphics.text(
                if self.ui.close_pending() {
                    "Release held pad to close safely"
                } else {
                    "One audition voice; edits are memory-only"
                },
                10,
                210,
                300,
                16,
                10.0,
            );
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
        let pressed = event.action == PadAction::Press;
        let notes_ready = mpc_sample_sdk::midi::notes_ready();
        if self.ui.is_open() {
            self.ui
                .record_pad_callback(notes_ready, event.input != 0, event.properties != 0);
        }
        // `notes_ready` means both displaced note sites exist. The press
        // callback alone proves only note-on is installed; paired manual
        // dispatch must not start unless note-off is available too.
        let native_dispatch_available =
            event.input != 0 && (!pressed || event.properties != 0) && notes_ready;
        let outcome = self.ui.pad(event.pad, pressed, native_dispatch_available);
        match outcome.playback {
            Playback::None => {}
            Playback::NoteOn(pad) => {
                // SAFETY: all firmware objects come from this live pad
                // callback, on its owning UI path. Only the numeric pad is
                // retained by the reducer.
                let sent = unsafe {
                    mpc_sample_sdk::midi::send_note_on(
                        event.input,
                        event.source,
                        i32::from(pad),
                        event.properties,
                        event.velocity,
                    )
                };
                if !sent {
                    self.ui.note_on_failed(pad);
                }
            }
            Playback::NoteOff(pad) => {
                // SAFETY: `input` is callback-local and pad/source match the
                // consumed note-on pair.
                unsafe {
                    mpc_sample_sdk::midi::send_note_off(event.input, event.source, i32::from(pad));
                }
            }
        }
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
