use hereticc_core::codec;
use hereticc_core::{
    CoreError, DecodeError, DecodeLimit, EventId, EventKind, MacroId, MusicalTime, PPQN,
    ParameterAddress, ParameterChange, ParameterEvent, ParameterId, ParameterIntent,
    ParameterOwner, ParameterScope, Pattern, PatternId, PatternLength, PatternTick, ProcessorSlot,
    Project, Scene, SceneId, SceneVoicePolicy, Scheduler, SoundSource, StepEvent, TickInterval,
    TrackId, ValidationError, VoiceToken,
};

const STEP: u64 = PPQN as u64 / 4;
const SIXTEEN_STEPS: u64 = STEP * 16;

fn track(index: u8) -> TrackId {
    TrackId::new(index).expect("test track")
}

fn note(id: u64, at: u64, duration: u64) -> StepEvent {
    StepEvent {
        id: EventId::new(id),
        at: PatternTick::new(at),
        duration_ticks: duration,
        sound: SoundSource::TrackDefault,
        parameters: Vec::new(),
    }
}

fn project_with_lengths(lengths: [u64; 16]) -> Project {
    let scene_id = SceneId::new(1);
    let mut project = Project::empty(scene_id);
    let mut scene_patterns = [PatternId::new(0); 16];
    for index in 0_u8..16 {
        let track_id = track(index);
        let pattern_id = PatternId::new(100 + u64::from(index));
        scene_patterns[usize::from(index)] = pattern_id;
        project.tracks[usize::from(index)].patterns.push(pattern_id);
        project.patterns.push(Pattern {
            id: pattern_id,
            track: track_id,
            length: PatternLength::new(lengths[usize::from(index)]).expect("positive"),
            notes: Vec::new(),
            parameters: Vec::new(),
        });
    }
    project.scenes.push(Scene {
        id: scene_id,
        patterns: scene_patterns,
    });
    project
}

fn project() -> Project {
    project_with_lengths([SIXTEEN_STEPS; 16])
}

fn interval(start: u64, end: u64) -> TickInterval {
    TickInterval::new(MusicalTime::new(start), MusicalTime::new(end)).expect("interval")
}

fn note_ons(events: &[hereticc_core::ScheduledEvent]) -> Vec<(u64, u8, u64, VoiceToken)> {
    events
        .iter()
        .filter_map(|event| match event.kind {
            EventKind::NoteOn { voice, .. } => Some((
                event.at.ticks(),
                event.track.expect("note track").get(),
                event.origin.expect("note origin").get(),
                voice,
            )),
            _ => None,
        })
        .collect()
}

#[test]
fn initializes_exactly_sixteen_tracks() {
    let project = project();
    assert_eq!(project.tracks.len(), 16);
    assert_eq!(
        project.tracks.each_ref().map(|value| value.id),
        TrackId::all()
    );
    project.validate().expect("valid project");
}

#[test]
fn independent_fifteen_and_sixteen_step_patterns_wrap_without_drift() {
    let mut lengths = [SIXTEEN_STEPS; 16];
    lengths[0] = STEP * 15;
    let mut project = project_with_lengths(lengths);
    project.patterns[0].notes.push(note(1, 0, 1));
    project.patterns[1].notes.push(note(2, 0, 1));
    let horizon = STEP * 15 * 16;

    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler
        .schedule(interval(0, horizon), 128)
        .expect("schedule");
    let starts = note_ons(&events);
    assert_eq!(starts.iter().filter(|event| event.1 == 0).count(), 16);
    assert_eq!(starts.iter().filter(|event| event.1 == 1).count(), 15);
    assert!(
        starts
            .iter()
            .filter(|event| event.1 == 0)
            .enumerate()
            .all(|(cycle, event)| event.0 == cycle as u64 * STEP * 15)
    );
}

#[test]
fn tick_zero_and_pattern_wrap_are_both_scheduled() {
    let mut project = project();
    project.patterns[0].notes.push(note(1, 0, 1));
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler
        .schedule(interval(0, SIXTEEN_STEPS + 1), 16)
        .expect("schedule");
    assert_eq!(
        note_ons(&events)
            .into_iter()
            .map(|event| event.0)
            .collect::<Vec<_>>(),
        vec![0, SIXTEEN_STEPS]
    );
}

