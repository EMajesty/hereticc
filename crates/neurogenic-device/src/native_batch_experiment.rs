//! Approval-gated M0.6a static playback-cache experiment.

use std::sync::{Mutex, PoisonError, TryLockError};

use neurogenic_core::{
    EventId, MaterializationIdentity, MusicalTime, Pattern, PatternId, PatternLength, PatternTick,
    PlaybackWindowCompiler, PlaybackWindowLimits, Project, Scene, SceneId, SoundSource, StepEvent,
    TickInterval, TrackId,
};

use crate::native_batch::{
    NativeBatchCoordinator, NativeBatchCounts, NativeBatchItem, NativeBatchState,
};
use crate::native_sequence_experiment::{context_ready, current_context_identity, monotonic_ns};
use crate::native_stock_sequence::{
    NativeContextIdentity, NativeEditReceipt, NativeStockSequencePort,
};
use crate::stock_sequence::StockNoteRequest;

const LEASE_NS: u64 = 180_000_000_000;
const EVENT_COUNT: usize = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BatchExperimentState {
    Idle,
    Inserting,
    Ready,
    Cleaning,
    Cleaned,
    Failed,
    Dirty,
    Expired,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BatchDiagnostic {
    NotStarted,
    WaitingForPaint,
    Progress,
    AllEventsVerified,
    CleanupRequested,
    AllEventsAbsent,
    ContextChanged,
    DeadlineExpired,
    NativeFailure,
    AmbiguousOwnership,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BatchSnapshot {
    pub state: BatchExperimentState,
    pub diagnostic: BatchDiagnostic,
    pub counts: NativeBatchCounts,
    pub paints: u32,
}

struct Runtime {
    coordinator: NativeBatchCoordinator<NativeEditReceipt>,
    expected_context: Option<NativeContextIdentity>,
    deadline_ns: u64,
    diagnostic: BatchDiagnostic,
    override_state: Option<BatchExperimentState>,
    paints: u32,
}

impl Default for Runtime {
    fn default() -> Self {
        Self {
            coordinator: NativeBatchCoordinator::default(),
            expected_context: None,
            deadline_ns: 0,
            diagnostic: BatchDiagnostic::NotStarted,
            override_state: None,
            paints: 0,
        }
    }
}

static RUNTIME: Mutex<Option<Runtime>> = Mutex::new(None);

pub fn reset() {
    *RUNTIME.lock().unwrap_or_else(PoisonError::into_inner) = Some(Runtime::default());
}

fn items() -> Option<[NativeBatchItem; EVENT_COUNT]> {
    let scene_id = SceneId::new(1);
    let mut project = Project::empty(scene_id);
    let mut selections = [PatternId::new(0); TrackId::COUNT];
    let specifications: [&[(u64, u64, u8)]; 2] = [
        &[(0, 480, 0), (960, 240, 0), (1920, 480, 0), (2880, 240, 0)],
        &[(240, 240, 1), (973, 240, 1), (2040, 240, 1), (3857, 240, 1)],
    ];
    let mut event_id = 1_u64;
    for track in TrackId::all() {
        let pattern_id = PatternId::new(u64::from(track.get()) + 1);
        selections[track.index()] = pattern_id;
        project.tracks[track.index()].patterns.push(pattern_id);
        let mut notes = Vec::new();
        if let Some(track_events) = specifications.get(track.index()) {
            for (tick, duration, pad) in *track_events {
                notes.push(StepEvent {
                    id: EventId::new(event_id),
                    at: PatternTick::new(*tick),
                    duration_ticks: *duration,
                    sound: SoundSource::Pad(TrackId::new(*pad)?),
                    parameters: vec![],
                });
                event_id = event_id.checked_add(1)?;
            }
        }
        project.patterns.push(Pattern {
            id: pattern_id,
            track,
            length: PatternLength::new(7680)?,
            notes,
            parameters: vec![],
        });
    }
    project.scenes.push(Scene {
        id: scene_id,
        patterns: selections,
    });
    let mut compiler = PlaybackWindowCompiler::new(&project).ok()?;
    let interval = TickInterval::new(MusicalTime::new(0), MusicalTime::new(7680)).ok()?;
    let window = compiler
        .compile(
            interval,
            PlaybackWindowLimits {
                scheduled_events: 32,
                audio_requests: EVENT_COUNT,
            },
        )
        .ok()?;
    if window.audio.len() != EVENT_COUNT {
        return None;
    }
    let mut result = [NativeBatchItem {
        identity: MaterializationIdentity {
            event: EventId::new(0),
            at: MusicalTime::new(0),
        },
        request: StockNoteRequest::from_absolute(0, 0, 1, 100).ok()?,
    }; EVENT_COUNT];
    for (index, request) in window.audio.into_iter().enumerate() {
        let SoundSource::Pad(pad) = request.target else {
            return None;
        };
        result[index] = NativeBatchItem {
            identity: request.identity,
            request: StockNoteRequest::from_absolute(
                pad.get(),
                i64::try_from(request.identity.at.ticks()).ok()?,
                i64::try_from(request.duration_ticks).ok()?,
                request.velocity,
            )
            .ok()?,
        };
    }
    Some(result)
}

pub fn arm() -> bool {
    let Some(now) = monotonic_ns() else {
        return false;
    };
    let Some(deadline) = now.checked_add(LEASE_NS) else {
        return false;
    };
    if !context_ready() {
        return false;
    }
    let Some(context) = current_context_identity() else {
        return false;
    };
    let Some(items) = items() else {
        return false;
    };
    let mut lock = RUNTIME.lock().unwrap_or_else(PoisonError::into_inner);
    let runtime = lock.get_or_insert_with(Runtime::default);
    if runtime.coordinator.state() != NativeBatchState::Empty || runtime.override_state.is_some() {
        return false;
    }
    if runtime.coordinator.load(&items).is_err() {
        return false;
    }
    runtime.expected_context = Some(context);
    runtime.deadline_ns = deadline;
    runtime.diagnostic = BatchDiagnostic::WaitingForPaint;
    true
}

pub fn request_cleanup() -> bool {
    let mut runtime = RUNTIME.lock().unwrap_or_else(PoisonError::into_inner);
    let Some(runtime) = runtime.as_mut() else {
        return false;
    };
    if runtime.coordinator.begin_cleanup().is_err() {
        return false;
    }
    runtime.diagnostic = BatchDiagnostic::CleanupRequested;
    true
}

pub fn snapshot() -> BatchSnapshot {
    let runtime = RUNTIME.lock().unwrap_or_else(PoisonError::into_inner);
    let Some(runtime) = runtime.as_ref() else {
        return BatchSnapshot {
            state: BatchExperimentState::Idle,
            diagnostic: BatchDiagnostic::NotStarted,
            counts: NativeBatchCounts {
                total: 0,
                accepted: 0,
                verified: 0,
                removed: 0,
                unverified_accepted: 0,
            },
            paints: 0,
        };
    };
    BatchSnapshot {
        state: runtime
            .override_state
            .unwrap_or_else(|| match runtime.coordinator.state() {
                NativeBatchState::Empty => BatchExperimentState::Idle,
                NativeBatchState::Inserting => BatchExperimentState::Inserting,
                NativeBatchState::ReadyForPlayback => BatchExperimentState::Ready,
                NativeBatchState::Cleaning => BatchExperimentState::Cleaning,
                NativeBatchState::Cleaned => BatchExperimentState::Cleaned,
                NativeBatchState::Failed => BatchExperimentState::Failed,
                NativeBatchState::Dirty => BatchExperimentState::Dirty,
            }),
        diagnostic: runtime.diagnostic,
        counts: runtime.coordinator.counts(),
        paints: runtime.paints,
    }
}

pub(crate) fn on_step_paint(
    port: &mut NativeStockSequencePort<'_>,
    context: NativeContextIdentity,
    now: u64,
) {
    let mut runtime = match RUNTIME.try_lock() {
        Ok(runtime) => runtime,
        Err(TryLockError::WouldBlock) => return,
        Err(TryLockError::Poisoned(error)) => error.into_inner(),
    };
    let Some(runtime) = runtime.as_mut() else {
        return;
    };
    if runtime.override_state.is_some()
        || !matches!(
            runtime.coordinator.state(),
            NativeBatchState::Inserting | NativeBatchState::Cleaning
        )
    {
        return;
    }
    runtime.paints = runtime.paints.saturating_add(1);
    if now > runtime.deadline_ns {
        runtime.override_state = Some(BatchExperimentState::Expired);
        runtime.diagnostic = BatchDiagnostic::DeadlineExpired;
        return;
    }
    if runtime.expected_context != Some(context) {
        runtime.override_state = Some(BatchExperimentState::Failed);
        runtime.diagnostic = BatchDiagnostic::ContextChanged;
        return;
    }
    let result = match runtime.coordinator.state() {
        NativeBatchState::Inserting => runtime.coordinator.drive_insert(port),
        NativeBatchState::Cleaning => runtime.coordinator.drive_cleanup(port),
        _ => return,
    };
    if result.is_err() {
        runtime.diagnostic = if runtime.coordinator.counts().unverified_accepted > 0 {
            BatchDiagnostic::AmbiguousOwnership
        } else {
            BatchDiagnostic::NativeFailure
        };
        return;
    }
    runtime.diagnostic = match runtime.coordinator.state() {
        NativeBatchState::ReadyForPlayback => BatchDiagnostic::AllEventsVerified,
        NativeBatchState::Cleaned => BatchDiagnostic::AllEventsAbsent,
        NativeBatchState::Dirty => BatchDiagnostic::AmbiguousOwnership,
        _ => BatchDiagnostic::Progress,
    };
}

#[cfg(test)]
mod tests {
    use super::items;

    #[test]
    fn fixed_experiment_is_compiled_from_two_tracks_with_required_positions() {
        let items = items().expect("valid immutable fixture");
        let positions = items.map(|item| item.request.tick());
        assert_eq!(positions, [0, 240, 960, 973, 1920, 2040, 2880, 3857]);
        assert_eq!(items[0].request.physical_pad(), 0);
        assert_eq!(items[1].request.physical_pad(), 1);
        assert_eq!(items[4].request.duration(), 480);
        assert!(items[4].request.tick() + items[4].request.duration() > items[5].request.tick());
    }
}
