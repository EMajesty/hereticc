use neurogenic_device::reducer::{Button, Flow, Playback, UiState};

fn open(ui: &mut UiState) {
    let result = ui.button(Button::Data, true, true);
    assert_eq!(result.flow, Flow::Consume);
    assert!(ui.is_open());
    assert_eq!(ui.button(Button::Data, false, false).flow, Flow::Consume);
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
    assert_eq!(ui.pad(7, true, true).playback, Playback::NoteOn(7));
    assert_eq!(ui.selected_track(), 7);
    assert_eq!(ui.pad(7, true, true).playback, Playback::None);
    assert_eq!(ui.pad(7, false, true).playback, Playback::NoteOff(7));
    assert_eq!(ui.pad(7, false, true).flow, Flow::Forward);
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
    assert_eq!(ui.pad(-1, true, true).flow, Flow::Forward);
    assert_eq!(ui.pad(16, true, true).flow, Flow::Forward);
    assert_eq!(ui.pad(0, false, true).flow, Flow::Forward);
}

#[test]
fn page_loss_closes_and_drains_owned_releases() {
    let mut ui = UiState::default();
    open(&mut ui);
    ui.pad(3, true, true);
    ui.button(Button::Plus, true, false);
    let lost = ui.lose_page();
    assert!(lost.restore);
    assert!(!ui.is_open());
    assert_eq!(ui.pad(3, false, true).playback, Playback::NoteOff(3));
    assert_eq!(ui.button(Button::Plus, false, false).flow, Flow::Consume);
    assert_eq!(ui.pad(4, true, true).flow, Flow::Forward);
    assert_eq!(ui.encoder(1).flow, Flow::Forward);
}

#[test]
fn audition_is_one_voice_and_retriggers_only_after_release() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(2, true, true).playback, Playback::NoteOn(2));
    assert_eq!(ui.pad(2, true, true).playback, Playback::None);
    assert_eq!(ui.pad(5, true, true).flow, Flow::Consume);
    assert_eq!(ui.pad(5, false, true).flow, Flow::Consume);
    assert_eq!(ui.selected_track(), 2);
    assert_eq!(ui.pad(2, false, true).playback, Playback::NoteOff(2));
    assert_eq!(ui.pad(5, true, true).playback, Playback::NoteOn(5));
}

#[test]
fn close_waits_for_note_off_and_then_restores_stock_page() {
    let mut ui = UiState::default();
    open(&mut ui);
    ui.pad(4, true, true);
    let deferred = ui.button(Button::Data, true, true);
    assert!(ui.is_open());
    assert!(ui.close_pending());
    assert!(!deferred.restore);
    ui.button(Button::Data, false, false);
    let released = ui.pad(4, false, true);
    assert_eq!(released.playback, Playback::NoteOff(4));
    assert!(released.restore);
    assert!(!ui.is_open());
}

#[test]
fn unsupported_callback_forwards_without_stale_ownership() {
    let mut ui = UiState::default();
    open(&mut ui);
    assert_eq!(ui.pad(1, true, false).flow, Flow::Forward);
    assert_eq!(ui.active_audition(), None);
    assert_eq!(ui.pad(1, false, false).flow, Flow::Forward);
}

#[test]
fn unsupported_release_forwards_matching_stock_note_off() {
    let mut ui = UiState::default();
    open(&mut ui);
    ui.pad(6, true, true);
    let release = ui.pad(6, false, false);
    assert_eq!(release.flow, Flow::Forward);
    assert_eq!(release.playback, Playback::None);
    assert_eq!(ui.active_audition(), None);
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
