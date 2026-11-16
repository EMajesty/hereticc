use neurogenic_device::reducer::{Button, Flow, PadStatus, UiState};

fn open(ui: &mut UiState) {
    let result = ui.button(Button::Data, true, true);
    assert_eq!(result.flow, Flow::Consume);
    assert!(ui.is_open());
    assert_eq!(ui.button(Button::Data, false, false).flow, Flow::Consume);
}

fn pad(
    ui: &mut UiState,
    number: i32,
    pressed: bool,
    available: bool,
) -> neurogenic_device::reducer::Outcome {
    // Physical evidence from MINIMOD records source 0 on press and source 1
    // on release for the same input object. The reducer must pair by input
    // when the source changes across the edge pair.
    let source = i32::from(!pressed);
    ui.pad(number, pressed, available, 0x1000, source)
}

#[test]
fn opens_closes_and_reopens_without_losing_memory_pattern() {
    let mut ui = UiState::default();
    open(&mut ui);
    ui.button(Button::Plus, true, false);
    ui.button(Button::Plus, false, false);
    assert!(ui.step_active(0, 0));

    let close = ui.button(Button::Data, true, true);
    assert!(close.restore);
    assert!(!ui.is_open());
    assert_eq!(ui.button(Button::Data, false, false).flow, Flow::Consume);
    open(&mut ui);
    assert!(ui.step_active(0, 0));
}

#[test]
fn encoder_wraps_in_both_directions_and_uses_full_delta() {
    let mut ui = UiState::default();
    assert_eq!(ui.encoder(1).flow, Flow::Forward);
    open(&mut ui);
    ui.encoder(-1);
    assert_eq!(ui.selected_step(), 15);
    ui.encoder(18);
    assert_eq!(ui.selected_step(), 1);
    assert_eq!(ui.encoder(0).flow, Flow::Forward);
}

#[test]
fn pads_select_tracks_and_preserve_consumed_pairs() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(pad(&mut ui, 7, true, true).flow, Flow::Forward);
    assert_eq!(ui.selected_track(), 7);
    assert_eq!(pad(&mut ui, 7, true, true).flow, Flow::Consume);
    assert_eq!(pad(&mut ui, 7, false, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 7, false, true).flow, Flow::Consume);
}

#[test]
fn plus_toggles_once_per_press_and_release_pair() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.button(Button::Plus, true, false).flow, Flow::Consume);
    assert!(ui.step_active(0, 0));
    ui.button(Button::Plus, true, false);
    assert!(ui.step_active(0, 0));
    assert_eq!(ui.button(Button::Plus, false, false).flow, Flow::Consume);
    ui.button(Button::Plus, true, false);
    assert!(!ui.step_active(0, 0));
}

#[test]
fn unowned_and_invalid_inputs_always_forward() {
    let mut ui = UiState::default();
    assert_eq!(ui.button(Button::Other, true, true).flow, Flow::Forward);
    assert_eq!(ui.button(Button::Plus, true, false).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, -1, true, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 16, true, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 0, false, true).flow, Flow::Forward);
}

#[test]
fn page_loss_closes_and_drains_owned_releases() {
    let mut ui = UiState::default();
    open(&mut ui);
    pad(&mut ui, 3, true, true);
    ui.button(Button::Plus, true, false);
    let lost = ui.lose_page();
    assert!(lost.restore);
    assert!(!ui.is_open());
    assert_eq!(pad(&mut ui, 3, false, true).flow, Flow::Forward);
    assert_eq!(ui.button(Button::Plus, false, false).flow, Flow::Consume);
    assert_eq!(pad(&mut ui, 4, true, true).flow, Flow::Forward);
    assert_eq!(ui.encoder(1).flow, Flow::Forward);
}

#[test]
fn audition_is_one_voice_and_retriggers_only_after_release() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(pad(&mut ui, 2, true, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 2, true, true).flow, Flow::Consume);
    assert_eq!(pad(&mut ui, 5, true, true).flow, Flow::Consume);
    assert_eq!(pad(&mut ui, 5, false, true).flow, Flow::Consume);
    assert_eq!(ui.selected_track(), 2);
    assert_eq!(pad(&mut ui, 2, false, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 2, false, true).flow, Flow::Consume);
    assert_eq!(pad(&mut ui, 5, true, true).flow, Flow::Forward);
}

#[test]
fn suppressed_pad_release_does_not_disturb_active_pad() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(2, true, true, 0x1000, 0).flow, Flow::Forward);
    assert_eq!(ui.pad(5, true, true, 0x2000, 2).flow, Flow::Consume);
    assert_eq!(ui.pad(5, false, true, 0x2000, 3).flow, Flow::Consume);
    assert_eq!(ui.active_audition(), Some(2));
    assert_eq!(ui.pad(2, false, true, 0x1000, 1).flow, Flow::Forward);
    assert_eq!(ui.active_audition(), None);
}

