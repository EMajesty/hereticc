//! Feature-gated first native insertion experiment.
//!
//! The HIL worker changes only atomics. Native work happens synchronously from
//! the stock Step-screen paint callback after its stock painter returns.

use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Mutex, PoisonError};

use crate::native_stock_sequence::{NativeEditReceipt, NativeStockSequencePort};
use crate::stock_sequence::{StockNoteRequest, StockSequenceEditPort};

const STEP_SCREEN_VTABLE: usize = 0x06e2_4f38;
const STEP_PAINT_SLOT: usize = STEP_SCREEN_VTABLE + 0xd8;
const STOCK_STEP_PAINT: usize = 0x010e_2e10;
const PAGE_SIZE: usize = 4096;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Unavailable,
    Idle,
    Armed,
    Accepted,
    Observed,
    RemoveRequested,
    Removed,
    Rejected,
    Failed,
}

static STATUS: AtomicU8 = AtomicU8::new(Status::Unavailable as u8);
static ARMED: AtomicBool = AtomicBool::new(false);
static REMOVE: AtomicBool = AtomicBool::new(false);
static RECEIPT: Mutex<Option<NativeEditReceipt>> = Mutex::new(None);

#[cfg(target_arch = "aarch64")]
unsafe extern "C" {
    fn neurogenic_m04a_step_paint_bridge(screen: usize, graphics: usize);
}

#[cfg(not(target_arch = "aarch64"))]
unsafe extern "C" fn neurogenic_m04a_step_paint_bridge(screen: usize, graphics: usize) {
    unsafe { neurogenic_m04a_step_paint(screen, graphics) };
}

pub fn status() -> Status {
    match STATUS.load(Ordering::Acquire) {
        1 => Status::Idle,
        2 => Status::Armed,
        3 => Status::Accepted,
        4 => Status::Observed,
        5 => Status::RemoveRequested,
        6 => Status::Removed,
        7 => Status::Rejected,
        8 => Status::Failed,
        _ => Status::Unavailable,
    }
}

pub fn arm() -> bool {
    if status() != Status::Idle {
        return false;
    }
    ARMED.store(true, Ordering::Release);
    STATUS.store(Status::Armed as u8, Ordering::Release);
    true
}

pub fn request_remove() -> bool {
    if !matches!(status(), Status::Accepted | Status::Observed) {
        return false;
    }
    REMOVE.store(true, Ordering::Release);
    STATUS.store(Status::RemoveRequested as u8, Ordering::Release);
    true
}

pub fn install() -> bool {
    let base = mpc_sample_sdk::image::base();
    if base == 0 {
        return false;
    }
    let slot = base + STEP_PAINT_SLOT;
    if mpc_sample_sdk::image::read_pointer(slot) != Some(base + STOCK_STEP_PAINT) {
        return false;
    }
    let page = slot & !(PAGE_SIZE - 1);
    // SAFETY: this is the verified Step-screen vtable page. The write is one
    // aligned pointer and the page is returned read-only before publication.
    if unsafe {
        libc::mprotect(
            page as *mut libc::c_void,
            PAGE_SIZE,
            libc::PROT_READ | libc::PROT_WRITE,
        )
    } != 0
    {
        return false;
    }
    let replacement = neurogenic_m04a_step_paint_bridge as *const () as usize;
    let wrote = unsafe { mpc_sample_sdk::image::write(slot, &replacement.to_ne_bytes()) };
    let sealed =
        unsafe { libc::mprotect(page as *mut libc::c_void, PAGE_SIZE, libc::PROT_READ) } == 0;
    if !wrote || !sealed || mpc_sample_sdk::image::read_pointer(slot) != Some(replacement) {
        // Best-effort rollback of this extension's one slot. A process restart
        // remains authoritative recovery if page protection itself failed.
        let _ = unsafe {
            libc::mprotect(
                page as *mut libc::c_void,
                PAGE_SIZE,
                libc::PROT_READ | libc::PROT_WRITE,
            )
        };
        let original = base + STOCK_STEP_PAINT;
        let _ = unsafe { mpc_sample_sdk::image::write(slot, &original.to_ne_bytes()) };
        let _ = unsafe { libc::mprotect(page as *mut libc::c_void, PAGE_SIZE, libc::PROT_READ) };
        return false;
    }
    STATUS.store(Status::Idle as u8, Ordering::Release);
    true
}

/// Feature-gated Step-screen vtable target. Installed only after exact slot
/// validation by [`install`].
///
/// # Safety
///
/// Called only by the stock firmware with its live Step screen and graphics
/// objects on the owning UI thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn neurogenic_m04a_step_paint(screen: usize, graphics: usize) {
    let original: unsafe extern "C" fn(usize, usize) =
        unsafe { core::mem::transmute(mpc_sample_sdk::image::base() + STOCK_STEP_PAINT) };
    unsafe { original(screen, graphics) };

    let mut receipt = RECEIPT.lock().unwrap_or_else(PoisonError::into_inner);
    let mut port = unsafe { NativeStockSequencePort::from_step_paint(screen) };
    if REMOVE.swap(false, Ordering::AcqRel) {
        if let Some(existing) = receipt.as_ref() {
            if port.remove(existing).is_ok() {
                *receipt = None;
                STATUS.store(Status::Removed as u8, Ordering::Release);
            } else {
                STATUS.store(Status::Failed as u8, Ordering::Release);
            }
        }
        return;
    }
    if let Some(existing) = receipt.as_ref() {
        match port.contains(existing) {
            Ok(true) => STATUS.store(Status::Observed as u8, Ordering::Release),
            Ok(false) => {}
            Err(_) => STATUS.store(Status::Failed as u8, Ordering::Release),
        }
        return;
    }
    if !ARMED.swap(false, Ordering::AcqRel) {
        return;
    }
    let Ok(request) = StockNoteRequest::from_grid_step(0, 0, 100) else {
        STATUS.store(Status::Rejected as u8, Ordering::Release);
        return;
    };
    match port.insert_current_track(request) {
        Ok(accepted) => {
            *receipt = Some(accepted);
            STATUS.store(Status::Accepted as u8, Ordering::Release);
        }
        Err(_) => STATUS.store(Status::Failed as u8, Ordering::Release),
    }
}
