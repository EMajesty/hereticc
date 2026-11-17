//! Firmware-1.3.0.12 stock Step-editor command adapter.
//!
//! The adapter is callback-scoped: construction is unsafe and valid only while
//! the stock `StepEditScreen` paint callback owns `screen` on its UI thread.
//! No firmware pointer appears in a request or receipt, and the adapter cannot
//! outlive that callback borrow.

use core::marker::PhantomData;

use crate::stock_sequence::{StockNoteRequest, StockSequenceEditPort};

/// Why the native edit was not accepted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeEditError {
    /// The visible Step screen/controller/current-sequence graph was unavailable.
    ContextUnavailable,
    /// The request or current sequence bounds rejected the edit.
    Rejected,
    /// Construction, native allocation, command creation, or dispatch failed.
    NativeFailure,
    /// This build is not the `AArch64` MPC target.
    UnsupportedTarget,
}

/// Pointer-free receipt for one accepted native add command.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeEditReceipt {
    event: [u8; 56],
    sequence_length: i64,
    note: u32,
}

impl NativeEditReceipt {
    /// Resolved stock note number recorded by the native constructor path.
    #[must_use]
    pub const fn note(&self) -> u32 {
        self.note
    }

    /// Sequence length revalidation token used by the removal path.
    #[must_use]
    pub const fn sequence_length(&self) -> i64 {
        self.sequence_length
    }
}

/// A non-retaining edit port borrowed from one live Step-screen UI callback.
pub struct NativeStockSequencePort<'callback> {
    screen: usize,
    _callback: PhantomData<&'callback mut ()>,
}

impl NativeStockSequencePort<'_> {
    /// Bind one live stock Step-editor paint callback.
    ///
    /// # Safety
    ///
    /// `screen` must be the callback's live firmware-1.3.0.12
    /// `StepEditScreen`, on its owning UI thread, for the complete lifetime
    /// `'callback`. The value must not be cached or used from any worker.
    #[must_use]
    pub const unsafe fn from_step_paint(screen: usize) -> Self {
        Self {
            screen,
            _callback: PhantomData,
        }
    }

    /// Submit the matching native remove command while the same callback
    /// context is live. This is the bounded rollback path for the first test.
    ///
    /// # Errors
    ///
    /// Fails closed when the Step context or sequence changed, or native command
    /// construction/dispatch failed.
    pub fn remove(&mut self, receipt: &NativeEditReceipt) -> Result<(), NativeEditError> {
        backend::remove(self.screen, receipt)
    }

    /// Check whether the exact accepted event is present in the same current
    /// event list. This is observation C; command acceptance alone is not.
    ///
    /// # Errors
    ///
    /// Fails closed when the callback context changed or the bounded event-list
    /// traversal was malformed or exceeded its budget.
    pub fn contains(&mut self, receipt: &NativeEditReceipt) -> Result<bool, NativeEditError> {
        backend::contains(self.screen, receipt)
    }
}

impl StockSequenceEditPort for NativeStockSequencePort<'_> {
    type Receipt = NativeEditReceipt;
    type Error = NativeEditError;

    fn insert_current_track(
        &mut self,
        request: StockNoteRequest,
    ) -> Result<Self::Receipt, Self::Error> {
        backend::insert(self.screen, request)
    }
}

#[cfg(all(target_arch = "aarch64", feature = "native-sequence-adapter"))]
mod backend {
    use super::{NativeEditError, NativeEditReceipt};
    use crate::stock_sequence::StockNoteRequest;

    #[repr(C)]
    struct NativeRequest {
        physical_pad: u8,
        velocity: u8,
        reserved: u16,
        tick: i64,
        duration: i64,
    }

    #[repr(C)]
    struct NativeReceipt {
        event: [u8; 56],
        sequence_length: i64,
        note: u32,
        reserved: u32,
    }