#[test]
fn simultaneous_events_have_stable_order_independent_of_insertion() {
    let mut first = project();
    first.patterns[0].notes = vec![note(20, 0, 10), note(10, 0, 10)];
    first.patterns[1].notes = vec![note(5, 0, 10)];
    let mut second = first.clone();
    second.patterns[0].notes.reverse();

    let mut first_scheduler = Scheduler::new(&first).expect("first");
    let mut second_scheduler = Scheduler::new(&second).expect("second");
    let first_events = first_scheduler.schedule(interval(0, 1), 16).expect("first");
    let second_events = second_scheduler
        .schedule(interval(0, 1), 16)
        .expect("second");
    assert_eq!(note_ons(&first_events), note_ons(&second_events));
    assert_eq!(
        note_ons(&first_events)
            .into_iter()
            .map(|event| (event.1, event.2))
            .collect::<Vec<_>>(),
        vec![(0, 10), (0, 20), (1, 5)]
    );
}

#[test]
fn adjacent_windows_equal_one_combined_window_without_boundary_duplicates() {
    let mut project = project();
    project.patterns[0].notes = vec![
        note(1, 0, STEP),
        note(2, STEP, STEP),
        note(3, STEP * 2, STEP),
    ];
    let end = STEP * 3;
    let mut combined = Scheduler::new(&project).expect("combined");
    let expected = combined.schedule(interval(0, end), 32).expect("combined");

    let mut chunked = Scheduler::new(&project).expect("chunked");
    let mut actual = chunked.schedule(interval(0, STEP), 32).expect("first");
    actual.extend(
        chunked
            .schedule(interval(STEP, STEP * 2), 32)
            .expect("second"),
    );
    actual.extend(
        chunked
            .schedule(interval(STEP * 2, end), 32)
            .expect("third"),
    );
    assert_eq!(actual, expected);
}

#[test]
fn overlapping_same_sound_notes_keep_distinct_voice_tokens() {
    let mut project = project();
    project.patterns[0].notes = vec![note(1, 0, STEP * 3), note(2, STEP, STEP * 3)];
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler
        .schedule(interval(0, STEP * 5), 32)
        .expect("schedule");
    let starts = note_ons(&events);
    assert_ne!(starts[0].3, starts[1].3);
    for (_, _, origin, token) in starts {
        assert!(events.iter().any(|event| {
            event.origin == Some(EventId::new(origin))
                && event.kind == EventKind::NoteOff { voice: token }
        }));
    }
}

#[test]
fn notes_can_overlap_their_next_pattern_cycle() {
    let mut lengths = [SIXTEEN_STEPS; 16];
    lengths[0] = STEP * 2;
    let mut project = project_with_lengths(lengths);
    project.patterns[0].notes.push(note(1, 0, STEP * 3));
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler
        .schedule(interval(0, STEP * 4), 32)
        .expect("schedule");
    let starts = note_ons(&events);
    assert_eq!(starts.len(), 2);
    assert_eq!(starts[1].0, STEP * 2);
    let first_token = starts[0].3;
    assert!(events.iter().any(|event| {
        event.at == MusicalTime::new(STEP * 3)
            && event.kind == EventKind::NoteOff { voice: first_token }
    }));
}

#[test]
fn scene_commit_is_atomic_phase_reset_and_old_voices_continue() {
    let mut project = project();
    project.patterns[0].notes.push(note(1, 0, STEP * 6));

    let new_scene = SceneId::new(2);
    let mut new_patterns = [PatternId::new(0); 16];
    for index in 0_u8..16 {
        let track_id = track(index);
        let pattern_id = PatternId::new(200 + u64::from(index));
        new_patterns[usize::from(index)] = pattern_id;
        project.tracks[usize::from(index)].patterns.push(pattern_id);
        project.patterns.push(Pattern {
            id: pattern_id,
            track: track_id,
            length: PatternLength::new(SIXTEEN_STEPS).expect("positive"),
            notes: vec![note(1000 + u64::from(index), 0, STEP)],
            parameters: Vec::new(),
        });
    }
    project.scenes.push(Scene {
        id: new_scene,
        patterns: new_patterns,
    });
    project.validate().expect("project");

    let boundary = STEP * 4;
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    scheduler
        .request_scene(
            new_scene,
            MusicalTime::new(boundary),
            SceneVoicePolicy::Continue,
        )
        .expect("request");
    let events = scheduler
        .schedule(interval(0, STEP * 7), 128)
        .expect("schedule");

    let at_boundary = events
        .iter()
        .filter(|event| event.at == MusicalTime::new(boundary))
        .collect::<Vec<_>>();
    assert!(matches!(at_boundary[0].kind, EventKind::SceneTransition(_)));
    assert_eq!(
        at_boundary
            .iter()
            .filter(|event| matches!(event.kind, EventKind::NoteOn { .. }))
            .count(),
        16
    );
    assert!(!events.iter().any(|event| {
        event.at >= MusicalTime::new(boundary)
            && event.origin == Some(EventId::new(1))
            && matches!(event.kind, EventKind::NoteOn { .. })
    }));
    assert!(events.iter().any(|event| {
        event.at == MusicalTime::new(STEP * 6)
            && event.origin == Some(EventId::new(1))
            && matches!(event.kind, EventKind::NoteOff { .. })
    }));
    assert_eq!(scheduler.active_scene(), new_scene);
}

