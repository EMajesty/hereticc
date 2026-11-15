use hereticc_core::{
    EventId, MusicalTime, Pattern, PatternId, PatternLength, PatternTick, Project, Scene, SceneId,
    SceneVoicePolicy, Scheduler, SoundSource, StepEvent, TickInterval, TrackId,
};
use hereticc_host::HostAdapter;

const STEP: u64 = 240;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let first_scene = SceneId::new(1);
    let second_scene = SceneId::new(2);
    let mut project = Project::empty(first_scene);
    let mut first_patterns = [PatternId::new(0); TrackId::COUNT];
    let mut second_patterns = [PatternId::new(0); TrackId::COUNT];

    for index in 0_u8..16 {
        let track = TrackId::new(index).expect("0..16 are valid tracks");
        let first = PatternId::new(100 + u64::from(index));
        let second = PatternId::new(200 + u64::from(index));
        first_patterns[usize::from(index)] = first;
        second_patterns[usize::from(index)] = second;
        project.tracks[usize::from(index)]
            .patterns
            .extend([first, second]);
        let length = if index == 0 { STEP * 15 } else { STEP * 16 };
        project
            .patterns
            .push(pattern(first, track, length, index, false));
        project
            .patterns
            .push(pattern(second, track, length, index, true));
    }
    project.scenes.extend([
        Scene {
            id: first_scene,
            patterns: first_patterns,
        },
        Scene {
            id: second_scene,
            patterns: second_patterns,
        },
    ]);

    let boundary = STEP * 16;
    let mut scheduler = Scheduler::new(&project)?;
    scheduler.request_scene(
        second_scene,
        MusicalTime::new(boundary),
        SceneVoicePolicy::Continue,
    )?;
    let events = scheduler.schedule(
        TickInterval::new(MusicalTime::new(0), MusicalTime::new(boundary + STEP * 2))?,
        128,
    )?;
    let mut adapter = HostAdapter::new(Some(first_scene));
    adapter.consume(&events)?;
    print!("{}", adapter.trace());
    Ok(())
}

fn pattern(id: PatternId, track: TrackId, length: u64, index: u8, second: bool) -> Pattern {
    let notes = if index < 2 {
        vec![StepEvent {
            id: EventId::new((if second { 2_000 } else { 1_000 }) + u64::from(index)),
            at: PatternTick::new(0),
            duration_ticks: STEP * 2,
            sound: SoundSource::TrackDefault,
            parameters: Vec::new(),
        }]
    } else {
        Vec::new()
    };
    Pattern {
        id,
        track,
        length: PatternLength::new(length).expect("positive pattern length"),
        notes,
        parameters: Vec::new(),
    }
}
