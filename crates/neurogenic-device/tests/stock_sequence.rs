use neurogenic_core::GRID_STEP_TICKS;
use neurogenic_device::reducer::{Button, UiState};
use neurogenic_device::stock_sequence::{
    AttemptState, OneShotInsertion, RequestError, StockNoteRequest, StockSequenceEditPort,
    SubmitError,
};

#[test]
fn active_ui_selection_becomes_a_pointer_free_request() {
    let mut ui = UiState::default();
    assert_eq!(
        StockNoteRequest::from_active_selection(&ui, 100),
        Err(RequestError::EditorClosed)
    );
    ui.button(Button::Data, true, true);
    ui.button(Button::Data, false, true);
    assert_eq!(
        StockNoteRequest::from_active_selection(&ui, 100),
        Err(RequestError::StepInactive)
    );
    ui.pad(2, true);
    ui.pad(2, false);
    ui.encoder(6);
    ui.button(Button::Plus, true, false);
    ui.button(Button::Plus, false, false);

    let request = StockNoteRequest::from_active_selection(&ui, 96).expect("active selection");
    assert_eq!(request.physical_pad(), 2);
    assert_eq!(request.tick(), 6 * 240);
    assert_eq!(request.velocity(), 96);
}

#[test]
fn grid_step_maps_to_source_confirmed_constructor_arguments() {
    let request = StockNoteRequest::from_grid_step(3, 5, 100).expect("request");
    assert_eq!(request.physical_pad(), 3);
    assert_eq!(request.tick(), i64::try_from(GRID_STEP_TICKS * 5).unwrap());
    assert_eq!(request.duration(), i64::try_from(GRID_STEP_TICKS).unwrap());
    assert_eq!(request.velocity(), 100);

    let args = request.resolve(39, 16 * 240).expect("resolved");
    assert_eq!(args.midi_note, 39);
    assert_eq!(args.tick, 5 * 240);
    assert_eq!(args.duration, 240);
    assert_eq!(args.default_value, 100);
    assert_eq!(args.flags, 0);
    assert!((args.velocity - 100.0 / 127.0).abs() < f32::EPSILON);
}

#[test]
fn all_prototype_steps_map_without_drift() {
    for step in 0_u8..16 {
        let request = StockNoteRequest::from_grid_step(0, step, 64).expect("request");
        assert_eq!(request.tick(), i64::from(step) * 240);
        assert_eq!(request.duration(), 240);
    }
}

#[test]
fn invalid_pad_step_velocity_and_sequence_bounds_fail_before_adapter() {
    assert_eq!(
        StockNoteRequest::from_grid_step(16, 0, 100),
        Err(RequestError::PadOutOfRange)
    );
    assert_eq!(
        StockNoteRequest::from_grid_step(0, 16, 100),
        Err(RequestError::StepOutOfRange)
    );
    assert_eq!(
        StockNoteRequest::from_grid_step(0, 0, 0),
        Err(RequestError::VelocityOutOfRange)
    );
    let request = StockNoteRequest::from_grid_step(0, 15, 127).expect("request");
    assert_eq!(
        request.resolve(36, 0),
        Err(RequestError::InvalidSequenceLength)
    );
    assert_eq!(
        request.resolve(36, 15 * 240),
        Err(RequestError::OutsideSequence)
    );
}

#[derive(Default)]
struct FakePort {
    calls: usize,
    fail: bool,
}

impl StockSequenceEditPort for FakePort {
    type Receipt = u32;
    type Error = &'static str;

    fn insert_current_track(
        &mut self,
        _request: StockNoteRequest,
    ) -> Result<Self::Receipt, Self::Error> {
        self.calls += 1;
        if self.fail {
            Err("native command failed")
        } else {
            Ok(7)
        }
    }
}

#[test]
fn insertion_is_at_most_once_after_success_failure_or_cancellation() {
    let request = StockNoteRequest::from_grid_step(0, 0, 100).expect("request");

    let mut success = OneShotInsertion::new(request);
    let mut port = FakePort::default();
    assert_eq!(success.submit(&mut port), Ok(7));
    assert_eq!(success.state(), AttemptState::Submitted);
    assert_eq!(
        success.submit(&mut port),
        Err(SubmitError::Terminal(AttemptState::Submitted))
    );
    assert_eq!(port.calls, 1);

    let mut failed = OneShotInsertion::new(request);
    let mut failing = FakePort {
        calls: 0,
        fail: true,
    };
    assert_eq!(
        failed.submit(&mut failing),
        Err(SubmitError::Adapter("native command failed"))
    );
    assert_eq!(failed.state(), AttemptState::Failed);
    assert_eq!(
        failed.submit(&mut failing),
        Err(SubmitError::Terminal(AttemptState::Failed))
    );
    assert_eq!(failing.calls, 1);

    let mut cancelled = OneShotInsertion::new(request);
    assert!(cancelled.cancel());
    assert!(!cancelled.cancel());
    assert_eq!(
        cancelled.submit(&mut port),
        Err(SubmitError::Terminal(AttemptState::Cancelled))
    );
    assert_eq!(port.calls, 1);
}
