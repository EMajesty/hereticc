//! Deterministic translation of the current sixteen-step editor grid.

use crate::time::PPQN;
use crate::{
    EventId, Pattern, PatternId, PatternLength, PatternTick, SoundSource, StepEvent, TrackId,
};

/// One sixteenth note at the core's provisional 960 PPQ resolution.
pub const GRID_STEP_TICKS: u64 = PPQN as u64 / 4;
/// Length of the current sixteen-step prototype pattern.
pub const GRID_PATTERN_TICKS: u64 = GRID_STEP_TICKS * 16;

/// Translate one track's sixteen active-step bits into a core pattern.
///
/// Event identifiers are stable for `(track, step)`, notes are one grid step
/// long, and sound resolution remains the adapter's responsibility.
///
/// # Panics
///
/// This function has no data-dependent panic. Its fixed positive pattern
/// length is asserted when constructing the validated length type.
#[must_use]
pub fn pattern_from_step_bits(track: TrackId, id: PatternId, bits: u16) -> Pattern {
    let notes = (0_u8..16)
        .filter(|step| bits & (1_u16 << step) != 0)
        .map(|step| StepEvent {
            id: EventId::new(u64::from(track.get()) * 16 + u64::from(step) + 1),
            at: PatternTick::new(u64::from(step) * GRID_STEP_TICKS),
            duration_ticks: GRID_STEP_TICKS,
            sound: SoundSource::TrackDefault,
            parameters: Vec::new(),
        })
        .collect();
    Pattern {
        id,
        track,
        length: PatternLength::new(GRID_PATTERN_TICKS).expect("fixed grid length is positive"),
        notes,
        parameters: Vec::new(),
    }
}
