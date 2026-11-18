//! Fixed-capacity, commit-on-proof native batch coordinator.
//!
//! Each call advances at most one native insert, observation, remove, or
//! absence check. The coordinator retains only pointer-free requests and copied
//! receipts; callers drive it from the verified stock UI callback.

use neurogenic_core::MaterializationIdentity;

use crate::native_grid::GridEditPort;
use crate::native_stock_sequence::NativeEventPresence;
use crate::stock_sequence::StockNoteRequest;

pub const MAX_STATIC_BATCH_EVENTS: usize = 12;
const MAX_OBSERVATIONS: u8 = 8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeBatchItem {
    pub identity: MaterializationIdentity,
    pub request: StockNoteRequest,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeBatchState {
    Empty,
    Inserting,
    ReadyForPlayback,
    Failed,
    Cleaning,
    Cleaned,
    Dirty,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeBatchError<E> {
    Empty,
    Capacity,
    DuplicateIdentity,
    InvalidState,
    Native(E),
    AmbiguousEvent,
    ObservationLimit,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeBatchCounts {
    pub total: u8,
    pub accepted: u8,
    pub verified: u8,
    pub removed: u8,
    pub unverified_accepted: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SlotState {
    Empty,
    Pending,
    Accepted,
    Verified,
    RemoveAccepted,
    Removed,
}

#[derive(Clone, Copy, Debug)]
struct Slot<R: Copy> {
    item: Option<NativeBatchItem>,
    receipt: Option<R>,
    state: SlotState,
    observations: u8,
}

impl<R: Copy> Slot<R> {
    const EMPTY: Self = Self {
        item: None,
        receipt: None,
        state: SlotState::Empty,
        observations: 0,
    };
}

pub struct NativeBatchCoordinator<R: Copy> {
    slots: [Slot<R>; MAX_STATIC_BATCH_EVENTS],
    len: usize,
    cursor: usize,
    state: NativeBatchState,
}

impl<R: Copy> Default for NativeBatchCoordinator<R> {
    fn default() -> Self {
        Self {
            slots: [Slot::EMPTY; MAX_STATIC_BATCH_EVENTS],
            len: 0,
            cursor: 0,
            state: NativeBatchState::Empty,
        }
    }
}

impl<R: Copy> NativeBatchCoordinator<R> {
    /// Load a complete bounded batch before any native call.
    ///
    /// # Errors
    ///
    /// Rejects empty, oversized, duplicate-identity, or nonempty reuse.
    pub fn load(&mut self, items: &[NativeBatchItem]) -> Result<(), NativeBatchError<()>> {
        if self.state != NativeBatchState::Empty {
            return Err(NativeBatchError::InvalidState);
        }
        if items.is_empty() {
            return Err(NativeBatchError::Empty);
        }
        if items.len() > MAX_STATIC_BATCH_EVENTS {
            return Err(NativeBatchError::Capacity);
        }
        for (index, item) in items.iter().enumerate() {
            if items[..index]
                .iter()
                .any(|prior| prior.identity == item.identity)
            {
                return Err(NativeBatchError::DuplicateIdentity);
            }
            self.slots[index] = Slot {
                item: Some(*item),
                receipt: None,
                state: SlotState::Pending,
                observations: 0,
            };
        }
        self.len = items.len();
        self.cursor = 0;
        self.state = NativeBatchState::Inserting;
        Ok(())
    }

    #[must_use]
    pub const fn state(&self) -> NativeBatchState {
        self.state
    }

    #[must_use]
    pub fn counts(&self) -> NativeBatchCounts {
        let mut counts = NativeBatchCounts {
            total: u8::try_from(self.len).unwrap_or(u8::MAX),
            accepted: 0,
            verified: 0,
            removed: 0,
            unverified_accepted: 0,
        };
        for slot in &self.slots[..self.len] {
            if matches!(
                slot.state,
                SlotState::Accepted
                    | SlotState::Verified
                    | SlotState::RemoveAccepted
                    | SlotState::Removed
            ) {
                counts.accepted = counts.accepted.saturating_add(1);
            }
            if matches!(
                slot.state,
                SlotState::Verified | SlotState::RemoveAccepted | SlotState::Removed
            ) {
                counts.verified = counts.verified.saturating_add(1);
            }
            if slot.state == SlotState::Removed {
                counts.removed = counts.removed.saturating_add(1);
            }
            if slot.state == SlotState::Accepted {
                counts.unverified_accepted = counts.unverified_accepted.saturating_add(1);
            }
        }
        counts
    }

    /// Advance one insertion or verification boundary.
    ///
    /// # Errors
    ///
    /// Fails closed, preserving all per-slot evidence for explicit cleanup.
    pub fn drive_insert<P>(&mut self, port: &mut P) -> Result<(), NativeBatchError<P::Error>>
    where
        P: GridEditPort<Receipt = R>,
    {
        if self.state != NativeBatchState::Inserting || self.cursor >= self.len {
            return Err(NativeBatchError::InvalidState);
        }
        let slot = &mut self.slots[self.cursor];
        match slot.state {
            SlotState::Pending => {
                let Some(item) = slot.item else {
                    self.state = NativeBatchState::Failed;
                    return Err(NativeBatchError::InvalidState);
                };
                match port.insert(item.request) {
                    Ok(receipt) => {
                        slot.receipt = Some(receipt);
                        slot.state = SlotState::Accepted;
                        Ok(())
                    }
                    Err(error) => {
                        self.state = NativeBatchState::Failed;
                        Err(NativeBatchError::Native(error))
                    }
                }
            }
            SlotState::Accepted => {
                let Some(receipt) = slot.receipt.as_mut() else {
                    self.state = NativeBatchState::Failed;
                    return Err(NativeBatchError::InvalidState);
                };
                match port.observe(receipt) {
                    Ok((NativeEventPresence::Unique, _)) => {
                        slot.state = SlotState::Verified;
                        self.cursor += 1;
                        if self.cursor == self.len {
                            self.state = NativeBatchState::ReadyForPlayback;
                        }
                        Ok(())
                    }
                    Ok((NativeEventPresence::Absent, _)) => {
                        slot.observations = slot.observations.saturating_add(1);
                        if slot.observations >= MAX_OBSERVATIONS {
                            self.state = NativeBatchState::Failed;
                            Err(NativeBatchError::ObservationLimit)
                        } else {
                            Ok(())
                        }
                    }
                    Ok((NativeEventPresence::Ambiguous, _)) => {
                        self.state = NativeBatchState::Failed;
                        Err(NativeBatchError::AmbiguousEvent)
                    }
                    Err(error) => {
                        self.state = NativeBatchState::Failed;
                        Err(NativeBatchError::Native(error))
                    }
                }
            }
            _ => Err(NativeBatchError::InvalidState),
        }
    }

    /// Begin explicit cleanup of uniquely verified receipts.
    ///
    /// Accepted-but-unverified receipts are never removed speculatively.
    ///
    /// # Errors
    ///
    /// Rejects cleanup outside ready/failed states.
    pub fn begin_cleanup(&mut self) -> Result<(), NativeBatchError<()>> {
        if !matches!(
            self.state,
            NativeBatchState::ReadyForPlayback | NativeBatchState::Failed
        ) {
            return Err(NativeBatchError::InvalidState);
        }
        self.cursor = self.len;
        self.state = NativeBatchState::Cleaning;
        self.finish_cleanup_if_done();
        Ok(())
    }

    /// Advance one exact removal or absence-check boundary.
    ///
    /// # Errors
    ///
    /// Any ambiguity or native error stops cleanup without touching another
    /// receipt.
    pub fn drive_cleanup<P>(&mut self, port: &mut P) -> Result<(), NativeBatchError<P::Error>>
    where
        P: GridEditPort<Receipt = R>,
    {
        if self.state != NativeBatchState::Cleaning {
            return Err(NativeBatchError::InvalidState);
        }
        while self.cursor > 0 {
            let index = self.cursor - 1;
            let slot = &mut self.slots[index];
            match slot.state {
                SlotState::Verified => {
                    let Some(receipt) = slot.receipt else {
                        self.state = NativeBatchState::Dirty;
                        return Err(NativeBatchError::InvalidState);
                    };
                    match port.remove(&receipt) {
                        Ok(()) => {
                            slot.state = SlotState::RemoveAccepted;
                            slot.observations = 0;
                            return Ok(());
                        }
                        Err(error) => {
                            self.state = NativeBatchState::Dirty;
                            return Err(NativeBatchError::Native(error));
                        }
                    }
                }
                SlotState::RemoveAccepted => {
                    let Some(receipt) = slot.receipt.as_mut() else {
                        self.state = NativeBatchState::Dirty;
                        return Err(NativeBatchError::InvalidState);
                    };
                    match port.observe(receipt) {
                        Ok((NativeEventPresence::Absent, _)) => {
                            slot.state = SlotState::Removed;
                            self.cursor -= 1;
                            self.finish_cleanup_if_done();
                            return Ok(());
                        }
                        Ok((NativeEventPresence::Unique, _)) => {
                            slot.observations = slot.observations.saturating_add(1);
                            if slot.observations >= MAX_OBSERVATIONS {
                                self.state = NativeBatchState::Dirty;
                                return Err(NativeBatchError::ObservationLimit);
                            }
                            return Ok(());
                        }
                        Ok((NativeEventPresence::Ambiguous, _)) => {
                            self.state = NativeBatchState::Dirty;
                            return Err(NativeBatchError::AmbiguousEvent);
                        }
                        Err(error) => {
                            self.state = NativeBatchState::Dirty;
                            return Err(NativeBatchError::Native(error));
                        }
                    }
                }
                _ => self.cursor -= 1,
            }
        }
        self.finish_cleanup_if_done();
        Ok(())
    }

    fn finish_cleanup_if_done(&mut self) {
        if self.cursor == 0 && self.state == NativeBatchState::Cleaning {
            self.state = if self.counts().unverified_accepted == 0 {
                NativeBatchState::Cleaned
            } else {
                NativeBatchState::Dirty
            };
        }
    }
}
