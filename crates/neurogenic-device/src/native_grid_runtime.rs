//! Feature-gated bridge between pointer-free input intents and Step paint.

use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU8, AtomicU16, AtomicU32, Ordering};
use std::sync::{Arc, Mutex, OnceLock, TryLockError};

use crate::NeurogenicDevice;
use crate::native_grid::{
    GridCell, GridEditPort, GridEditProgress, GridEditState, NativeGridEditor,
};
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
            Self::Failed => "NATIVE: FAILED (SHIFT+DATA: stock view)",
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
    hook_installed: AtomicBool,
    paints: AtomicU32,
    overlay_activations: AtomicU32,
    intents: AtomicU32,
    insertions: AtomicU32,
    observations: AtomicU32,
    removals: AtomicU32,
    invalidations: AtomicU32,
    renders: AtomicU32,
    inputs: AtomicU32,
    encoder_callbacks: AtomicU32,
    encoder_raw_sum: AtomicI32,
    encoder_movements: AtomicU32,
    repaint_requests: AtomicU32,
    presentation_switches: AtomicU32,
    commits: AtomicU32,
    reset_pending: AtomicBool,
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
            hook_installed: AtomicBool::new(false),
            paints: AtomicU32::new(0),
            overlay_activations: AtomicU32::new(0),
            intents: AtomicU32::new(0),
            insertions: AtomicU32::new(0),
            observations: AtomicU32::new(0),
            removals: AtomicU32::new(0),
            invalidations: AtomicU32::new(0),
            renders: AtomicU32::new(0),
            inputs: AtomicU32::new(0),
            encoder_callbacks: AtomicU32::new(0),
            encoder_raw_sum: AtomicI32::new(0),
            encoder_movements: AtomicU32::new(0),
            repaint_requests: AtomicU32::new(0),
            presentation_switches: AtomicU32::new(0),
            commits: AtomicU32::new(0),
            reset_pending: AtomicBool::new(false),
        })
        .is_ok()
}

fn increment(value: &AtomicU32) {
    let mut current = value.load(Ordering::Acquire);
    while current != u32::MAX {
        match value.compare_exchange_weak(current, current + 1, Ordering::AcqRel, Ordering::Acquire)
        {
            Ok(_) => break,
            Err(observed) => current = observed,
        }
    }
}

fn add_signed(value: &AtomicI32, delta: i32) {
    let mut current = value.load(Ordering::Acquire);
    loop {
        let next = current.saturating_add(delta);
        match value.compare_exchange_weak(current, next, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => break,
            Err(observed) => current = observed,
        }
    }
}

