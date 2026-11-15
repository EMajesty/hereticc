//! Hardware-independent, deterministic sequencing foundations for HERETICC.
//!
//! The crate has no device, operating-system, audio, MIDI, UI, or Akai SDK
//! dependency. M0.2 APIs allocate and are not claimed to be real-time safe.

pub mod codec;
pub mod error;
pub mod id;
pub mod model;
pub mod parameter;
pub mod scheduler;
pub mod time;

pub use error::{CoreError, DecodeError, DecodeLimit, ValidationError};
pub use id::{EventId, PatternId, SampleId, SceneId, SliceId, TrackId, VoiceToken};
pub use model::{PROJECT_FORMAT_VERSION, Pattern, Project, Scene, SoundSource, StepEvent, Track};
pub use parameter::{
    MAX_PARAMETER_ID_BYTES, MacroId, ParameterAddress, ParameterChange, ParameterEvent,
    ParameterId, ParameterIntent, ParameterOwner, ParameterScope, PerformanceEffectId,
    ProcessorSlot, SendId,
};
pub use scheduler::{EventKind, SceneTransition, SceneVoicePolicy, ScheduledEvent, Scheduler};
pub use time::{MusicalTime, PPQN, PatternLength, PatternTick, TickInterval};