#[test]
fn parameter_events_precede_note_starts_and_preserve_typed_intents() {
    let mut project = project();
    let note_id = EventId::new(1);
    let voice_change = ParameterChange {
        address: ParameterAddress {
            scope: ParameterScope::Voice,
            owner: ParameterOwner::EventVoice(note_id),
            processor_slot: None,
            parameter: ParameterId::new("pitch.semitones").expect("id"),
        },
        intent: ParameterIntent::Relative(7.0),
    };
    let track_change = ParameterChange {
        address: ParameterAddress {
            scope: ParameterScope::TrackInsert,
            owner: ParameterOwner::Track(track(0)),
            processor_slot: ProcessorSlot::new(1),
            parameter: ParameterId::new("filter.cutoff").expect("id"),
        },
        intent: ParameterIntent::Absolute(0.5),
    };
    let mut event = note(1, 0, STEP);
    event.parameters.push(voice_change.clone());
    project.patterns[0].notes.push(event);
    project.patterns[0].parameters.push(ParameterEvent {
        id: EventId::new(2),
        at: PatternTick::new(0),
        change: track_change.clone(),
    });

    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler.schedule(interval(0, 1), 8).expect("schedule");
    assert!(matches!(events[0].kind, EventKind::ParameterChange { .. }));
    assert!(matches!(events[1].kind, EventKind::ParameterChange { .. }));
    assert!(matches!(events[2].kind, EventKind::NoteOn { .. }));
    let resolved_voice = events.iter().find_map(|event| match &event.kind {
        EventKind::ParameterChange { change, voice } if *change == voice_change => *voice,
        _ => None,
    });
    assert_eq!(resolved_voice, Some(VoiceToken::new(0)));
    assert!(events.iter().any(|event| {
        matches!(&event.kind, EventKind::ParameterChange { change, voice: None } if *change == track_change)
    }));

    let macro_address = ParameterAddress {
        scope: ParameterScope::Macro,
        owner: ParameterOwner::Macro(MacroId::new(9)),
        processor_slot: None,
        parameter: ParameterId::new("morph").expect("id"),
    };
    assert!(macro_address.is_valid());
}

#[test]
fn capacity_failure_is_transactional() {
    let mut project = project();
    project.patterns[0].notes.push(note(1, 0, 10));
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    assert_eq!(
        scheduler.schedule(interval(0, 1), 0),
        Err(CoreError::OutputCapacityExceeded { capacity: 0 })
    );
    assert_eq!(scheduler.cursor(), MusicalTime::new(0));
    let events = scheduler.schedule(interval(0, 1), 1).expect("retry");
    assert_eq!(note_ons(&events)[0].3, VoiceToken::new(0));
}

#[test]
fn long_run_has_no_cumulative_pattern_drift() {
    let mut lengths = [SIXTEEN_STEPS; 16];
    lengths[0] = STEP * 15;
    let mut project = project_with_lengths(lengths);
    project.patterns[0].notes.push(note(1, 17, 1));
    let cycles = 10_000_u64;
    let cycle_count = usize::try_from(cycles).expect("test cycle count fits usize");
    let end = STEP * 15 * cycles + 18;
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let events = scheduler
        .schedule(interval(0, end), cycle_count * 2 + 1)
        .expect("long schedule");
    let starts = note_ons(&events);
    assert_eq!(starts.len(), cycle_count + 1);
    assert_eq!(starts.last().expect("last").0, STEP * 15 * cycles + 17);
}

