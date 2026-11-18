//! Bounded compilation of scheduled semantic events into audio cache requests.
//!
//! This is a host-oriented compiler over the existing scheduler. It contains no
//! firmware representation and is not claimed real-time safe.

use crate::error::{CoreError, ValidationError};
use crate::id::{EventId, TrackId};
use crate::model::{Project, SoundSource};
use crate::scheduler::{EventKind, Scheduler};
use crate::time::{MusicalTime, TickInterval};

/// Format-v1 events do not yet persist velocity, so static materialization uses
/// the agreed prototype default until format v2 adds a typed field.
pub const FORMAT_V1_DEFAULT_VELOCITY: u8 = 100;

/// Stable identity of one recurring event occurrence in an absolute timeline.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct MaterializationIdentity {
    pub event: EventId,
    pub at: MusicalTime,
}

/// One pointer-free audio request for a future stock or alternative backend.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AudioMaterializationRequest {
    pub identity: MaterializationIdentity,
    pub track: TrackId,
    pub target: SoundSource,
    pub duration_ticks: u64,
    pub velocity: u8,
}

/// Independent bounds for semantic scheduling and resulting audio requests.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PlaybackWindowLimits {
    pub scheduled_events: usize,
    pub audio_requests: usize,
}

/// Deterministic output for one contiguous half-open interval.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlaybackWindow {
    pub interval: TickInterval,
    pub audio: Vec<AudioMaterializationRequest>,
}

/// Stateful compiler preserving the scheduler's contiguous-window semantics.
pub struct PlaybackWindowCompiler<'project> {
    project: &'project Project,
    scheduler: Scheduler<'project>,
}

impl<'project> PlaybackWindowCompiler<'project> {
    /// Validate and bind an immutable project snapshot.
    ///
    /// # Errors
    ///
    /// Returns the existing project validation error.
    pub fn new(project: &'project Project) -> Result<Self, CoreError> {
        Ok(Self {
            project,
            scheduler: Scheduler::new(project)?,
        })
    }

    /// Compile one contiguous window transactionally.
    ///
    /// Scene transitions, note ends, and parameter events remain in the shared
    /// semantic timeline but only note starts become audio cache requests. A
    /// future MIDI router can consume the same scheduler stream without changing
    /// timestamp or ordering semantics.
    ///
    /// # Errors
    ///
    /// Returns the scheduler error or a fail-closed audio capacity error. The
    /// compiler cursor does not advance on failure.
    pub fn compile(
        &mut self,
        interval: TickInterval,
        limits: PlaybackWindowLimits,
    ) -> Result<PlaybackWindow, CoreError> {
        let mut transaction = self.scheduler.clone();
        let scheduled = transaction.schedule(interval, limits.scheduled_events)?;
        let mut audio = Vec::new();
        for event in scheduled {
            let EventKind::NoteOn { sound, .. } = event.kind else {
                continue;
            };
            let Some(track) = event.track else {
                return Err(ValidationError::InvalidEncoding("scheduled note track").into());
            };
            let Some(origin) = event.origin else {
                return Err(ValidationError::InvalidEncoding("scheduled note origin").into());
            };
            let Some(duration_ticks) = self
                .project
                .patterns
                .iter()
                .flat_map(|pattern| &pattern.notes)
                .find(|note| note.id == origin)
                .map(|note| note.duration_ticks)
            else {
                return Err(ValidationError::InvalidEncoding("scheduled note metadata").into());
            };
            if audio.len() == limits.audio_requests {
                return Err(CoreError::OutputCapacityExceeded {
                    capacity: limits.audio_requests,
                });
            }
            audio.push(AudioMaterializationRequest {
                identity: MaterializationIdentity {
                    event: origin,
                    at: event.at,
                },
                track,
                target: sound,
                duration_ticks,
                velocity: FORMAT_V1_DEFAULT_VELOCITY,
            });
        }
        self.scheduler = transaction;
        Ok(PlaybackWindow { interval, audio })
    }

    #[must_use]
    pub const fn cursor(&self) -> MusicalTime {
        self.scheduler.cursor()
    }
}
