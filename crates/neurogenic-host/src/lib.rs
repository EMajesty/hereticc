//! Host-only validation adapter for semantic scheduler output.
//!
//! This crate deliberately performs no audio, MIDI, wall-clock, device, or
//! firmware work. It validates voice lifecycle and records an exact event trace.

use std::collections::BTreeMap;
use std::fmt::Write as _;

use neurogenic_core::{
    EventId, EventKind, MusicalTime, ParameterIntent, ParameterOwner, SceneId, ScheduledEvent,
    SoundSource, TrackId, VoiceToken,
};

/// Voice state retained until the matching semantic note-off.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ActiveVoice {
    pub track: TrackId,
    pub origin: EventId,
    pub sound: SoundSource,
    pub started_at: MusicalTime,
}

/// Explicit rejection from the host event consumer.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum HostAdapterError {
    OutOfOrder {
        previous_tick: u64,
        previous_rank: u8,
        tick: u64,
        rank: u8,
    },
    MissingTrack(&'static str),
    MissingOrigin(&'static str),
    DuplicateVoiceToken(VoiceToken),
    UnmatchedNoteOff(VoiceToken),
    VoiceMetadataMismatch(VoiceToken),
    SceneMismatch {
        expected: SceneId,
        event_from: SceneId,
    },
}

impl core::fmt::Display for HostAdapterError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for HostAdapterError {}

/// Transactional host consumer retaining scheduler order exactly as received.
#[derive(Clone, Debug, Default)]
pub struct HostAdapter {
    active_voices: BTreeMap<VoiceToken, ActiveVoice>,
    events: Vec<ScheduledEvent>,
    active_scene: Option<SceneId>,
    last_order: Option<(MusicalTime, u8)>,
}

impl HostAdapter {
    #[must_use]
    pub const fn new(initial_scene: Option<SceneId>) -> Self {
        Self {
            active_voices: BTreeMap::new(),
            events: Vec::new(),
            active_scene: initial_scene,
            last_order: None,
        }
    }

    /// Validate and record a scheduler batch. Failure leaves all adapter state
    /// unchanged, allowing the caller to diagnose or retry the batch.
    ///
    /// # Errors
    ///
    /// Rejects ordering violations, malformed event metadata, duplicate tokens,
    /// unmatched note-offs, and inconsistent scene transitions.
    pub fn consume(&mut self, events: &[ScheduledEvent]) -> Result<(), HostAdapterError> {
        let mut transaction = self.clone();
        for event in events {
            transaction.consume_one(event)?;
        }
        *self = transaction;
        Ok(())
    }

    #[must_use]
    pub fn active_voice(&self, token: VoiceToken) -> Option<&ActiveVoice> {
        self.active_voices.get(&token)
    }

    #[must_use]
    pub fn active_voice_count(&self) -> usize {
        self.active_voices.len()
    }

    #[must_use]
    pub const fn active_scene(&self) -> Option<SceneId> {
        self.active_scene
    }

    #[must_use]
    pub fn events(&self) -> &[ScheduledEvent] {
        &self.events
    }

    /// Produce a deterministic line-oriented trace suitable for tests and
    /// inspection. Parameter identifiers use debug quoting so control bytes
    /// cannot create ambiguous trace lines.
    #[must_use]
    pub fn trace(&self) -> String {
        let mut output = String::new();
        for event in &self.events {
            write_trace_line(&mut output, event);
        }
        output
    }

    fn consume_one(&mut self, event: &ScheduledEvent) -> Result<(), HostAdapterError> {
        let rank = event_rank(&event.kind);
        if let Some((previous_at, previous_rank)) = self.last_order
            && (event.at, rank) < (previous_at, previous_rank)
        {
            return Err(HostAdapterError::OutOfOrder {
                previous_tick: previous_at.ticks(),
                previous_rank,
                tick: event.at.ticks(),
                rank,
            });
        }

        match &event.kind {
            EventKind::SceneTransition(transition) => {
                if let Some(expected) = self.active_scene
                    && expected != transition.from
                {
                    return Err(HostAdapterError::SceneMismatch {
                        expected,
                        event_from: transition.from,
                    });
                }
                self.active_scene = Some(transition.to);
            }
            EventKind::NoteOff { voice } => {
                let track = required_track(event, "note-off")?;
                let origin = required_origin(event, "note-off")?;
                let active = self
                    .active_voices
                    .get(voice)
                    .ok_or(HostAdapterError::UnmatchedNoteOff(*voice))?;
                if active.track != track || active.origin != origin {
                    return Err(HostAdapterError::VoiceMetadataMismatch(*voice));
                }
                self.active_voices.remove(voice);
            }
            EventKind::ParameterChange { .. } => {}
            EventKind::NoteOn { voice, sound } => {
                let track = required_track(event, "note-on")?;
                let origin = required_origin(event, "note-on")?;
                if self.active_voices.contains_key(voice) {
                    return Err(HostAdapterError::DuplicateVoiceToken(*voice));
                }
                self.active_voices.insert(
                    *voice,
                    ActiveVoice {
                        track,
                        origin,
                        sound: *sound,
                        started_at: event.at,
                    },
                );
            }
        }
        self.last_order = Some((event.at, rank));
        self.events.push(event.clone());
        Ok(())
    }
}

fn required_track(event: &ScheduledEvent, kind: &'static str) -> Result<TrackId, HostAdapterError> {
    event.track.ok_or(HostAdapterError::MissingTrack(kind))
}

fn required_origin(
    event: &ScheduledEvent,
    kind: &'static str,
) -> Result<EventId, HostAdapterError> {
    event.origin.ok_or(HostAdapterError::MissingOrigin(kind))
}

const fn event_rank(kind: &EventKind) -> u8 {
    match kind {
        EventKind::SceneTransition(_) => 0,
        EventKind::NoteOff { .. } => 1,
        EventKind::ParameterChange { .. } => 2,
        EventKind::NoteOn { .. } => 3,
    }
}

fn write_trace_line(output: &mut String, event: &ScheduledEvent) {
    let track = event
        .track
        .map_or_else(|| "--".to_owned(), |id| format!("{:02}", id.get()));
    let origin = event
        .origin
        .map_or_else(|| "-".to_owned(), |id| id.get().to_string());
    let prefix = format!("{:020} track={track} origin={origin}", event.at.ticks());
    match &event.kind {
        EventKind::SceneTransition(transition) => {
            writeln!(
                output,
                "{prefix} SCENE {}->{} policy={:?}",
                transition.from.get(),
                transition.to.get(),
                transition.voice_policy
            )
            .expect("writing to String cannot fail");
        }
        EventKind::NoteOff { voice } => {
            writeln!(output, "{prefix} OFF voice={}", voice.get())
                .expect("writing to String cannot fail");
        }
        EventKind::ParameterChange { change, voice } => {
            let slot = change
                .address
                .processor_slot
                .map_or_else(|| "-".to_owned(), |value| value.index().to_string());
            let intent = match change.intent {
                ParameterIntent::Absolute(value) => format!("absolute={value:?}"),
                ParameterIntent::Relative(value) => format!("relative={value:?}"),
            };
            let voice = voice.map_or_else(|| "-".to_owned(), |value| value.get().to_string());
            writeln!(
                output,
                "{prefix} PARAM scope={:?} owner={} slot={slot} id={:?} {intent} voice={voice}",
                change.address.scope,
                owner_trace(change.address.owner),
                change.address.parameter.as_str()
            )
            .expect("writing to String cannot fail");
        }
        EventKind::NoteOn { voice, sound } => {
            writeln!(
                output,
                "{prefix} ON voice={} sound={}",
                voice.get(),
                sound_trace(*sound)
            )
            .expect("writing to String cannot fail");
        }
    }
}

fn sound_trace(sound: SoundSource) -> String {
    match sound {
        SoundSource::TrackDefault => "track-default".to_owned(),
        SoundSource::Pad(track) => format!("pad:{}", track.get()),
        SoundSource::Sample { sample, slice } => slice.map_or_else(
            || format!("sample:{}", sample.get()),
            |slice| format!("sample:{}:slice:{}", sample.get(), slice.get()),
        ),
    }
}

fn owner_trace(owner: ParameterOwner) -> String {
    match owner {
        ParameterOwner::EventVoice(id) => format!("event-voice:{}", id.get()),
        ParameterOwner::Track(id) => format!("track:{}", id.get()),
        ParameterOwner::Send(id) => format!("send:{}", id.get()),
        ParameterOwner::Master => "master".to_owned(),
        ParameterOwner::PerformanceEffect(id) => format!("performance-fx:{}", id.get()),
        ParameterOwner::Macro(id) => format!("macro:{}", id.get()),
    }
}
