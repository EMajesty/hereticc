use core::fmt;

use crate::id::{EventId, PatternId, SceneId, TrackId};

/// Invalid persistent project data.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ValidationError {
    UnsupportedFormatVersion(u32),
    DuplicatePatternId(PatternId),
    DuplicateSceneId(SceneId),
    DuplicateEventId(EventId),
    InvalidTrackSlot {
        expected: TrackId,
        actual: TrackId,
    },
    MissingInitialScene(SceneId),
    DanglingPattern {
        scene: SceneId,
        pattern: PatternId,
    },
    PatternOnWrongTrack {
        scene: SceneId,
        slot: TrackId,
        pattern_track: TrackId,
    },
    PatternNotOwnedByTrack {
        pattern: PatternId,
        track: TrackId,
    },
    EventOutsidePattern {
        pattern: PatternId,
        event: EventId,
    },
    ZeroNoteDuration {
        pattern: PatternId,
        event: EventId,
    },
    InvalidParameterAddress {
        pattern: PatternId,
        event: EventId,
    },
    TrailingData,
    InvalidEncoding(&'static str),
}

impl fmt::Display for ValidationError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for ValidationError {}

/// Runtime, arithmetic, capacity, or persistence failure.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum CoreError {
    ArithmeticOverflow,
    InvalidInterval,
    NonContiguousInterval { expected: u64, actual: u64 },
    OutputCapacityExceeded { capacity: usize },
    VoiceTokenExhausted,
    SceneNotFound(SceneId),
    TransitionInPast,
    TransitionAlreadyPending,
    UnsupportedSceneVoicePolicy,
    InvalidProject(ValidationError),
    TruncatedProjectData,
}

impl fmt::Display for CoreError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for CoreError {}

impl From<ValidationError> for CoreError {
    fn from(value: ValidationError) -> Self {
        Self::InvalidProject(value)
    }
}