    unsafe extern "C" {
        fn neurogenic_stock_sequence_insert(
            base: usize,
            screen: usize,
            request: *const NativeRequest,
            receipt: *mut NativeReceipt,
        ) -> i32;
        fn neurogenic_stock_sequence_remove(
            base: usize,
            screen: usize,
            receipt: *const NativeReceipt,
        ) -> i32;
        fn neurogenic_stock_sequence_contains(
            base: usize,
            screen: usize,
            receipt: *const NativeReceipt,
        ) -> i32;
    }

    /// Link-time anchor used by the M0.4a artifact inspector. It exposes no
    /// operation and exists only to keep the reviewed native shim in the ELF.
    #[unsafe(no_mangle)]
    pub extern "C" fn neurogenic_m04a_native_adapter_link_anchor() -> usize {
        neurogenic_stock_sequence_insert as *const () as usize
    }

    fn error(status: i32) -> NativeEditError {
        match status {
            1 => NativeEditError::ContextUnavailable,
            2 => NativeEditError::Rejected,
            _ => NativeEditError::NativeFailure,
        }
    }

    pub(super) fn insert(
        screen: usize,
        request: StockNoteRequest,
    ) -> Result<NativeEditReceipt, NativeEditError> {
        let native = NativeRequest {
            physical_pad: request.physical_pad(),
            velocity: request.velocity(),
            reserved: 0,
            tick: request.tick(),
            duration: request.duration(),
        };
        let mut receipt = NativeReceipt {
            event: [0; 56],
            sequence_length: 0,
            note: 0,
            reserved: 0,
        };
        // SAFETY: the port constructor's contract supplies the live callback;
        // the C++ shim owns validation, native temporaries, and exception cleanup.
        let status = unsafe {
            neurogenic_stock_sequence_insert(
                mpc_sample_sdk::image::base(),
                screen,
                &native,
                &mut receipt,
            )
        };
        if status != 0 {
            return Err(error(status));
        }
        Ok(NativeEditReceipt {
            event: receipt.event,
            sequence_length: receipt.sequence_length,
            note: receipt.note,
        })
    }

    pub(super) fn remove(
        screen: usize,
        receipt: &NativeEditReceipt,
    ) -> Result<(), NativeEditError> {
        let native = NativeReceipt {
            event: receipt.event,
            sequence_length: receipt.sequence_length,
            note: receipt.note,
            reserved: 0,
        };
        // SAFETY: same callback-scoped contract as insertion.
        let status = unsafe {
            neurogenic_stock_sequence_remove(mpc_sample_sdk::image::base(), screen, &native)
        };
        if status == 0 {
            Ok(())
        } else {
            Err(error(status))
        }
    }

    pub(super) fn contains(
        screen: usize,
        receipt: &NativeEditReceipt,
    ) -> Result<bool, NativeEditError> {
        let native = NativeReceipt {
            event: receipt.event,
            sequence_length: receipt.sequence_length,
            note: receipt.note,
            reserved: 0,
        };
        // SAFETY: same callback-scoped contract as insertion.
        let status = unsafe {
            neurogenic_stock_sequence_contains(mpc_sample_sdk::image::base(), screen, &native)
        };
        match status {
            1 => Ok(true),
            0 => Ok(false),
            value => Err(error(-value)),
        }
    }
}

#[cfg(not(all(target_arch = "aarch64", feature = "native-sequence-adapter")))]
mod backend {
    use super::{NativeEditError, NativeEditReceipt};
    use crate::stock_sequence::StockNoteRequest;
    pub(super) fn insert(
        _: usize,
        _: StockNoteRequest,
    ) -> Result<NativeEditReceipt, NativeEditError> {
        Err(NativeEditError::UnsupportedTarget)
    }
    pub(super) fn remove(_: usize, _: &NativeEditReceipt) -> Result<(), NativeEditError> {
        Err(NativeEditError::UnsupportedTarget)
    }
    pub(super) fn contains(_: usize, _: &NativeEditReceipt) -> Result<bool, NativeEditError> {
        Err(NativeEditError::UnsupportedTarget)
    }
}