#[test]
fn invalid_data_and_arithmetic_overflow_are_rejected() {
    assert!(PatternLength::new(0).is_none());
    assert_eq!(
        MusicalTime::new(u64::MAX).checked_add(1),
        Err(CoreError::ArithmeticOverflow)
    );

    let mut duplicate = project();
    duplicate.patterns.push(duplicate.patterns[0].clone());
    assert!(matches!(
        duplicate.validate(),
        Err(ValidationError::DuplicatePatternId(_))
    ));

    let mut dangling = project();
    dangling.scenes[0].patterns[0] = PatternId::new(9999);
    assert!(matches!(
        dangling.validate(),
        Err(ValidationError::DanglingPattern { .. })
    ));

    let mut invalid_track = project();
    invalid_track.tracks[0].id = track(1);
    assert!(matches!(
        invalid_track.validate(),
        Err(ValidationError::InvalidTrackSlot { .. })
    ));

    let mut standalone_voice = project();
    let parameter_id = EventId::new(77);
    standalone_voice.patterns[0]
        .parameters
        .push(ParameterEvent {
            id: parameter_id,
            at: PatternTick::new(0),
            change: ParameterChange {
                address: ParameterAddress {
                    scope: ParameterScope::Voice,
                    owner: ParameterOwner::EventVoice(parameter_id),
                    processor_slot: None,
                    parameter: ParameterId::new("pitch").expect("id"),
                },
                intent: ParameterIntent::Absolute(0.0),
            },
        });
    assert!(matches!(
        standalone_voice.validate(),
        Err(ValidationError::InvalidParameterAddress { .. })
    ));

    let mut overflow = project_with_lengths([2; 16]);
    overflow.patterns[0].notes.push(note(1, 0, u64::MAX));
    let mut scheduler = Scheduler::new(&overflow).expect("scheduler");
    assert_eq!(
        scheduler.schedule(interval(0, 3), 16),
        Err(CoreError::ArithmeticOverflow)
    );
    assert_eq!(scheduler.cursor(), MusicalTime::new(0));
}

#[test]
fn project_codec_round_trips_and_revalidates() {
    let mut project = project();
    project.tracks[0].default_sound = SoundSource::Sample {
        sample: hereticc_core::SampleId::new(88),
        slice: Some(hereticc_core::SliceId::new(3)),
    };
    project.patterns[0].notes.push(note(1, STEP, STEP * 2));
    let encoded = codec::encode(&project).expect("encode");
    let decoded = codec::decode(&encoded).expect("decode");
    assert_eq!(decoded, project);

    let mut trailing = encoded;
    trailing.push(0);
    assert_eq!(
        codec::decode(&trailing),
        Err(CoreError::Decode(DecodeError::TrailingData { bytes: 1 }))
    );
}

#[test]
fn large_valid_timestamps_do_not_fail_from_intermediate_overflow() {
    let mut project = project_with_lengths([u64::MAX; 16]);
    project.patterns[0].notes.push(note(1, 0, 1));
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    let first_end = u64::MAX - 1;
    let first = scheduler
        .schedule(interval(0, first_end), 4)
        .expect("large first window");
    assert_eq!(note_ons(&first).len(), 1);
    let final_window = scheduler
        .schedule(interval(first_end, u64::MAX), 4)
        .expect("mathematically next occurrence is beyond u64");
    assert_eq!(final_window, []);
}

#[test]
fn extreme_note_end_overflow_is_transactional() {
    let mut project = project_with_lengths([u64::MAX; 16]);
    project.patterns[0].notes.push(note(1, u64::MAX - 1, 2));
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    assert_eq!(
        scheduler.schedule(interval(0, u64::MAX), 8),
        Err(CoreError::ArithmeticOverflow)
    );
    assert_eq!(scheduler.cursor(), MusicalTime::new(0));
}

#[test]
fn huge_combined_and_contiguous_windows_are_equivalent() {
    let length = u64::MAX / 4;
    let mut project = project_with_lengths([length; 16]);
    project.patterns[0].notes.push(note(1, 7, 1));
    let split = length * 2;
    let end = length * 3 + 8;
    let mut combined = Scheduler::new(&project).expect("combined");
    let expected = combined.schedule(interval(0, end), 16).expect("combined");
    let mut chunked = Scheduler::new(&project).expect("chunked");
    let mut actual = chunked.schedule(interval(0, split), 16).expect("first");
    actual.extend(chunked.schedule(interval(split, end), 16).expect("second"));
    assert_eq!(actual, expected);
}

