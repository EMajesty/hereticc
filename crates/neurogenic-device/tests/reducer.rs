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
fn unowned_bank_buttons_and_invalid_inputs_always_forward() {
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

#[test]
fn grid_bits_translate_deterministically_into_existing_core_events() {
    use neurogenic_core::{
        EventKind, GRID_PATTERN_TICKS, GRID_STEP_TICKS, MusicalTime, PatternId, Project, Scene,
        SceneId, Scheduler, TickInterval, TrackId, pattern_from_step_bits,
    };

    let mut ui = UiState::default();
    open(&mut ui);
    ui.button(Button::Plus, true, false);
    ui.button(Button::Plus, false, false);
    ui.encoder(4);
    ui.button(Button::Plus, true, false);
    ui.button(Button::Plus, false, false);

    let scene_id = SceneId::new(1);
    let mut project = Project::empty(scene_id);
    let mut patterns = [PatternId::new(0); 16];
    for track_index in 0_u8..16 {
        let track = TrackId::new(track_index).expect("track");
        let pattern_id = PatternId::new(100 + u64::from(track_index));
        patterns[usize::from(track_index)] = pattern_id;
        project.tracks[usize::from(track_index)]
            .patterns
            .push(pattern_id);
        let bits = ui
            .pattern_bits(usize::from(track_index))
            .expect("grid track");
        project
            .patterns
            .push(pattern_from_step_bits(track, pattern_id, bits));
    }
    project.scenes.push(Scene {
        id: scene_id,
        patterns,
    });

    let mut scheduler = Scheduler::new(&project).expect("translated project");
    let events = scheduler
        .schedule(
            TickInterval::new(MusicalTime::new(0), MusicalTime::new(GRID_PATTERN_TICKS))
                .expect("interval"),
            16,
        )
        .expect("events");
    let note_ons = events
        .iter()
        .filter_map(|event| match event.kind {
            EventKind::NoteOn { .. } => Some((event.at.ticks(), event.track.expect("track").get())),
            _ => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(note_ons, vec![(0, 0), (GRID_STEP_TICKS * 4, 0)]);
}
