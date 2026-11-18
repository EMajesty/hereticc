use neurogenic_core::{
    CoreError, EventId, MusicalTime, Pattern, PatternId, PatternLength, PatternTick,
    PlaybackWindowCompiler, PlaybackWindowLimits, Project, Scene, SceneId, SoundSource, StepEvent,
    TickInterval, TrackId,
};

fn fixture() -> Project {
    let scene = SceneId::new(1);
    let mut project = Project::empty(scene);
    let mut selections = [PatternId::new(0); TrackId::COUNT];
    for track in TrackId::all() {
        let pattern_id = PatternId::new(u64::from(track.get()) + 10);
        selections[track.index()] = pattern_id;
        project.tracks[track.index()].patterns.push(pattern_id);
        let notes = match track.get() {
            0 => vec![
                StepEvent {
                    id: EventId::new(1),
                    at: PatternTick::new(0),
                    duration_ticks: 480,
                    sound: SoundSource::Pad(track),
                    parameters: vec![],
                },
                StepEvent {
                    id: EventId::new(2),
                    at: PatternTick::new(257),
                    duration_ticks: 500,
                    sound: SoundSource::Pad(TrackId::new(1).unwrap()),
                    parameters: vec![],
                },
            ],
            1 => vec![StepEvent {
                id: EventId::new(3),
                at: PatternTick::new(480),
                duration_ticks: 960,
                sound: SoundSource::TrackDefault,
                parameters: vec![],
            }],
            _ => vec![],
        };
        project.patterns.push(Pattern {
            id: pattern_id,
            track,
            length: PatternLength::new(1920).unwrap(),
            notes,
            parameters: vec![],
        });
    }
    project.scenes.push(Scene {
        id: scene,
        patterns: selections,
    });
    project
}

fn interval(start: u64, end: u64) -> TickInterval {
    TickInterval::new(MusicalTime::new(start), MusicalTime::new(end)).unwrap()
}

#[test]
fn compiles_multiple_targets_off_grid_overlap_and_half_open_boundaries() {
    let project = fixture();
    let mut compiler = PlaybackWindowCompiler::new(&project).unwrap();
    let window = compiler
        .compile(
            interval(0, 1920),
            PlaybackWindowLimits {
                scheduled_events: 16,
                audio_requests: 3,
            },
        )
        .unwrap();
    assert_eq!(window.audio.len(), 3);
    assert_eq!(window.audio[0].identity.at.ticks(), 0);
    assert_eq!(window.audio[1].identity.at.ticks(), 257);
    assert_eq!(window.audio[2].identity.at.ticks(), 480);
    assert_eq!(window.audio[1].duration_ticks, 500);
    assert_eq!(window.audio[2].track, TrackId::new(1).unwrap());
    assert_eq!(window.audio[0].velocity, 100);

    let repeated = compiler
        .compile(
            interval(1920, 3840),
            PlaybackWindowLimits {
                scheduled_events: 16,
                audio_requests: 3,
            },
        )
        .unwrap();
    assert_eq!(repeated.audio[0].identity.event, EventId::new(1));
    assert_eq!(repeated.audio[0].identity.at.ticks(), 1920);
}

#[test]
fn ordering_and_occurrence_identity_are_deterministic() {
    let project = fixture();
    let limits = PlaybackWindowLimits {
        scheduled_events: 16,
        audio_requests: 3,
    };
    let first = PlaybackWindowCompiler::new(&project)
        .unwrap()
        .compile(interval(0, 1920), limits)
        .unwrap();
    let second = PlaybackWindowCompiler::new(&project)
        .unwrap()
        .compile(interval(0, 1920), limits)
        .unwrap();
    assert_eq!(first, second);
}

#[test]
fn output_capacity_failure_is_transactional() {
    let project = fixture();
    let mut compiler = PlaybackWindowCompiler::new(&project).unwrap();
    let error = compiler
        .compile(
            interval(0, 1920),
            PlaybackWindowLimits {
                scheduled_events: 16,
                audio_requests: 2,
            },
        )
        .unwrap_err();
    assert_eq!(error, CoreError::OutputCapacityExceeded { capacity: 2 });
    assert_eq!(compiler.cursor(), MusicalTime::new(0));
    assert_eq!(
        compiler
            .compile(
                interval(0, 1920),
                PlaybackWindowLimits {
                    scheduled_events: 16,
                    audio_requests: 3,
                },
            )
            .unwrap()
            .audio
            .len(),
        3
    );
}