#[test]
fn decoder_rejects_truncation_and_oversized_counts_before_allocation() {
    let encoded = codec::encode(&project()).expect("encode");
    for length in 0..encoded.len() {
        assert!(
            codec::decode(&encoded[..length]).is_err(),
            "prefix {length}"
        );
    }

    let mut oversized_patterns = encoded.clone();
    let pattern_count_offset = 8 + 4 + 8 + 16 * (2 + 4 + 8);
    oversized_patterns[pattern_count_offset..pattern_count_offset + 4]
        .copy_from_slice(&u32::MAX.to_le_bytes());
    assert_eq!(
        codec::decode(&oversized_patterns),
        Err(CoreError::Decode(DecodeError::LimitExceeded {
            limit: DecodeLimit::Patterns,
            declared: u64::from(u32::MAX),
            maximum: 4_096,
        }))
    );

    let mut oversized_notes = encoded;
    let first_pattern_offset = pattern_count_offset + 4;
    let note_count_offset = first_pattern_offset + 8 + 1 + 8;
    oversized_notes[note_count_offset..note_count_offset + 4]
        .copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(matches!(
        codec::decode(&oversized_notes),
        Err(CoreError::Decode(DecodeError::LimitExceeded {
            limit: DecodeLimit::EventsPerPattern,
            ..
        }))
    ));
}

#[test]
fn decoder_rejects_invalid_utf8_oversized_strings_and_references() {
    let mut parameter_project = project();
    parameter_project.patterns[0]
        .parameters
        .push(ParameterEvent {
            id: EventId::new(500),
            at: PatternTick::new(0),
            change: ParameterChange {
                address: ParameterAddress {
                    scope: ParameterScope::Track,
                    owner: ParameterOwner::Track(track(0)),
                    processor_slot: None,
                    parameter: ParameterId::new("cutoff").expect("parameter"),
                },
                intent: ParameterIntent::Absolute(0.5),
            },
        });
    let encoded = codec::encode(&parameter_project).expect("encode");
    let position = encoded
        .windows(b"cutoff".len())
        .position(|window| window == b"cutoff")
        .expect("identifier bytes");

    let mut invalid_utf8 = encoded.clone();
    invalid_utf8[position] = 0xff;
    assert_eq!(
        codec::decode(&invalid_utf8),
        Err(CoreError::Decode(DecodeError::InvalidUtf8))
    );

    let mut oversized_string = encoded;
    oversized_string[position - 4..position].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(matches!(
        codec::decode(&oversized_string),
        Err(CoreError::Decode(DecodeError::LimitExceeded {
            limit: DecodeLimit::ParameterIdentifierBytes,
            ..
        }))
    ));

    let mut dangling = codec::encode(&project()).expect("encode");
    let first_scene_pattern = dangling.len() - TrackId::COUNT * 8;
    dangling[first_scene_pattern..first_scene_pattern + 8]
        .copy_from_slice(&999_999_u64.to_le_bytes());
    assert!(matches!(
        codec::decode(&dangling),
        Err(CoreError::InvalidProject(
            ValidationError::DanglingPattern { .. }
        ))
    ));
}

#[test]
fn decoder_custom_size_and_allocation_budgets_are_enforced() {
    let encoded = codec::encode(&project()).expect("encode");
    let limits = codec::DecodeLimits {
        max_encoded_project_bytes: encoded.len() - 1,
        ..codec::DecodeLimits::default()
    };
    assert!(matches!(
        codec::decode_with_limits(&encoded, limits),
        Err(CoreError::Decode(DecodeError::LimitExceeded {
            limit: DecodeLimit::EncodedProjectBytes,
            ..
        }))
    ));

    let limits = codec::DecodeLimits {
        max_decoded_allocation_bytes: 1,
        ..codec::DecodeLimits::default()
    };
    assert!(matches!(
        codec::decode_with_limits(&encoded, limits),
        Err(CoreError::Decode(DecodeError::LimitExceeded {
            limit: DecodeLimit::DecodedAllocationBytes,
            ..
        }))
    ));
}

#[test]
fn unsupported_scene_voice_policies_are_explicit() {
    let project = project();
    let mut scheduler = Scheduler::new(&project).expect("scheduler");
    assert_eq!(
        scheduler.request_scene(SceneId::new(1), MusicalTime::new(0), SceneVoicePolicy::Cut,),
        Err(CoreError::UnsupportedSceneVoicePolicy)
    );
}
