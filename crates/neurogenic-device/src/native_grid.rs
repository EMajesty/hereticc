//! Commit-on-proof coordinator for the first native 16-by-16 grid.
//!
//! The coordinator owns only copied requests and receipts. A caller must drive
//! it from the verified stock Step-paint callback; it does not schedule native
//! work or retain firmware pointers.

use crate::native_stock_sequence::{
    NativeEditError, NativeEditReceipt, NativeEventPresence, NativeScanEvidence,
    NativeStockSequencePort,
};
use crate::reducer::{CELLS, UiState};
use crate::stock_sequence::{StockNoteRequest, StockSequenceEditPort};

const DEFAULT_VELOCITY: u8 = 100;

/// Pointer-free grid location.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GridCell {
    pub physical_pad: u8,
    pub step: u8,
}

/// Externally visible transaction stage for pending/error UI feedback.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditState {
    Idle,
    InsertRequested(GridCell),
    InsertAccepted(GridCell),
    RemoveRequested(GridCell),
    RemoveAccepted(GridCell),
    Failed(GridCell),
}

/// Why a grid request could not be staged.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridRequestError {
    Busy,
    EditorClosed,
    InvalidCell,
    UntrackedActiveCell,
}

/// Why a staged native edit failed closed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditError<E> {
    Native(E),
    AmbiguousEvent,
    Terminal,
}

/// One callback-sized amount of progress.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditProgress {
    Waiting,
    InsertCommandAccepted,
    InsertCommitted,
    RemoveCommandAccepted,
    RemoveCommitted,
}

/// Minimal native operations needed by the coordinator.
pub trait GridEditPort {
    type Receipt: Copy;
    type Error: Copy;

    /// Submit one insertion.
    ///
    /// # Errors
    ///
    /// Returns the adapter's fail-closed native error.
    fn insert(&mut self, request: StockNoteRequest) -> Result<Self::Receipt, Self::Error>;
    /// Observe the receipt in the current guarded context.
    ///
    /// # Errors
    ///
    /// Returns the adapter's fail-closed native error.
    fn observe(
        &mut self,
        receipt: &mut Self::Receipt,
    ) -> Result<(NativeEventPresence, NativeScanEvidence), Self::Error>;
    /// Submit exact removal for an owned canonical receipt.
    ///
    /// # Errors
    ///
    /// Returns the adapter's fail-closed native error.
    fn remove(&mut self, receipt: &Self::Receipt) -> Result<(), Self::Error>;
}

