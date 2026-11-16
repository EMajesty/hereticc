use std::collections::BTreeSet;

use crate::error::ValidationError;
use crate::id::{EventId, PatternId, SampleId, SceneId, SliceId, TrackId};
use crate::parameter::{ParameterChange, ParameterEvent, ParameterOwner, ParameterScope};
use crate::time::{PatternLength, PatternTick};

pub const PROJECT_FORMAT_VERSION: u32 = 1;

/// A stable, platform-neutral sound reference.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum SoundSource {
    TrackDefault,
    Pad(TrackId),
    Sample {
        sample: SampleId,
        slice: Option<SliceId>,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct StepEvent {
    pub id: EventId,
    pub at: PatternTick,
    pub duration_ticks: u64,
    pub sound: SoundSource,
    pub parameters: Vec<ParameterChange>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Pattern {
    pub id: PatternId,
    pub track: TrackId,
    pub length: PatternLength,
    pub notes: Vec<StepEvent>,
    pub parameters: Vec<ParameterEvent>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Track {
    pub id: TrackId,
    pub default_sound: SoundSource,
    pub patterns: Vec<PatternId>,
}

impl Track {
    fn empty(id: TrackId) -> Self {
        Self {
            id,
            default_sound: SoundSource::Pad(id),
            patterns: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Scene {
    pub id: SceneId,
    pub patterns: [PatternId; TrackId::COUNT],
}

#[derive(Clone, Debug, PartialEq)]
pub struct Project {
    pub format_version: u32,
    pub tracks: [Track; TrackId::COUNT],
    pub patterns: Vec<Pattern>,
    pub scenes: Vec<Scene>,
    pub initial_scene: SceneId,
}

impl Project {
    #[must_use]
    pub fn empty(initial_scene: SceneId) -> Self {
        Self {
            format_version: PROJECT_FORMAT_VERSION,
            tracks: TrackId::all().map(Track::empty),
            patterns: Vec::new(),
            scenes: Vec::new(),
            initial_scene,
        }
    }

    #[must_use]
    pub fn pattern(&self, id: PatternId) -> Option<&Pattern> {
        self.patterns.iter().find(|pattern| pattern.id == id)
    }

    #[must_use]
    pub fn scene(&self, id: SceneId) -> Option<&Scene> {
        self.scenes.iter().find(|scene| scene.id == id)
    }

    /// Validate all IDs, ownership, event positions, and scene references.
    ///
    /// # Errors
    ///
    /// Returns the first structural or semantic validation failure.
    pub fn validate(&self) -> Result<(), ValidationError> {
        if self.format_version != PROJECT_FORMAT_VERSION {
            return Err(ValidationError::UnsupportedFormatVersion(
                self.format_version,
            ));
        }

        self.validate_tracks()?;
        self.validate_patterns()?;
        self.validate_scenes()?;
        Ok(())
    }

    fn validate_tracks(&self) -> Result<(), ValidationError> {
        for (expected, track) in TrackId::all().into_iter().zip(&self.tracks) {
            if track.id != expected {
                return Err(ValidationError::InvalidTrackSlot {
                    expected,
                    actual: track.id,
                });
            }
        }
        for track in &self.tracks {
            for id in &track.patterns {
                let Some(pattern) = self.pattern(*id) else {
                    return Err(ValidationError::PatternNotOwnedByTrack {
                        pattern: *id,
                        track: track.id,
                    });
                };
                if pattern.track != track.id {
                    return Err(ValidationError::PatternNotOwnedByTrack {
                        pattern: *id,
                        track: track.id,
                    });
                }
            }
        }
        Ok(())
    }

    fn validate_patterns(&self) -> Result<(), ValidationError> {
        let mut pattern_ids = BTreeSet::new();
        let mut event_ids = BTreeSet::new();
        for pattern in &self.patterns {
            if !pattern_ids.insert(pattern.id) {
                return Err(ValidationError::DuplicatePatternId(pattern.id));
            }
            if !self.tracks[pattern.track.index()]
                .patterns
                .contains(&pattern.id)
            {
                return Err(ValidationError::PatternNotOwnedByTrack {
                    pattern: pattern.id,
                    track: pattern.track,
                });
            }
            for note in &pattern.notes {
                if !event_ids.insert(note.id) {
                    return Err(ValidationError::DuplicateEventId(note.id));
                }
                if note.at.ticks() >= pattern.length.ticks() {
                    return Err(ValidationError::EventOutsidePattern {
                        pattern: pattern.id,
                        event: note.id,
                    });
                }
                if note.duration_ticks == 0 {
                    return Err(ValidationError::ZeroNoteDuration {
                        pattern: pattern.id,
                        event: note.id,
                    });
                }
                for change in &note.parameters {
                    validate_change(pattern.id, note.id, change)?;
                }
            }
            for parameter in &pattern.parameters {
                if !event_ids.insert(parameter.id) {
                    return Err(ValidationError::DuplicateEventId(parameter.id));
                }
                if parameter.at.ticks() >= pattern.length.ticks() {
                    return Err(ValidationError::EventOutsidePattern {
                        pattern: pattern.id,
                        event: parameter.id,
                    });
                }
                validate_change(pattern.id, parameter.id, &parameter.change)?;
                if parameter.change.address.scope == ParameterScope::Voice {
                    return Err(ValidationError::InvalidParameterAddress {
                        pattern: pattern.id,
                        event: parameter.id,
                    });
                }
            }
        }
        Ok(())
    }

    fn validate_scenes(&self) -> Result<(), ValidationError> {
        let mut scene_ids = BTreeSet::new();
        for scene in &self.scenes {
            if !scene_ids.insert(scene.id) {
                return Err(ValidationError::DuplicateSceneId(scene.id));
            }
            for (slot, pattern_id) in TrackId::all().into_iter().zip(&scene.patterns) {
                let Some(pattern) = self.pattern(*pattern_id) else {
                    return Err(ValidationError::DanglingPattern {
                        scene: scene.id,
                        pattern: *pattern_id,
                    });
                };
                if pattern.track != slot {
                    return Err(ValidationError::PatternOnWrongTrack {
                        scene: scene.id,
                        slot,
                        pattern_track: pattern.track,
                    });
                }
            }
        }
        if self.scene(self.initial_scene).is_none() {
            return Err(ValidationError::MissingInitialScene(self.initial_scene));
        }
        Ok(())
    }
}

fn validate_change(
    pattern: PatternId,
    event: EventId,
    change: &ParameterChange,
) -> Result<(), ValidationError> {
    if !change.address.is_valid() || !change.intent.is_valid() {
        return Err(ValidationError::InvalidParameterAddress { pattern, event });
    }
    if change.address.scope == ParameterScope::Voice
        && change.address.owner != ParameterOwner::EventVoice(event)
    {
        return Err(ValidationError::InvalidParameterAddress { pattern, event });
    }
    Ok(())
}
