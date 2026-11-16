use neurogenic_device::reducer::{Button, Flow, UiState};

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
    assert_eq!(ui.pad(7, true).flow, Flow::Consume);
    assert_eq!(ui.selected_track(), 7);
    assert_eq!(ui.pad(7, true).flow, Flow::Consume);
    assert_eq!(ui.pad(7, false).flow, Flow::Consume);
    assert_eq!(ui.pad(7, false).flow, Flow::Forward);
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
    assert_eq!(ui.pad(-1, true).flow, Flow::Forward);
    assert_eq!(ui.pad(16, true).flow, Flow::Forward);
    assert_eq!(ui.pad(0, false).flow, Flow::Forward);
}

#[test]
fn page_loss_closes_and_drains_owned_releases() {
    let mut ui = UiState::default();
    open(&mut ui);
    ui.pad(3, true);
    ui.button(Button::Plus, true, false);
    let lost = ui.lose_page();
    assert!(lost.restore);
    assert!(!ui.is_open());
    assert_eq!(ui.pad(3, false).flow, Flow::Consume);
    assert_eq!(ui.button(Button::Plus, false, false).flow, Flow::Consume);
    assert_eq!(ui.pad(4, true).flow, Flow::Forward);
    assert_eq!(ui.encoder(1).flow, Flow::Forward);
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