pub(crate) fn set_hook_installed(installed: bool) {
    if let Some(runtime) = RUNTIME.get() {
        runtime.hook_installed.store(installed, Ordering::Release);
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct DiagnosticSnapshot {
    pub hook: bool,
    pub paints: u32,
    pub open: bool,
    pub activations: u32,
    pub intents: u32,
    pub insertions: u32,
    pub observations: u32,
    pub removals: u32,
    pub invalidations: u32,
    pub renders: u32,
    pub inputs: u32,
    pub encoder_callbacks: u32,
    pub encoder_raw_sum: i32,
    pub encoder_movements: u32,
    pub repaint_requests: u32,
    pub presentation_switches: u32,
    pub commits: u32,
    pub context: bool,
    pub state: &'static str,
}

pub(crate) fn diagnostic_snapshot() -> DiagnosticSnapshot {
    let Some(runtime) = RUNTIME.get() else {
        return DiagnosticSnapshot {
            hook: false,
            paints: 0,
            open: false,
            activations: 0,
            intents: 0,
            insertions: 0,
            observations: 0,
            removals: 0,
            invalidations: 0,
            renders: 0,
            inputs: 0,
            encoder_callbacks: 0,
            encoder_raw_sum: 0,
            encoder_movements: 0,
            repaint_requests: 0,
            presentation_switches: 0,
            commits: 0,
            context: false,
            state: "unavailable",
        };
    };
    let state = match status(runtime) {
        RuntimeStatus::Inactive => "inactive",
        RuntimeStatus::Ready => "ready",
        RuntimeStatus::Pending => "pending",
        RuntimeStatus::Failed => "failed",
        RuntimeStatus::ContextChanged => "context_changed",
        RuntimeStatus::Busy => "busy",
    };
    DiagnosticSnapshot {
        hook: runtime.hook_installed.load(Ordering::Acquire),
        paints: runtime.paints.load(Ordering::Acquire),
        open: runtime.overlay_open.load(Ordering::Acquire),
        activations: runtime.overlay_activations.load(Ordering::Acquire),
        intents: runtime.intents.load(Ordering::Acquire),
        insertions: runtime.insertions.load(Ordering::Acquire),
        observations: runtime.observations.load(Ordering::Acquire),
        removals: runtime.removals.load(Ordering::Acquire),
        invalidations: runtime.invalidations.load(Ordering::Acquire),
        renders: runtime.renders.load(Ordering::Acquire),
        inputs: runtime.inputs.load(Ordering::Acquire),
        encoder_callbacks: runtime.encoder_callbacks.load(Ordering::Acquire),
        encoder_raw_sum: runtime.encoder_raw_sum.load(Ordering::Acquire),
        encoder_movements: runtime.encoder_movements.load(Ordering::Acquire),
        repaint_requests: runtime.repaint_requests.load(Ordering::Acquire),
        presentation_switches: runtime.presentation_switches.load(Ordering::Acquire),
        commits: runtime.commits.load(Ordering::Acquire),
        context: runtime
            .context
            .try_lock()
            .is_ok_and(|context| context.is_some()),
        state,
    }
}

pub(crate) fn note_input() {
    if let Some(runtime) = RUNTIME.get() {
        increment(&runtime.inputs);
    }
}

pub(crate) fn note_encoder(raw: i32, logical: i32) {
    if let Some(runtime) = RUNTIME.get() {
        increment(&runtime.encoder_callbacks);
        add_signed(&runtime.encoder_raw_sum, raw);
        if logical != 0 {
            increment(&runtime.encoder_movements);
        }
    }
}

pub(crate) fn note_presentation_switch() {
    if let Some(runtime) = RUNTIME.get() {
        increment(&runtime.presentation_switches);
    }
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
    increment(&runtime.intents);
    true
}

pub(crate) fn context_active() -> bool {
    RUNTIME.get().is_some_and(|runtime| {
        runtime.active.load(Ordering::Acquire) && !runtime.reset_pending.load(Ordering::Acquire)
    })
}

pub(crate) fn deactivate() {
    let Some(runtime) = RUNTIME.get() else {
        return;
    };
    runtime.active.store(false, Ordering::Release);
    runtime.reset_pending.store(true, Ordering::Release);
    runtime.intent.store(NO_INTENT, Ordering::Release);
    runtime.overlay_open.store(false, Ordering::Release);
    runtime
        .status
        .store(RuntimeStatus::Inactive as u8, Ordering::Release);
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
    increment(&runtime.invalidations);
    runtime.reset_pending.store(true, Ordering::Release);
    editor.invalidate();
    ui.invalidate_native_grid();
    runtime.intent.store(NO_INTENT, Ordering::Release);
    runtime.active.store(false, Ordering::Release);
    runtime.overlay_open.store(false, Ordering::Release);
    runtime
        .status
        .store(RuntimeStatus::ContextChanged as u8, Ordering::Release);
}

fn apply_pending_reset(
    runtime: &Runtime,
    ui: &mut UiState,
    editor: &mut NativeGridEditor<NativeEditReceipt>,
) -> bool {
    if !runtime.reset_pending.swap(false, Ordering::AcqRel) {
        return true;
    }
    ui.invalidate_native_grid();
    editor.invalidate();
    let Ok(mut context) = runtime.context.try_lock() else {
        runtime.reset_pending.store(true, Ordering::Release);
        return false;
    };
    *context = None;
    true
}

fn record_progress(runtime: &Runtime, progress: GridEditProgress) {
    match progress {
        GridEditProgress::InsertCommitted => {
            increment(&runtime.insertions);
            increment(&runtime.observations);
            increment(&runtime.commits);
            runtime
                .status
                .store(RuntimeStatus::Ready as u8, Ordering::Release);
        }
        GridEditProgress::RemoveCommitted => {
            increment(&runtime.removals);
            increment(&runtime.commits);
            runtime
                .status
                .store(RuntimeStatus::Ready as u8, Ordering::Release);
        }
        _ => runtime
            .status
            .store(RuntimeStatus::Pending as u8, Ordering::Release),
    }
}

fn advance_editor<P>(
    runtime: &Runtime,
    editor: &mut NativeGridEditor<NativeEditReceipt>,
    port: &mut P,
    ui: &mut UiState,
) where
    P: GridEditPort<Receipt = NativeEditReceipt>,
{
    let first = editor.drive(port, ui);
    match first {
        Ok(progress) => {
            record_progress(runtime, progress);
            // The native command service commits synchronously on the verified
            // Step UI thread. Verify once in the same callback so a committed
            // bit is drawn in this frame. If firmware has not published the
            // event yet, bounded later repaint observations remain in force.
            if matches!(
                progress,
                GridEditProgress::InsertCommandAccepted | GridEditProgress::RemoveCommandAccepted
            ) {
                match editor.drive(port, ui) {
                    Ok(progress) => record_progress(runtime, progress),
                    Err(_) => runtime
                        .status
                        .store(RuntimeStatus::Failed as u8, Ordering::Release),
                }
            }
        }
        Err(_) => runtime
            .status
            .store(RuntimeStatus::Failed as u8, Ordering::Release),
    }
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
    increment(&runtime.paints);
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
    if !apply_pending_reset(runtime, &mut ui, &mut editor) {
        return;
    }
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
    runtime.active.store(true, Ordering::Release);
    if first_context {
        ui.activate_native_grid();
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
        advance_editor(runtime, &mut editor, &mut port, &mut ui);
    }

    let was_open = runtime.overlay_open.swap(ui.is_open(), Ordering::AcqRel);
    if ui.is_open() && !was_open {
        increment(&runtime.overlay_activations);
    }
    if ui.is_open() {
        increment(&runtime.renders);
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
        increment(&runtime.repaint_requests);
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
