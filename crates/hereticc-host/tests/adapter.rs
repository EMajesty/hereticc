use hereticc_core::{
    EventId, EventKind, MusicalTime, ParameterAddress, ParameterChange, ParameterId,
    ParameterIntent, ParameterOwner, ParameterScope, SceneId, SceneTransition, SceneVoicePolicy,
    ScheduledEvent, SoundSource, TrackId, VoiceToken,
};
use hereticc_host::{HostAdapter, HostAdapterError};

fn track() -> TrackId {
    TrackId::new(0).expect("track")
}

fn on(at: u64, voice: u64) -> ScheduledEvent {
    ScheduledEvent {
        at: MusicalTime::new(at),
        track: Some(track()),
        origin: Some(EventId::new(10)),
        kind: EventKind::NoteOn {
            voice: VoiceToken::new(voice),
            sound: SoundSource::TrackDefault,
        },
    }
}

fn off(at: u64, voice: u64) -> ScheduledEvent {
    ScheduledEvent {
        at: MusicalTime::new(at),
        track: Some(track()),
        origin: Some(EventId::new(10)),
        kind: EventKind::NoteOff {
            voice: VoiceToken::new(voice),
        },
    }
}

#[test]
fn tracks_voice_lifecycle_and_records_deterministic_trace() {
    let parameter = ScheduledEvent {
        at: MusicalTime::new(0),
        track: Some(track()),
        origin: Some(EventId::new(9)),
        kind: EventKind::ParameterChange {
            change: ParameterChange {
                address: ParameterAddress {
                    scope: ParameterScope::Track,
                    owner: ParameterOwner::Track(track()),
                    processor_slot: None,
                    parameter: ParameterId::new("filter.cutoff").expect("id"),
                },
                intent: ParameterIntent::Absolute(0.5),
            },
            voice: None,
        },
    };
    let mut adapter = HostAdapter::default();
    adapter
        .consume(&[parameter, on(0, 7), off(4, 7)])
        .expect("consume");
    assert_eq!(adapter.active_voice_count(), 0);
    assert_eq!(
        adapter.trace(),
        concat!(
            "00000000000000000000 track=00 origin=9 PARAM scope=Track owner=track:0 slot=- id=\"filter.cutoff\" absolute=0.5 voice=-\n",
            "00000000000000000000 track=00 origin=10 ON voice=7 sound=track-default\n",
            "00000000000000000004 track=00 origin=10 OFF voice=7\n"
        )
    );
}

#[test]
fn duplicate_and_unmatched_tokens_are_explicit_and_transactional() {
    let mut adapter = HostAdapter::default();
    adapter.consume(&[on(0, 1)]).expect("first note");
    let prior_trace = adapter.trace();
    assert_eq!(
        adapter.consume(&[on(1, 1)]),
        Err(HostAdapterError::DuplicateVoiceToken(VoiceToken::new(1)))
    );
    assert_eq!(adapter.trace(), prior_trace);

    let mut empty = HostAdapter::default();
    assert_eq!(
        empty.consume(&[off(1, 99)]),
        Err(HostAdapterError::UnmatchedNoteOff(VoiceToken::new(99)))
    );
    assert_eq!(empty.events(), []);
}

#[test]
fn rejects_out_of_order_events_and_tracks_scene_transitions() {
    let first = SceneId::new(1);
    let second = SceneId::new(2);
    let transition = ScheduledEvent {
        at: MusicalTime::new(8),
        track: None,
        origin: None,
        kind: EventKind::SceneTransition(SceneTransition {
            from: first,
            to: second,
            voice_policy: SceneVoicePolicy::Continue,
        }),
    };
    let mut adapter = HostAdapter::new(Some(first));
    adapter.consume(&[transition]).expect("transition");
    assert_eq!(adapter.active_scene(), Some(second));
    assert!(adapter.trace().contains("SCENE 1->2 policy=Continue"));
    assert!(matches!(
        adapter.consume(&[on(7, 2)]),
        Err(HostAdapterError::OutOfOrder { .. })
    ));
}
