use crate::error::CoreError;

/// Provisional project resolution. It is a host-side design choice, not a
/// statement about the MPC Sample's internal clock.
pub const PPQN: u32 = 960;

/// Absolute integer musical time in ticks.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct MusicalTime(u64);

impl MusicalTime {
    #[must_use]
    pub const fn new(ticks: u64) -> Self {
        Self(ticks)
    }

    #[must_use]
    pub const fn ticks(self) -> u64 {
        self.0
    }

    /// Add ticks without wrapping.
    ///
    /// # Errors
    ///
    /// Returns [`CoreError::ArithmeticOverflow`] if the result exceeds `u64`.
    pub fn checked_add(self, ticks: u64) -> Result<Self, CoreError> {
        self.0
            .checked_add(ticks)
            .map(Self)
            .ok_or(CoreError::ArithmeticOverflow)
    }

    /// Return the distance from an earlier time without wrapping.
    ///
    /// # Errors
    ///
    /// Returns [`CoreError::ArithmeticOverflow`] when `earlier` is later.
    pub fn checked_sub(self, earlier: Self) -> Result<u64, CoreError> {
        self.0
            .checked_sub(earlier.0)
            .ok_or(CoreError::ArithmeticOverflow)
    }
}

/// A strictly positive pattern length in ticks.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PatternLength(u64);

impl PatternLength {
    #[must_use]
    pub const fn new(ticks: u64) -> Option<Self> {
        if ticks == 0 { None } else { Some(Self(ticks)) }
    }

    #[must_use]
    pub const fn ticks(self) -> u64 {
        self.0
    }
}

/// A position inside a pattern. Project validation checks it against the
/// owning pattern's length.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PatternTick(u64);

impl PatternTick {
    #[must_use]
    pub const fn new(ticks: u64) -> Self {
        Self(ticks)
    }

    #[must_use]
    pub const fn ticks(self) -> u64 {
        self.0
    }
}

/// A half-open scheduling interval `[start, end)`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TickInterval {
    start: MusicalTime,
    end: MusicalTime,
}

impl TickInterval {
    /// Construct a half-open interval.
    ///
    /// # Errors
    ///
    /// Returns [`CoreError::InvalidInterval`] when `end` precedes `start`.
    pub fn new(start: MusicalTime, end: MusicalTime) -> Result<Self, CoreError> {
        if end < start {
            return Err(CoreError::InvalidInterval);
        }
        Ok(Self { start, end })
    }

    #[must_use]
    pub const fn start(self) -> MusicalTime {
        self.start
    }

    #[must_use]
    pub const fn end(self) -> MusicalTime {
        self.end
    }

    #[must_use]
    pub const fn contains(self, time: MusicalTime) -> bool {
        time.0 >= self.start.0 && time.0 < self.end.0
    }

    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.start.0 == self.end.0
    }
}
