use core::cmp::Ordering;

use crate::error::CoreError;
use crate::id::{EventId, PatternId, SceneId, TrackId, VoiceToken};
use crate::model::{Project, SoundSource, StepEvent};
use crate::parameter::{ParameterChange, ParameterOwner, ParameterScope};
use crate::time::{MusicalTime, TickInterval};

/// Voice behavior at a scene commit. Only `Continue` is implemented in M0.2.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SceneVoicePolicy {
    Continue,
    Cut,
    Release,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SceneTransition {
    pub from: SceneId,
    pub to: SceneId,
    pub voice_policy: SceneVoicePolicy,
}

#[derive(Clone, Debug, PartialEq)]
pub enum EventKind {
    SceneTransition(SceneTransition),
    NoteOff {
        voice: VoiceToken,
    },
    ParameterChange {
        change: ParameterChange,
        /// Resolved token for a voice-scoped note parameter.
        voice: Option<VoiceToken>,
    },
    NoteOn {
        voice: VoiceToken,
        sound: SoundSource,
    },
}

/// A semantic event at an absolute musical tick.
#[derive(Clone, Debug, PartialEq)]
pub struct ScheduledEvent {
    pub at: MusicalTime,
    pub track: Option<TrackId>,
    pub origin: Option<EventId>,
    pub kind: EventKind,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ActiveVoice {
    token: VoiceToken,
    track: TrackId,
    origin: EventId,
    off_at: MusicalTime,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct PendingTransition {
    scene: SceneId,
    at: MusicalTime,
    policy: SceneVoicePolicy,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct StartCandidate {
    at: MusicalTime,
    track: TrackId,
    pattern: PatternId,
    event_index: usize,
    cycle: u64,
}

/// Stateful deterministic scheduler over a validated immutable project.
///
/// M0.2 allocates vectors and transactionally clones scheduler state for every
/// call. It is suitable for host tests, not a real-time audio callback.
#[derive(Clone, Debug)]
pub struct Scheduler<'project> {
    project: &'project Project,
    active_scene: SceneId,
    scene_started_at: MusicalTime,
    cursor: MusicalTime,
    next_voice: u64,
    active_voices: Vec<ActiveVoice>,
    pending: Option<PendingTransition>,
}

impl<'project> Scheduler<'project> {
    /// Validate a project and create a scheduler at tick zero.
    ///
    /// # Errors
    ///
    /// Returns an error when project validation fails.
    pub fn new(project: &'project Project) -> Result<Self, CoreError> {
        project.validate()?;
        Ok(Self {
            project,
            active_scene: project.initial_scene,
            scene_started_at: MusicalTime::new(0),
            cursor: MusicalTime::new(0),
            next_voice: 0,
            active_voices: Vec::new(),
            pending: None,
        })
    }

    #[must_use]
    pub const fn cursor(&self) -> MusicalTime {
        self.cursor
    }

    #[must_use]
    pub const fn active_scene(&self) -> SceneId {
        self.active_scene
    }

    /// Queue one scene transition at an explicit absolute tick.
    ///
    /// # Errors
    ///
    /// Returns an error for an unknown scene, past boundary, existing request,
    /// or an M0.2-unsupported voice policy.
    pub fn request_scene(
        &mut self,
        scene: SceneId,
        at: MusicalTime,
        policy: SceneVoicePolicy,
    ) -> Result<(), CoreError> {
        if policy != SceneVoicePolicy::Continue {
            return Err(CoreError::UnsupportedSceneVoicePolicy);
        }
        if self.project.scene(scene).is_none() {
            return Err(CoreError::SceneNotFound(scene));
        }
        if at < self.cursor {
            return Err(CoreError::TransitionInPast);
        }
        if self.pending.is_some() {
            return Err(CoreError::TransitionAlreadyPending);
        }
        self.pending = Some(PendingTransition { scene, at, policy });
        Ok(())
    }

    /// Schedule one contiguous half-open interval. On any error, including
    /// capacity exhaustion, scheduler state remains unchanged.
    ///
    /// # Errors
    ///
    /// Returns an error for a non-contiguous interval, arithmetic/token
    /// overflow, or output beyond `capacity`.
    pub fn schedule(
        &mut self,
        interval: TickInterval,
        capacity: usize,
    ) -> Result<Vec<ScheduledEvent>, CoreError> {
        if interval.start() != self.cursor {
            return Err(CoreError::NonContiguousInterval {
                expected: self.cursor.ticks(),
                actual: interval.start().ticks(),
            });
        }
        let mut transaction = self.clone();
        let events = transaction.schedule_inner(interval)?;
        if events.len() > capacity {
            return Err(CoreError::OutputCapacityExceeded { capacity });
        }
        *self = transaction;
        Ok(events)
    }

    fn schedule_inner(&mut self, interval: TickInterval) -> Result<Vec<ScheduledEvent>, CoreError> {
        let mut events = Vec::new();
        let mut starts = Vec::new();

        for active in &self.active_voices {
            if interval.contains(active.off_at) {
                events.push(ScheduledEvent {
                    at: active.off_at,
                    track: Some(active.track),
                    origin: Some(active.origin),
                    kind: EventKind::NoteOff {
                        voice: active.token,
                    },
                });
            }
        }

        let transition = self.pending.filter(|pending| interval.contains(pending.at));
        if let Some(pending) = transition {
            self.collect_segment(
                self.active_scene,
                self.scene_started_at,
                interval.start(),
                pending.at,
                &mut starts,
                &mut events,
            )?;
            let from = self.active_scene;
            self.active_scene = pending.scene;
            self.scene_started_at = pending.at;
            self.pending = None;
            events.push(ScheduledEvent {
                at: pending.at,
                track: None,
                origin: None,
                kind: EventKind::SceneTransition(SceneTransition {
                    from,
                    to: pending.scene,
                    voice_policy: pending.policy,
                }),
            });
            self.collect_segment(
                self.active_scene,
                self.scene_started_at,
                pending.at,
                interval.end(),
                &mut starts,
                &mut events,
            )?;
        } else {
            self.collect_segment(
                self.active_scene,
                self.scene_started_at,
                interval.start(),
                interval.end(),
                &mut starts,
                &mut events,
            )?;
        }

        starts.sort_by_key(|candidate| {
            (
                candidate.at,
                candidate.track,
                candidate.pattern,
                self.project
                    .pattern(candidate.pattern)
                    .expect("validated")
                    .notes[candidate.event_index]
                    .id,
                candidate.cycle,
            )
        });
        for start in starts {
            self.emit_start(start, interval, &mut events)?;
        }

        self.active_voices
            .retain(|voice| voice.off_at >= interval.end());
        events.sort_by(compare_events);
        self.cursor = interval.end();
        Ok(events)
    }

    fn collect_segment(
        &self,
        scene_id: SceneId,
        epoch: MusicalTime,
        start: MusicalTime,
        end: MusicalTime,
        starts: &mut Vec<StartCandidate>,
        events: &mut Vec<ScheduledEvent>,
    ) -> Result<(), CoreError> {
        if start >= end {
            return Ok(());
        }
        let scene = self.project.scene(scene_id).expect("validated scene");
        for track in TrackId::all() {
            let pattern_id = scene.patterns[track.index()];
            let pattern = self.project.pattern(pattern_id).expect("validated pattern");
            for (event_index, note) in pattern.notes.iter().enumerate() {
                for (at, cycle) in
                    occurrences(epoch, start, end, note.at.ticks(), pattern.length.ticks())?
                {
                    starts.push(StartCandidate {
                        at,
                        track,
                        pattern: pattern_id,
                        event_index,
                        cycle,
                    });
                }
            }
            for parameter in &pattern.parameters {
                for (at, _) in occurrences(
                    epoch,
                    start,
                    end,
                    parameter.at.ticks(),
                    pattern.length.ticks(),
                )? {
                    events.push(ScheduledEvent {
                        at,
                        track: Some(track),
                        origin: Some(parameter.id),
                        kind: EventKind::ParameterChange {
                            change: parameter.change.clone(),
                            voice: None,
                        },
                    });
                }
            }
        }
        Ok(())
    }

    fn emit_start(
        &mut self,
        candidate: StartCandidate,
        interval: TickInterval,
        events: &mut Vec<ScheduledEvent>,
    ) -> Result<(), CoreError> {
        let note: &StepEvent = &self
            .project
            .pattern(candidate.pattern)
            .expect("validated pattern")
            .notes[candidate.event_index];
        let token_value = self.next_voice;
        self.next_voice = self
            .next_voice
            .checked_add(1)
            .ok_or(CoreError::VoiceTokenExhausted)?;
        let token = VoiceToken::new(token_value);
        let off_at = candidate.at.checked_add(note.duration_ticks)?;

        for change in &note.parameters {
            events.push(ScheduledEvent {
                at: candidate.at,
                track: Some(candidate.track),
                origin: Some(note.id),
                kind: EventKind::ParameterChange {
                    voice: (change.address.scope == ParameterScope::Voice
                        && change.address.owner == ParameterOwner::EventVoice(note.id))
                    .then_some(token),
                    change: change.clone(),
                },
            });
        }
        events.push(ScheduledEvent {
            at: candidate.at,
            track: Some(candidate.track),
            origin: Some(note.id),
            kind: EventKind::NoteOn {
                voice: token,
                sound: note.sound,
            },
        });
        if interval.contains(off_at) {
            events.push(ScheduledEvent {
                at: off_at,
                track: Some(candidate.track),
                origin: Some(note.id),
                kind: EventKind::NoteOff { voice: token },
            });
        } else if off_at >= interval.end() {
            self.active_voices.push(ActiveVoice {
                token,
                track: candidate.track,
                origin: note.id,
                off_at,
            });
        }
        Ok(())
    }
}

fn occurrences(
    epoch: MusicalTime,
    start: MusicalTime,
    end: MusicalTime,
    event_tick: u64,
    length: u64,
) -> Result<Vec<(MusicalTime, u64)>, CoreError> {
    let start_relative = start.checked_sub(epoch)?;
    let (mut absolute, mut cycle) = if event_tick >= start_relative {
        (epoch.checked_add(event_tick)?, 0)
    } else {
        let difference = start_relative
            .checked_sub(event_tick)
            .ok_or(CoreError::ArithmeticOverflow)?;
        let quotient = difference / length;
        let remainder = difference % length;
        let cycle = quotient
            .checked_add(u64::from(remainder != 0))
            .ok_or(CoreError::ArithmeticOverflow)?;
        let advance = if remainder == 0 {
            0
        } else {
            length - remainder
        };
        let Some(absolute) = start.ticks().checked_add(advance).map(MusicalTime::new) else {
            // The next mathematical occurrence is beyond the representable
            // timestamp domain, so it cannot belong to this valid interval.
            return Ok(Vec::new());
        };
        (absolute, cycle)
    };
    let mut output = Vec::new();
    while absolute < end {
        if absolute >= start {
            output.push((absolute, cycle));
        }
        let Some(next_absolute) = absolute.ticks().checked_add(length).map(MusicalTime::new) else {
            break;
        };
        absolute = next_absolute;
        cycle = cycle.checked_add(1).ok_or(CoreError::ArithmeticOverflow)?;
    }
    Ok(output)
}

fn compare_events(left: &ScheduledEvent, right: &ScheduledEvent) -> Ordering {
    event_key(left)
        .cmp(&event_key(right))
        .then_with(|| match (&left.kind, &right.kind) {
            (
                EventKind::ParameterChange {
                    change: left_change,
                    ..
                },
                EventKind::ParameterChange {
                    change: right_change,
                    ..
                },
            ) => left_change
                .address
                .cmp(&right_change.address)
                .then_with(|| intent_key(left_change.intent).cmp(&intent_key(right_change.intent))),
            _ => Ordering::Equal,
        })
}

fn event_key(event: &ScheduledEvent) -> (MusicalTime, u8, u8, u64, u64) {
    let (rank, voice) = match event.kind {
        EventKind::SceneTransition(_) => (0, 0),
        EventKind::NoteOff { voice } => (1, voice.get()),
        EventKind::ParameterChange { voice, .. } => (2, voice.map_or(0, VoiceToken::get)),
        EventKind::NoteOn { voice, .. } => (3, voice.get()),
    };
    (
        event.at,
        rank,
        event.track.map_or(u8::MAX, TrackId::get),
        event.origin.map_or(0, EventId::get),
        voice,
    )
}

fn intent_key(intent: crate::parameter::ParameterIntent) -> (u8, u64) {
    match intent {
        crate::parameter::ParameterIntent::Absolute(value) => (0, value.to_bits()),
        crate::parameter::ParameterIntent::Relative(value) => (1, value.to_bits()),
    }
}