#[test]
fn release_prefers_exact_source_then_falls_back_to_input_identity() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(1, true, true, 0x1000, 7).flow, Flow::Forward);
    assert_eq!(ui.pad(1, true, true, 0x1000, 8).flow, Flow::Consume);

    // An exact source match drains only the suppressed repeated press.
    assert_eq!(ui.pad(1, false, true, 0x1000, 8).flow, Flow::Consume);
    assert_eq!(ui.active_audition(), Some(1));
    // The physically observed source mismatch still pairs by input.
    assert_eq!(ui.pad(1, false, true, 0x1000, 9).flow, Flow::Forward);
    assert_eq!(ui.active_audition(), None);
}

#[test]
fn bank_button_forwarding_does_not_change_held_pad_identity() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(9, true, true, 0x1000, 0).flow, Flow::Forward);
    assert_eq!(ui.button(Button::Other, true, false).flow, Flow::Forward);
    assert_eq!(ui.button(Button::Other, false, false).flow, Flow::Forward);
    assert_eq!(ui.pad(9, false, true, 0x1000, 1).flow, Flow::Forward);
    assert_eq!(ui.selected_track(), 9);
}

#[test]
fn unmatched_release_cannot_clear_or_close_an_active_pad() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(4, true, true, 0x1000, 0).flow, Flow::Forward);
    ui.button(Button::Data, true, true);
    assert!(ui.close_pending());
    assert_eq!(ui.pad(4, false, true, 0x2000, 1).flow, Flow::Consume);
    assert!(ui.is_open());
    assert_eq!(ui.active_audition(), Some(4));
    let release = ui.pad(4, false, true, 0x1000, 1);
    assert_eq!(release.flow, Flow::Forward);
    assert!(release.restore);
    assert!(!ui.is_open());
}

#[test]
fn forwarded_pad_uses_stock_repaint_while_consumed_status_requests_one() {
    let mut ui = UiState::default();
    open(&mut ui);
    let forwarded = ui.pad(0, true, true, 0x1000, 0);
    assert_eq!(forwarded.flow, Flow::Forward);
    assert!(!forwarded.repaint);
    assert_eq!(ui.pad(0, false, true, 0x1000, 1).flow, Flow::Forward);

    let unavailable = ui.pad(1, true, false, 0x1000, 0);
    assert_eq!(unavailable.flow, Flow::Consume);
    assert!(unavailable.repaint);
}

#[test]
fn close_waits_for_note_off_and_then_restores_stock_page() {
    let mut ui = UiState::default();
    open(&mut ui);
    pad(&mut ui, 4, true, true);
    let deferred = ui.button(Button::Data, true, true);
    assert!(ui.is_open());
    assert!(ui.close_pending());
    assert!(!deferred.restore);
    ui.button(Button::Data, false, false);
    let released = pad(&mut ui, 4, false, true);
    assert_eq!(released.flow, Flow::Forward);
    assert!(released.restore);
    assert!(!ui.is_open());
}

#[test]
fn dispatch_unavailable_still_selects_and_owns_the_pad_pair() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(pad(&mut ui, 1, true, false).flow, Flow::Consume);
    assert_eq!(ui.selected_track(), 1);
    assert_eq!(ui.active_audition(), None);
    assert_eq!(ui.pad_status(), PadStatus::DispatchUnavailable);
    assert_eq!(pad(&mut ui, 1, false, false).flow, Flow::Consume);
}

#[test]
fn pad_capability_display_state_is_bounded_and_explicit() {
    let mut ui = UiState::default();
    assert_eq!(ui.pad_capabilities(), (false, false, false));
    ui.record_pad_callback(false, true, true);
    assert_eq!(ui.pad_capabilities(), (false, true, true));
    ui.record_pad_callback(true, true, false);
    assert_eq!(ui.pad_capabilities(), (true, true, false));
}

#[test]
fn release_forwards_matching_stock_note_off_even_if_capability_changes() {
    let mut ui = UiState::default();
    open(&mut ui);
    pad(&mut ui, 6, true, true);
    let release = pad(&mut ui, 6, false, false);
    assert_eq!(release.flow, Flow::Forward);
    assert_eq!(ui.active_audition(), None);
}

#[test]
fn null_properties_do_not_block_supported_native_forwarding() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad_status(), PadStatus::NoCallback);
    ui.record_pad_callback(true, true, false);
    assert_eq!(pad(&mut ui, 6, true, true).flow, Flow::Forward);
    assert_eq!(ui.active_audition(), Some(6));
    assert_eq!(ui.pad_status(), PadStatus::NativeForwardRequested);
    assert_eq!(pad(&mut ui, 6, false, true).flow, Flow::Forward);
}

#[test]
fn closed_overlay_forwards_even_when_native_dispatch_is_ready() {
    let mut ui = UiState::default();
    assert_eq!(pad(&mut ui, 3, true, true).flow, Flow::Forward);
    assert_eq!(pad(&mut ui, 3, false, true).flow, Flow::Forward);
    assert_eq!(ui.pad_status(), PadStatus::NoCallback);
}

#[test]
fn pending_repaint_is_bounded_and_edge_triggered() {
    let mut ui = UiState::default();
    assert!(!ui.take_repaint());
    open(&mut ui);
    assert!(ui.take_repaint());
    assert!(!ui.take_repaint());
    ui.encoder(1);
    ui.encoder(1);
    assert!(ui.take_repaint());
    assert!(!ui.take_repaint());
}