impl GridEditPort for NativeStockSequencePort<'_> {
    type Receipt = NativeEditReceipt;
    type Error = NativeEditError;

    fn insert(&mut self, request: StockNoteRequest) -> Result<Self::Receipt, Self::Error> {
        self.insert_current_track(request)
    }

    fn observe(
        &mut self,
        receipt: &mut Self::Receipt,
    ) -> Result<(NativeEventPresence, NativeScanEvidence), Self::Error> {
        self.observe(receipt)
    }

    fn remove(&mut self, receipt: &Self::Receipt) -> Result<(), Self::Error> {
        self.remove(receipt)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Pending<R: Copy> {
    Idle,
    InsertRequested {
        cell: GridCell,
        request: StockNoteRequest,
    },
    InsertAccepted {
        cell: GridCell,
        receipt: R,
    },
    RemoveRequested {
        cell: GridCell,
        receipt: R,
    },
    RemoveAccepted {
        cell: GridCell,
        receipt: R,
    },
    Failed(GridCell),
}

/// Fixed-capacity receipt ownership for the first playable grid.
pub struct NativeGridEditor<R: Copy> {
    receipts: [[Option<R>; CELLS]; CELLS],
    pending: Pending<R>,
}

impl<R: Copy> Default for NativeGridEditor<R> {
    fn default() -> Self {
        Self {
            receipts: [[None; CELLS]; CELLS],
            pending: Pending::Idle,
        }
    }
}

impl<R: Copy> NativeGridEditor<R> {
    #[must_use]
    pub const fn state(&self) -> GridEditState {
        match self.pending {
            Pending::Idle => GridEditState::Idle,
            Pending::InsertRequested { cell, .. } => GridEditState::InsertRequested(cell),
            Pending::InsertAccepted { cell, .. } => GridEditState::InsertAccepted(cell),
            Pending::RemoveRequested { cell, .. } => GridEditState::RemoveRequested(cell),
            Pending::RemoveAccepted { cell, .. } => GridEditState::RemoveAccepted(cell),
            Pending::Failed(cell) => GridEditState::Failed(cell),
        }
    }

    /// Stage the selected cell without changing the displayed committed bit.
    ///
    /// # Errors
    ///
    /// Rejects a closed editor, another pending/failed operation, an invalid
    /// cell, or an active cell for which this coordinator owns no receipt.
    pub fn request_selected(&mut self, ui: &UiState) -> Result<GridCell, GridRequestError> {
        if !matches!(self.pending, Pending::Idle) {
            return Err(GridRequestError::Busy);
        }
        if !ui.is_open() {
            return Err(GridRequestError::EditorClosed);
        }
        let cell = GridCell {
            physical_pad: ui.selected_track(),
            step: ui.selected_step(),
        };
        let track = usize::from(cell.physical_pad);
        let step = usize::from(cell.step);
        if ui.step_active(track, step) {
            let Some(receipt) = self.receipts[track][step] else {
                return Err(GridRequestError::UntrackedActiveCell);
            };
            self.pending = Pending::RemoveRequested { cell, receipt };
        } else {
            let request =
                StockNoteRequest::from_grid_step(cell.physical_pad, cell.step, DEFAULT_VELOCITY)
                    .map_err(|_| GridRequestError::InvalidCell)?;
            self.pending = Pending::InsertRequested { cell, request };
        }
        Ok(cell)
    }

    /// Advance at most one native boundary from one verified UI callback.
    ///
    /// # Errors
    ///
    /// Returns a native adapter error or ambiguous-identity error and makes
    /// that cell terminal without changing its committed UI bit.
    pub fn drive<P>(
        &mut self,
        port: &mut P,
        ui: &mut UiState,
    ) -> Result<GridEditProgress, GridEditError<P::Error>>
    where
        P: GridEditPort<Receipt = R>,
    {
        match self.pending {
            Pending::Idle => Ok(GridEditProgress::Waiting),
            Pending::InsertRequested { cell, request } => match port.insert(request) {
                Ok(receipt) => {
                    self.pending = Pending::InsertAccepted { cell, receipt };
                    Ok(GridEditProgress::InsertCommandAccepted)
                }
                Err(error) => self.fail(cell, GridEditError::Native(error)),
            },
            Pending::InsertAccepted { cell, mut receipt } => match port.observe(&mut receipt) {
                Ok((NativeEventPresence::Absent, _)) => {
                    self.pending = Pending::InsertAccepted { cell, receipt };
                    Ok(GridEditProgress::Waiting)
                }
                Ok((NativeEventPresence::Unique, _)) => {
                    self.receipts[usize::from(cell.physical_pad)][usize::from(cell.step)] =
                        Some(receipt);
                    let _ = ui.commit_step(cell.physical_pad, cell.step, true);
                    self.pending = Pending::Idle;
                    Ok(GridEditProgress::InsertCommitted)
                }
                Ok((NativeEventPresence::Ambiguous, _)) => {
                    self.fail(cell, GridEditError::AmbiguousEvent)
                }
                Err(error) => self.fail(cell, GridEditError::Native(error)),
            },
            Pending::RemoveRequested { cell, receipt } => match port.remove(&receipt) {
                Ok(()) => {
                    self.pending = Pending::RemoveAccepted { cell, receipt };
                    Ok(GridEditProgress::RemoveCommandAccepted)
                }
                Err(error) => self.fail(cell, GridEditError::Native(error)),
            },
            Pending::RemoveAccepted { cell, mut receipt } => match port.observe(&mut receipt) {
                Ok((NativeEventPresence::Unique, _)) => {
                    self.pending = Pending::RemoveAccepted { cell, receipt };
                    Ok(GridEditProgress::Waiting)
                }
                Ok((NativeEventPresence::Absent, _)) => {
                    self.receipts[usize::from(cell.physical_pad)][usize::from(cell.step)] = None;
                    let _ = ui.commit_step(cell.physical_pad, cell.step, false);
                    self.pending = Pending::Idle;
                    Ok(GridEditProgress::RemoveCommitted)
                }
                Ok((NativeEventPresence::Ambiguous, _)) => {
                    self.fail(cell, GridEditError::AmbiguousEvent)
                }
                Err(error) => self.fail(cell, GridEditError::Native(error)),
            },
            Pending::Failed(_) => Err(GridEditError::Terminal),
        }
    }

    fn fail<E>(
        &mut self,
        cell: GridCell,
        error: GridEditError<E>,
    ) -> Result<GridEditProgress, GridEditError<E>> {
        self.pending = Pending::Failed(cell);
        Err(error)
    }
}
