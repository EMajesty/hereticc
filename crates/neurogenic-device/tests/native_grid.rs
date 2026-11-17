#![cfg(feature = "native-sequence-adapter")]

use std::collections::VecDeque;

use neurogenic_device::native_grid::{
    GridEditError, GridEditPort, GridEditProgress, GridEditState, GridRequestError,
    NativeGridEditor,
};
use neurogenic_device::native_stock_sequence::{NativeEventPresence, NativeScanEvidence};
use neurogenic_device::reducer::{Button, UiState};
use neurogenic_device::stock_sequence::StockNoteRequest;

#[derive(Default)]
struct FakePort {
    next_receipt: u16,
    observations: VecDeque<NativeEventPresence>,
    inserts: Vec<StockNoteRequest>,
    removes: Vec<u16>,
    fail_insert: bool,
}

impl GridEditPort for FakePort {
    type Receipt = u16;
    type Error = &'static str;

    fn insert(&mut self, request: StockNoteRequest) -> Result<Self::Receipt, Self::Error> {
        if self.fail_insert {
            return Err("insert");
        }
        self.inserts.push(request);
        self.next_receipt += 1;
        Ok(self.next_receipt)
    }

    fn observe(
        &mut self,
        _: &mut Self::Receipt,
    ) -> Result<(NativeEventPresence, NativeScanEvidence), Self::Error> {
        Ok((
            self.observations
                .pop_front()
                .unwrap_or(NativeEventPresence::Absent),
            NativeScanEvidence::default(),
        ))
    }

    fn remove(&mut self, receipt: &Self::Receipt) -> Result<(), Self::Error> {
        self.removes.push(*receipt);
        Ok(())
    }
}

fn open_at(track: i32, step_delta: i32) -> UiState {
    let mut ui = UiState::default();
    ui.button(Button::Data, true, true);
    ui.button(Button::Data, false, true);
    ui.pad(track, true);
    ui.pad(track, false);
    if step_delta != 0 {
        ui.encoder(step_delta);
    }
    ui
}

#[test]
fn insert_uses_grid_timing_and_commits_only_after_unique_observation() {
    let mut ui = open_at(3, 4);
    let mut editor = NativeGridEditor::<u16>::default();
    let mut port = FakePort::default();
    port.observations.push_back(NativeEventPresence::Absent);
    port.observations.push_back(NativeEventPresence::Unique);

    editor.request_selected(&ui).unwrap();
    assert!(!ui.step_active(3, 4));
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::InsertCommandAccepted)
    );
    assert_eq!(port.inserts[0].physical_pad(), 3);
    assert_eq!(port.inserts[0].tick(), 960);
    assert_eq!(port.inserts[0].velocity(), 100);
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::Waiting)
    );
    assert!(!ui.step_active(3, 4));
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::InsertCommitted)
    );
    assert!(ui.step_active(3, 4));
    assert_eq!(editor.state(), GridEditState::Idle);
}

#[test]
fn removal_uses_owned_receipt_and_commits_only_after_absence() {
    let mut ui = open_at(0, 0);
    let mut editor = NativeGridEditor::<u16>::default();
    let mut port = FakePort::default();
    port.observations.extend([
        NativeEventPresence::Unique,
        NativeEventPresence::Unique,
        NativeEventPresence::Absent,
    ]);

    editor.request_selected(&ui).unwrap();
    editor.drive(&mut port, &mut ui).unwrap();
    editor.drive(&mut port, &mut ui).unwrap();
    assert!(ui.step_active(0, 0));

    editor.request_selected(&ui).unwrap();
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::RemoveCommandAccepted)
    );
    assert_eq!(port.removes, vec![1]);
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::Waiting)
    );
    assert!(ui.step_active(0, 0));
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Ok(GridEditProgress::RemoveCommitted)
    );
    assert!(!ui.step_active(0, 0));
}

#[test]
fn failures_and_ambiguity_are_terminal_without_optimistic_commit() {
    let mut ui = open_at(0, 0);
    let mut editor = NativeGridEditor::<u16>::default();
    let mut port = FakePort {
        fail_insert: true,
        ..FakePort::default()
    };
    editor.request_selected(&ui).unwrap();
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Err(GridEditError::Native("insert"))
    );
    assert_eq!(
        editor.state(),
        GridEditState::Failed(neurogenic_device::native_grid::GridCell {
            physical_pad: 0,
            step: 0,
        })
    );
    assert!(!ui.step_active(0, 0));
    assert_eq!(editor.request_selected(&ui), Err(GridRequestError::Busy));

    let mut ambiguous = NativeGridEditor::<u16>::default();
    let mut port = FakePort::default();
    port.observations.push_back(NativeEventPresence::Ambiguous);
    ambiguous.request_selected(&ui).unwrap();
    ambiguous.drive(&mut port, &mut ui).unwrap();
    assert_eq!(
        ambiguous.drive(&mut port, &mut ui),
        Err(GridEditError::AmbiguousEvent)
    );
    assert!(!ui.step_active(0, 0));
}

#[test]
fn active_cells_without_owned_receipts_are_never_removed() {
    let mut ui = open_at(0, 0);
    ui.button(Button::Plus, true, false);
    ui.button(Button::Plus, false, false);
    let mut editor = NativeGridEditor::<u16>::default();
    assert_eq!(
        editor.request_selected(&ui),
        Err(GridRequestError::UntrackedActiveCell)
    );
}

#[test]
fn missing_verification_paints_fail_after_a_bounded_observation_budget() {
    let mut ui = open_at(0, 0);
    let mut editor = NativeGridEditor::<u16>::default();
    let mut port = FakePort::default();

    editor.request_selected(&ui).unwrap();
    editor.drive(&mut port, &mut ui).unwrap();
    for _ in 0..7 {
        assert_eq!(
            editor.drive(&mut port, &mut ui),
            Ok(GridEditProgress::Waiting)
        );
    }
    assert_eq!(
        editor.drive(&mut port, &mut ui),
        Err(GridEditError::ObservationLimit)
    );
    assert!(!ui.step_active(0, 0));
}

#[test]
fn invalidation_discards_pending_work_and_owned_receipts() {
    let mut ui = open_at(0, 0);
    let mut editor = NativeGridEditor::<u16>::default();
    let mut port = FakePort::default();
    port.observations.push_back(NativeEventPresence::Unique);

    editor.request_selected(&ui).unwrap();
    editor.drive(&mut port, &mut ui).unwrap();
    editor.drive(&mut port, &mut ui).unwrap();
    assert!(ui.step_active(0, 0));

    editor.invalidate();
    assert_eq!(editor.state(), GridEditState::Idle);
    assert_eq!(
        editor.request_selected(&ui),
        Err(GridRequestError::UntrackedActiveCell)
    );
    assert_eq!(port.removes, Vec::<u16>::new());
}
