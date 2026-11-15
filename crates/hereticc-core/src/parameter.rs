use crate::id::{EventId, TrackId};
use crate::time::PatternTick;

macro_rules! logical_id {
    ($name:ident) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
        pub struct $name(pub u64);

        impl $name {
            #[must_use]
            pub const fn new(value: u64) -> Self {
                Self(value)
            }

            #[must_use]
            pub const fn get(self) -> u64 {
                self.0
            }
        }
    };
}

logical_id!(SendId);
logical_id!(PerformanceEffectId);
logical_id!(MacroId);

/// Zero-based processor slot. M0.2 permits slots 0 and 1, matching the two
/// planned track/master insert slots.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ProcessorSlot(u8);

impl ProcessorSlot {
    #[must_use]
    pub const fn new(index: u8) -> Option<Self> {
        if index < 2 { Some(Self(index)) } else { None }
    }

    #[must_use]
    pub const fn index(self) -> u8 {
        self.0
    }
}

/// Stable symbolic parameter name, independent of firmware numeric IDs.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ParameterId(String);

impl ParameterId {
    pub fn new(value: impl Into<String>) -> Option<Self> {
        let value = value.into();
        if value.is_empty() || value.len() > 127 {
            None
        } else {
            Some(Self(value))
        }
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Logical parameter scope from `docs/EFFECTS.md`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ParameterScope {
    Voice,
    Track,
    TrackInsert,
    Send,
    SendEffect,
    Master,
    MasterInsert,
    PerformanceEffect,
    Macro,
}

/// Stable persistent owner. Voice parameters refer to the event that will
/// create a runtime voice, never to a persisted runtime token.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum ParameterOwner {
    EventVoice(EventId),
    Track(TrackId),
    Send(SendId),
    Master,
    PerformanceEffect(PerformanceEffectId),
    Macro(MacroId),
}

/// A hardware-independent parameter address.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ParameterAddress {
    pub scope: ParameterScope,
    pub owner: ParameterOwner,
    pub processor_slot: Option<ProcessorSlot>,
    pub parameter: ParameterId,
}

impl ParameterAddress {
    #[must_use]
    pub fn is_valid(&self) -> bool {
        let owner_matches = matches!(
            (self.scope, self.owner),
            (ParameterScope::Voice, ParameterOwner::EventVoice(_))
                | (
                    ParameterScope::Track | ParameterScope::TrackInsert,
                    ParameterOwner::Track(_)
                )
                | (
                    ParameterScope::Send | ParameterScope::SendEffect,
                    ParameterOwner::Send(_)
                )
                | (
                    ParameterScope::Master | ParameterScope::MasterInsert,
                    ParameterOwner::Master
                )
                | (
                    ParameterScope::PerformanceEffect,
                    ParameterOwner::PerformanceEffect(_)
                )
                | (ParameterScope::Macro, ParameterOwner::Macro(_))
        );
        let slot_matches = matches!(
            self.scope,
            ParameterScope::TrackInsert
                | ParameterScope::SendEffect
                | ParameterScope::MasterInsert
                | ParameterScope::PerformanceEffect
        ) == self.processor_slot.is_some();
        owner_matches && slot_matches
    }
}

/// An absolute baseline change or a relative modulation amount.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ParameterIntent {
    Absolute(f64),
    Relative(f64),
}

impl ParameterIntent {
    #[must_use]
    pub fn is_valid(self) -> bool {
        match self {
            Self::Absolute(value) | Self::Relative(value) => value.is_finite(),
        }
    }

    #[must_use]
    pub fn value(self) -> f64 {
        match self {
            Self::Absolute(value) | Self::Relative(value) => value,
        }
    }
}

/// Address plus intent, reusable from note-associated or standalone events.
#[derive(Clone, Debug, PartialEq)]
pub struct ParameterChange {
    pub address: ParameterAddress,
    pub intent: ParameterIntent,
}

/// A standalone pattern-relative parameter event.
#[derive(Clone, Debug, PartialEq)]
pub struct ParameterEvent {
    pub id: EventId,
    pub at: PatternTick,
    pub change: ParameterChange,
}
