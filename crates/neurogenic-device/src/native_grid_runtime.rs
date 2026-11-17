//! Feature-gated bridge between pointer-free input intents and Step paint.

use std::sync::atomic::{AtomicBool, AtomicU8, AtomicU16, Ordering};
use std::sync::{Arc, Mutex, OnceLock, TryLockError};

use crate::NeurogenicDevice;
use crate::native_grid::{GridCell, GridEditProgress, GridEditState, NativeGridEditor};
use crate::native_stock_sequence::{
    NativeContextIdentity, NativeEditReceipt, NativeStockSequencePort,
};
use crate::reducer::UiState;

const NO_INTENT: u16 = u16::MAX;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum RuntimeStatus {
    Inactive,
    Ready,
    Pending,
    Failed,
    ContextChanged,
    Busy,
}

impl RuntimeStatus {
    const fn footer(self) -> &'static str {
        match self {
            Self::Inactive => "NATIVE: inactive",
            Self::Ready => "NATIVE: READY",
            Self::Pending => "NATIVE: pending; turn DATA if idle",
            Self::Failed => "NATIVE: FAILED (close and inspect stock)",
            Self::ContextChanged => "NATIVE: context changed; grid closed",
            Self::Busy => "NATIVE: edit already pending",
        }
    }
}

struct Runtime {
    ui: Arc<Mutex<UiState>>,
    overlay_open: Arc<AtomicBool>,
    editor: Mutex<NativeGridEditor<NativeEditReceipt>>,
    context: Mutex<Option<NativeContextIdentity>>,
    intent: AtomicU16,
    active: AtomicBool,
    status: AtomicU8,
}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

pub(crate) fn register(ui: Arc<Mutex<UiState>>, overlay_open: Arc<AtomicBool>) -> bool {
    RUNTIME
        .set(Runtime {
            ui,
            overlay_open,
            editor: Mutex::new(NativeGridEditor::default()),
            context: Mutex::new(None),
            intent: AtomicU16::new(NO_INTENT),
            active: AtomicBool::new(false),
            status: AtomicU8::new(RuntimeStatus::Inactive as u8),
        })
        .is_ok()
}

fn encode(cell: GridCell) -> u16 {
    (u16::from(cell.physical_pad) << 8) | u16::from(cell.step)
}

fn decode(value: u16) -> GridCell {
    GridCell {
        physical_pad: u8::try_from(value >> 8).unwrap_or(u8::MAX),
        step: u8::try_from(value & 0xff).unwrap_or(u8::MAX),
    }
}

