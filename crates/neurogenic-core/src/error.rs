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
    InvalidEncoding(&'static str),
}

impl fmt::Display for ValidationError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for ValidationError {}

/// Decoder policy limit exceeded by untrusted input.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DecodeLimit {
    EncodedProjectBytes,
    Patterns,
    Scenes,
    TrackPatternReferences,
    EventsPerPattern,
    EventsPerProject,
    ParameterChangesPerEvent,
    ParameterIdentifierBytes,
    DecodedAllocationBytes,
}

/// Syntactic or resource-policy failure while decoding project bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DecodeError {
    Truncated,
    InvalidMagic,
    InvalidTag(&'static str),
    InvalidUtf8,
    TrailingData {
        bytes: usize,
    },
    LengthOverflow,
    LimitExceeded {
        limit: DecodeLimit,
        declared: u64,
        maximum: u64,
    },
}

impl fmt::Display for DecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{self:?}")
    }
}

impl std::error::Error for DecodeError {}

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
    Decode(DecodeError),
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

impl From<DecodeError> for CoreError {
    fn from(value: DecodeError) -> Self {
        Self::Decode(value)
    }
}
