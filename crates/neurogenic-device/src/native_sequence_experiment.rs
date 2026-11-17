//! Feature-gated first native insertion experiment.
//!
//! The HIL worker changes only atomics. Native work happens synchronously from
//! the stock Step-screen paint callback after its stock painter returns.

use std::sync::atomic::{AtomicBool, AtomicU8, AtomicU64, Ordering};
use std::sync::{Mutex, PoisonError};

use crate::native_stock_sequence::{
    NativeContextIdentity, NativeEditReceipt, NativeStockSequencePort,
};
use crate::stock_sequence::{StockNoteRequest, StockSequenceEditPort};

const STEP_SCREEN_VTABLE: usize = 0x06e2_4f38;
const STEP_PAINT_SLOT: usize = STEP_SCREEN_VTABLE + 0xd8;
const STOCK_STEP_PAINT: usize = 0x010e_2e10;
const PAGE_SIZE: usize = 4096;
const CONTEXT_LEASE_NS: u64 = 2_000_000_000;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    Unavailable,
    Idle,
    Armed,
    Accepted,
    Observed,
    RemoveRequested,
    RemoveAccepted,
    Removed,
    Rejected,
    Expired,
    Failed,
}

static STATUS: AtomicU8 = AtomicU8::new(Status::Unavailable as u8);
static ARMED: AtomicBool = AtomicBool::new(false);
static REMOVE: AtomicBool = AtomicBool::new(false);
static IN_CALLBACK: AtomicBool = AtomicBool::new(false);
static LAST_CONTEXT_NS: AtomicU64 = AtomicU64::new(0);
static ARM_DEADLINE_NS: AtomicU64 = AtomicU64::new(0);
static REMOVE_DEADLINE_NS: AtomicU64 = AtomicU64::new(0);
static COMMAND_ACCEPTED: AtomicBool = AtomicBool::new(false);
static EVENT_OBSERVED: AtomicBool = AtomicBool::new(false);
static EVENT_REMOVED: AtomicBool = AtomicBool::new(false);
static RECEIPT: Mutex<Option<NativeEditReceipt>> = Mutex::new(None);
static CURRENT_CONTEXT: Mutex<Option<NativeContextIdentity>> = Mutex::new(None);
static ARMED_CONTEXT: Mutex<Option<NativeContextIdentity>> = Mutex::new(None);
static UI_THREAD: Mutex<Option<libc::pthread_t>> = Mutex::new(None);

struct OperationGuard<'a>(&'a AtomicBool);

impl<'a> OperationGuard<'a> {
    fn enter(flag: &'a AtomicBool) -> Option<Self> {
        flag.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .ok()
            .map(|_| Self(flag))
    }
}

impl Drop for OperationGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
    }
}

fn monotonic_ns() -> Option<u64> {
    let mut value = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    // SAFETY: `value` is writable and CLOCK_MONOTONIC has no external input.
    if unsafe { libc::clock_gettime(libc::CLOCK_MONOTONIC, &raw mut value) } != 0
        || value.tv_sec < 0
        || value.tv_nsec < 0
    {
        return None;
    }
    u64::try_from(value.tv_sec)
        .ok()?
        .checked_mul(1_000_000_000)?
        .checked_add(u64::try_from(value.tv_nsec).ok()?)
}

const fn within_deadline(now: u64, deadline: u64) -> bool {
    now <= deadline
}

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
        6 => Status::RemoveAccepted,
        7 => Status::Removed,
        8 => Status::Rejected,
        9 => Status::Expired,
        10 => Status::Failed,
        _ => Status::Unavailable,
    }
}

pub fn context_ready() -> bool {
    let Some(now) = monotonic_ns() else {
        return false;
    };
    let last = LAST_CONTEXT_NS.load(Ordering::Acquire);
    last != 0 && now.saturating_sub(last) <= CONTEXT_LEASE_NS
}

pub fn command_accepted() -> bool {
    COMMAND_ACCEPTED.load(Ordering::Acquire)
}