pub(crate) fn request_toggle(track: u8, step: u8) -> bool {
    let Some(runtime) = RUNTIME.get() else {
        return false;
    };
    if !runtime.active.load(Ordering::Acquire) {
        runtime
            .status
            .store(RuntimeStatus::Inactive as u8, Ordering::Release);
        return false;
    }
    if runtime
        .status
        .compare_exchange(
            RuntimeStatus::Ready as u8,
            RuntimeStatus::Pending as u8,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
        .is_err()
    {
        return false;
    }
    if runtime
        .intent
        .compare_exchange(
            NO_INTENT,
            encode(GridCell {
                physical_pad: track,
                step,
            }),
            Ordering::AcqRel,
            Ordering::Acquire,
        )
        .is_err()
    {
        runtime
            .status
            .store(RuntimeStatus::Busy as u8, Ordering::Release);
        return false;
    }
    runtime
        .status
        .store(RuntimeStatus::Pending as u8, Ordering::Release);
    true
}

pub(crate) fn context_active() -> bool {
    RUNTIME
        .get()
        .is_some_and(|runtime| runtime.active.load(Ordering::Acquire))
}

pub(crate) fn deactivate() {
    let Some(runtime) = RUNTIME.get() else {
        return;
    };
    runtime.active.store(false, Ordering::Release);
    runtime.intent.store(NO_INTENT, Ordering::Release);
    runtime.overlay_open.store(false, Ordering::Release);
    runtime
        .status
        .store(RuntimeStatus::Inactive as u8, Ordering::Release);
    if let Ok(mut ui) = runtime.ui.try_lock() {
        ui.invalidate_native_grid();
    }
    if let Ok(mut editor) = runtime.editor.try_lock() {
        editor.invalidate();
    }
    if let Ok(mut context) = runtime.context.try_lock() {
        *context = None;
    }
}

fn status(runtime: &Runtime) -> RuntimeStatus {
    match runtime.status.load(Ordering::Acquire) {
        1 => RuntimeStatus::Ready,
        2 => RuntimeStatus::Pending,
        3 => RuntimeStatus::Failed,
        4 => RuntimeStatus::ContextChanged,
        5 => RuntimeStatus::Busy,
        _ => RuntimeStatus::Inactive,
    }
}

fn fail_context(
    runtime: &Runtime,
    ui: &mut UiState,
    editor: &mut NativeGridEditor<NativeEditReceipt>,
) {
    editor.invalidate();
    ui.invalidate_native_grid();
    runtime.intent.store(NO_INTENT, Ordering::Release);
    runtime.active.store(false, Ordering::Release);
    runtime.overlay_open.store(false, Ordering::Release);
    runtime
        .status
        .store(RuntimeStatus::ContextChanged as u8, Ordering::Release);
}

/// Run one bounded native-grid stage and draw from the live Step callback.
///
/// # Safety
///
/// `screen` and `graphics` must be the callback-local stock Step objects on
/// their owning UI thread. Neither value is retained.
pub(crate) unsafe fn on_step_paint(screen: usize, graphics: usize) {
    let Some(runtime) = RUNTIME.get() else {
        return;
    };
    runtime.active.store(true, Ordering::Release);

    let mut ui = match runtime.ui.try_lock() {
        Ok(ui) => ui,
        Err(TryLockError::WouldBlock | TryLockError::Poisoned(_)) => {
            runtime
                .status
                .store(RuntimeStatus::Failed as u8, Ordering::Release);
            return;
        }
    };
    let mut editor = match runtime.editor.try_lock() {
        Ok(editor) => editor,
        Err(TryLockError::WouldBlock | TryLockError::Poisoned(_)) => {
            runtime
                .status
                .store(RuntimeStatus::Failed as u8, Ordering::Release);
            return;
        }
    };
    let mut port = unsafe { NativeStockSequencePort::from_step_paint(screen) };
    let Ok(identity) = port.context_identity() else {
        fail_context(runtime, &mut ui, &mut editor);
        return;
    };
    let Ok(mut context) = runtime.context.try_lock() else {
        fail_context(runtime, &mut ui, &mut editor);
        return;
    };
    let first_context = context.is_none();
    if context.is_some_and(|known| known != identity) {
        *context = None;
        drop(context);
        fail_context(runtime, &mut ui, &mut editor);
        return;
    }
    *context = Some(identity);
    drop(context);
    if first_context {
        runtime
            .status
            .store(RuntimeStatus::Ready as u8, Ordering::Release);
    }

    let encoded = runtime.intent.swap(NO_INTENT, Ordering::AcqRel);
    if encoded != NO_INTENT && editor.request(&ui, decode(encoded)).is_err() {
        runtime
            .status
            .store(RuntimeStatus::Failed as u8, Ordering::Release);
    }

    if !matches!(
        editor.state(),
        GridEditState::Idle | GridEditState::Failed(_)
    ) {
        match editor.drive(&mut port, &mut ui) {
            Ok(GridEditProgress::InsertCommitted | GridEditProgress::RemoveCommitted) => runtime
                .status
                .store(RuntimeStatus::Ready as u8, Ordering::Release),
            Ok(_) => runtime
                .status
                .store(RuntimeStatus::Pending as u8, Ordering::Release),
            Err(_) => runtime
                .status
                .store(RuntimeStatus::Failed as u8, Ordering::Release),
        }
    }

    runtime.overlay_open.store(ui.is_open(), Ordering::Release);
    if ui.is_open() {
        let footer = status(runtime).footer();
        NeurogenicDevice::draw_native(&ui, mpc_sample_sdk::draw::Graphics::new(graphics), footer);
        let _ = ui.take_repaint();
    }

    if matches!(
        editor.state(),
        GridEditState::InsertRequested(_)
            | GridEditState::InsertAccepted(_)
            | GridEditState::RemoveRequested(_)
            | GridEditState::RemoveAccepted(_)
    ) {
        // SAFETY: request only the next verification paint while this callback
        // still owns the live Step component. No pointer is retained.
        unsafe { mpc_sample_sdk::draw::repaint_component(screen) };
    }
}

#[cfg(test)]
mod tests {
    use super::{GridCell, NO_INTENT, decode, encode};

    #[test]
    fn pointer_free_intent_round_trips_and_has_a_distinct_empty_value() {
        for track in 0..16 {
            for step in 0..16 {
                let cell = GridCell {
                    physical_pad: track,
                    step,
                };
                assert_ne!(encode(cell), NO_INTENT);
                assert_eq!(decode(encode(cell)), cell);
            }
        }
    }
}