pub fn event_observed() -> bool {
    EVENT_OBSERVED.load(Ordering::Acquire)
}

pub fn event_removed() -> bool {
    EVENT_REMOVED.load(Ordering::Acquire)
}

pub fn arm() -> bool {
    let Some(now) = monotonic_ns() else {
        return false;
    };
    if !context_ready()
        || STATUS
            .compare_exchange(
                Status::Idle as u8,
                Status::Armed as u8,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_err()
    {
        return false;
    }
    let Some(deadline) = now.checked_add(CONTEXT_LEASE_NS) else {
        STATUS.store(Status::Failed as u8, Ordering::Release);
        return false;
    };
    let current = *CURRENT_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner);
    let Some(current) = current else {
        STATUS.store(Status::Failed as u8, Ordering::Release);
        return false;
    };
    *ARMED_CONTEXT.lock().unwrap_or_else(PoisonError::into_inner) = Some(current);
    ARM_DEADLINE_NS.store(deadline, Ordering::Release);
    ARMED.store(true, Ordering::Release);
    true
}

pub fn request_remove() -> bool {
    let Some(now) = monotonic_ns() else {
        return false;
    };
    let Some(deadline) = now.checked_add(CONTEXT_LEASE_NS) else {
        return false;
    };
    if !context_ready()
        || STATUS
            .compare_exchange(
                Status::Observed as u8,
                Status::RemoveRequested as u8,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_err()
    {
        return false;
    }
    REMOVE_DEADLINE_NS.store(deadline, Ordering::Release);
    REMOVE.store(true, Ordering::Release);
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
        let rollback_writable = unsafe {
            libc::mprotect(
                page as *mut libc::c_void,
                PAGE_SIZE,
                libc::PROT_READ | libc::PROT_WRITE,
            )
        } == 0;
        if !rollback_writable {
            unsafe { libc::_exit(121) };
        }
        let original = base + STOCK_STEP_PAINT;
        if !unsafe { mpc_sample_sdk::image::write(slot, &original.to_ne_bytes()) } {
            unsafe { libc::_exit(121) };
        }
        let restored =
            unsafe { libc::mprotect(page as *mut libc::c_void, PAGE_SIZE, libc::PROT_READ) } == 0;
        if !restored {
            // Continuing with a writable firmware vtable page is less safe
            // than terminating this transient stock process. HIL restoration
            // remains responsible for removing the preload before restart.
            unsafe { libc::_exit(121) };
        }
        return false;
    }
    ARMED.store(false, Ordering::Release);
    REMOVE.store(false, Ordering::Release);
    LAST_CONTEXT_NS.store(0, Ordering::Release);
    ARM_DEADLINE_NS.store(0, Ordering::Release);
    REMOVE_DEADLINE_NS.store(0, Ordering::Release);
    COMMAND_ACCEPTED.store(false, Ordering::Release);
    EVENT_OBSERVED.store(false, Ordering::Release);
    EVENT_REMOVED.store(false, Ordering::Release);
    *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner) = None;
    *CURRENT_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner) = None;
    *ARMED_CONTEXT.lock().unwrap_or_else(PoisonError::into_inner) = None;
    *UI_THREAD.lock().unwrap_or_else(PoisonError::into_inner) = None;
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

    let Some(_operation) = OperationGuard::enter(&IN_CALLBACK) else {
        return;
    };
    // SAFETY: pthread_self/equal do not dereference firmware state. The first
    // stock paint establishes the owner; every later native action must run on
    // that same thread.
    let thread = unsafe { libc::pthread_self() };
    let mut owner = UI_THREAD.lock().unwrap_or_else(PoisonError::into_inner);
    if let Some(expected) = *owner {
        if unsafe { libc::pthread_equal(expected, thread) } == 0 {
            STATUS.store(Status::Failed as u8, Ordering::Release);
            return;
        }
    } else {
        *owner = Some(thread);
    }
    drop(owner);
    let mut port = unsafe { NativeStockSequencePort::from_step_paint(screen) };
    let Ok(current_context) = port.context_identity() else {
        return;
    };
    let Some(now) = monotonic_ns() else {
        STATUS.store(Status::Failed as u8, Ordering::Release);
        return;
    };
    LAST_CONTEXT_NS.store(now, Ordering::Release);
    *CURRENT_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner) = Some(current_context);

    if REMOVE.swap(false, Ordering::AcqRel) {
        if !within_deadline(now, REMOVE_DEADLINE_NS.load(Ordering::Acquire)) {
            STATUS.store(Status::Expired as u8, Ordering::Release);
            return;
        }
        let existing = *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner);
        if let Some(existing) = existing.as_ref() {
            if port.remove(existing).is_ok() {
                STATUS.store(Status::RemoveAccepted as u8, Ordering::Release);
            } else {
                STATUS.store(Status::Failed as u8, Ordering::Release);
            }
        }
        return;
    }

    let existing = *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner);
    if let Some(existing) = existing.as_ref() {
        match port.contains(existing) {
            Ok(true) if matches!(status(), Status::RemoveRequested | Status::RemoveAccepted) => {}
            Ok(true) => {
                EVENT_OBSERVED.store(true, Ordering::Release);
                STATUS.store(Status::Observed as u8, Ordering::Release);
            }
            Ok(false) if status() == Status::RemoveAccepted => {
                *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner) = None;
                EVENT_REMOVED.store(true, Ordering::Release);
                STATUS.store(Status::Removed as u8, Ordering::Release);
            }
            Ok(false) => {}
            Err(_) => STATUS.store(Status::Failed as u8, Ordering::Release),
        }
        return;
    }
    if !ARMED.swap(false, Ordering::AcqRel) {
        return;
    }
    if !within_deadline(now, ARM_DEADLINE_NS.load(Ordering::Acquire)) {
        STATUS.store(Status::Expired as u8, Ordering::Release);
        return;
    }
    let armed_context = ARMED_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
        .take();
    if armed_context != Some(current_context) {
        STATUS.store(Status::Rejected as u8, Ordering::Release);
        return;
    }
    let Ok(request) = StockNoteRequest::from_grid_step(0, 0, 100) else {
        STATUS.store(Status::Rejected as u8, Ordering::Release);
        return;
    };
    match port.insert_current_track(request) {
        Ok(accepted) => {
            *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner) = Some(accepted);
            COMMAND_ACCEPTED.store(true, Ordering::Release);
            STATUS.store(Status::Accepted as u8, Ordering::Release);
        }
        Err(_) => STATUS.store(Status::Failed as u8, Ordering::Release),
    }
}

#[cfg(test)]
mod tests {
    use super::{AtomicBool, NativeContextIdentity, OperationGuard, Ordering, within_deadline};

    #[test]
    fn callback_gate_is_nonblocking_and_released_on_scope_exit() {
        let flag = AtomicBool::new(false);
        let first = OperationGuard::enter(&flag).expect("first callback owns gate");
        assert!(OperationGuard::enter(&flag).is_none());
        drop(first);
        assert!(OperationGuard::enter(&flag).is_some());
        assert!(!flag.load(Ordering::Relaxed));
    }

    #[test]
    fn armed_context_identity_rejects_sequence_or_track_change() {
        let armed = NativeContextIdentity::fixture([1, 2]);
        let same = NativeContextIdentity::fixture([1, 2]);
        let changed_track = NativeContextIdentity::fixture([1, 9]);
        assert_eq!(Some(armed), Some(same));
        assert_ne!(Some(armed), Some(changed_track));
    }

    #[test]
    fn native_operation_deadlines_are_inclusive() {
        assert!(within_deadline(9, 10));
        assert!(within_deadline(10, 10));
        assert!(!within_deadline(11, 10));
    }
}
