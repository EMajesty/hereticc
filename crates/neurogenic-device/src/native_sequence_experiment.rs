//! Feature-gated first native insertion experiment.
//!
//! The HIL worker changes only atomics. Native work happens synchronously from
//! the stock Step-screen paint callback after its stock painter returns.

use std::sync::atomic::{AtomicBool, AtomicU8, AtomicU32, AtomicU64, Ordering};
use std::sync::{Mutex, PoisonError};

use crate::native_stock_sequence::{
    NativeContextIdentity, NativeEditReceipt, NativeEventPresence, NativeScanEvidence,
    NativeStockSequencePort,
};
use crate::stock_sequence::{StockNoteRequest, StockSequenceEditPort};

const STEP_SCREEN_VTABLE: usize = 0x06e2_4f38;
const STEP_PAINT_SLOT: usize = STEP_SCREEN_VTABLE + 0xd8;
const STOCK_STEP_PAINT: usize = 0x010e_2e10;
const PAGE_SIZE: usize = 4096;
const CONTEXT_LEASE_NS: u64 = 2_000_000_000;
// A human must request one stock Step repaint after arming. Context identity is
// revalidated in that callback, so this longer bound permits the deliberate
// input without moving firmware work onto the mailbox thread.
const OPERATION_LEASE_NS: u64 = 10_000_000_000;

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
    Inserting,
    Removing,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Diagnostic {
    NotReached,
    WaitingForPaint,
    PaintEntered,
    ExpiredBeforePaint,
    ExpiredAfterPaint,
    ContextUnavailable,
    ContextChanged,
    WrongThread,
    RequestRejected,
    NativeFailure,
    CommandAccepted,
    EventObserved,
    RemoveAccepted,
    EventRemoved,
    AmbiguousEvent,
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
static PAINT_GENERATION: AtomicU64 = AtomicU64::new(0);
static ARM_PAINT_GENERATION: AtomicU64 = AtomicU64::new(0);
static DIAGNOSTIC: AtomicU8 = AtomicU8::new(Diagnostic::NotReached as u8);
static SCAN_ATTEMPTS: AtomicU32 = AtomicU32::new(0);
static SCAN_TRAVERSED: AtomicU32 = AtomicU32::new(0);
static SCAN_TAG3: AtomicU32 = AtomicU32::new(0);
static SCAN_TICK: AtomicU32 = AtomicU32::new(0);
static SCAN_NOTE: AtomicU32 = AtomicU32::new(0);
static SCAN_NATIVE: AtomicU32 = AtomicU32::new(0);
static SCAN_BYTES: AtomicU32 = AtomicU32::new(0);
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

const fn expiry_diagnostic(paint_generation: u64, armed_generation: u64) -> Diagnostic {
    if paint_generation > armed_generation {
        Diagnostic::ExpiredAfterPaint
    } else {
        Diagnostic::ExpiredBeforePaint
    }
}

fn claim_status(status: &AtomicU8, expected: Status, claimed: Status) -> bool {
    status
        .compare_exchange(
            expected as u8,
            claimed as u8,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
        .is_ok()
}

fn set_state(status: Status, diagnostic: Diagnostic) {
    DIAGNOSTIC.store(diagnostic as u8, Ordering::Release);
    STATUS.store(status as u8, Ordering::Release);
}

#[cfg(target_arch = "aarch64")]
unsafe extern "C" {
    fn neurogenic_m04a_step_paint_bridge(screen: usize, graphics: usize);
}

#[cfg(not(target_arch = "aarch64"))]
unsafe extern "C" fn neurogenic_m04a_step_paint_bridge(screen: usize, graphics: usize) {
    unsafe { neurogenic_m04a_step_paint(screen, graphics) };
}

fn raw_status() -> Status {
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
        11 => Status::Inserting,
        12 => Status::Removing,
        _ => Status::Unavailable,
    }
}

pub fn status() -> Status {
    let current = raw_status();
    let deadline = match current {
        Status::Armed => ARM_DEADLINE_NS.load(Ordering::Acquire),
        Status::RemoveRequested => REMOVE_DEADLINE_NS.load(Ordering::Acquire),
        _ => return current,
    };
    let Some(now) = monotonic_ns() else {
        return current;
    };
    if deadline != 0
        && !within_deadline(now, deadline)
        && STATUS
            .compare_exchange(
                current as u8,
                Status::Expired as u8,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .is_ok()
    {
        ARMED.store(false, Ordering::Release);
        REMOVE.store(false, Ordering::Release);
        DIAGNOSTIC.store(
            expiry_diagnostic(
                PAINT_GENERATION.load(Ordering::Acquire),
                ARM_PAINT_GENERATION.load(Ordering::Acquire),
            ) as u8,
            Ordering::Release,
        );
        return Status::Expired;
    }
    raw_status()
}

pub fn diagnostic() -> Diagnostic {
    match DIAGNOSTIC.load(Ordering::Acquire) {
        1 => Diagnostic::WaitingForPaint,
        2 => Diagnostic::PaintEntered,
        3 => Diagnostic::ExpiredBeforePaint,
        4 => Diagnostic::ExpiredAfterPaint,
        5 => Diagnostic::ContextUnavailable,
        6 => Diagnostic::ContextChanged,
        7 => Diagnostic::WrongThread,
        8 => Diagnostic::RequestRejected,
        9 => Diagnostic::NativeFailure,
        10 => Diagnostic::CommandAccepted,
        11 => Diagnostic::EventObserved,
        12 => Diagnostic::RemoveAccepted,
        13 => Diagnostic::EventRemoved,
        14 => Diagnostic::AmbiguousEvent,
        _ => Diagnostic::NotReached,
    }
}

/// Latest bounded, pointer-free event-list scan evidence.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ScanEvidence {
    pub attempts: u32,
    pub traversed: u32,
    pub tag3: u32,
    pub tick_matches: u32,
    pub note_matches: u32,
    pub native_matches: u32,
    pub byte_matches: u32,
}

pub fn scan_evidence() -> ScanEvidence {
    ScanEvidence {
        attempts: SCAN_ATTEMPTS.load(Ordering::Acquire),
        traversed: SCAN_TRAVERSED.load(Ordering::Acquire),
        tag3: SCAN_TAG3.load(Ordering::Acquire),
        tick_matches: SCAN_TICK.load(Ordering::Acquire),
        note_matches: SCAN_NOTE.load(Ordering::Acquire),
        native_matches: SCAN_NATIVE.load(Ordering::Acquire),
        byte_matches: SCAN_BYTES.load(Ordering::Acquire),
    }
}

fn publish_scan(scan: NativeScanEvidence) {
    SCAN_ATTEMPTS.fetch_add(1, Ordering::AcqRel);
    SCAN_TRAVERSED.store(scan.traversed, Ordering::Release);
    SCAN_TAG3.store(scan.tag3, Ordering::Release);
    SCAN_TICK.store(scan.tick_matches, Ordering::Release);
    SCAN_NOTE.store(scan.note_matches, Ordering::Release);
    SCAN_NATIVE.store(scan.native_matches, Ordering::Release);
    SCAN_BYTES.store(scan.byte_matches, Ordering::Release);
}

pub fn paint_generation() -> u64 {
    PAINT_GENERATION.load(Ordering::Acquire)
}

pub fn paint_after_arm() -> bool {
    paint_generation() > ARM_PAINT_GENERATION.load(Ordering::Acquire)
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
    let Some(deadline) = now.checked_add(OPERATION_LEASE_NS) else {
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
    for counter in [
        &SCAN_ATTEMPTS,
        &SCAN_TRAVERSED,
        &SCAN_TAG3,
        &SCAN_TICK,
        &SCAN_NOTE,
        &SCAN_NATIVE,
        &SCAN_BYTES,
    ] {
        counter.store(0, Ordering::Release);
    }
    ARM_PAINT_GENERATION.store(paint_generation(), Ordering::Release);
    ARM_DEADLINE_NS.store(deadline, Ordering::Release);
    DIAGNOSTIC.store(Diagnostic::WaitingForPaint as u8, Ordering::Release);
    ARMED.store(true, Ordering::Release);
    true
}

pub fn request_remove() -> bool {
    let Some(now) = monotonic_ns() else {
        return false;
    };
    let Some(deadline) = now.checked_add(OPERATION_LEASE_NS) else {
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
    PAINT_GENERATION.store(0, Ordering::Release);
    ARM_PAINT_GENERATION.store(0, Ordering::Release);
    DIAGNOSTIC.store(Diagnostic::NotReached as u8, Ordering::Release);
    *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner) = None;
    *CURRENT_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner) = None;
    *ARMED_CONTEXT.lock().unwrap_or_else(PoisonError::into_inner) = None;
    *UI_THREAD.lock().unwrap_or_else(PoisonError::into_inner) = None;
    STATUS.store(Status::Idle as u8, Ordering::Release);
    true
}

fn insert_one(port: &mut NativeStockSequencePort<'_>) {
    let Ok(request) = StockNoteRequest::from_grid_step(0, 0, 100) else {
        DIAGNOSTIC.store(Diagnostic::RequestRejected as u8, Ordering::Release);
        STATUS.store(Status::Rejected as u8, Ordering::Release);
        return;
    };
    match port.insert_current_track(request) {
        Ok(accepted) => {
            *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner) = Some(accepted);
            COMMAND_ACCEPTED.store(true, Ordering::Release);
            DIAGNOSTIC.store(Diagnostic::CommandAccepted as u8, Ordering::Release);
            STATUS.store(Status::Accepted as u8, Ordering::Release);
        }
        Err(crate::native_stock_sequence::NativeEditError::ContextUnavailable) => {
            DIAGNOSTIC.store(Diagnostic::ContextUnavailable as u8, Ordering::Release);
            STATUS.store(Status::Failed as u8, Ordering::Release);
        }
        Err(crate::native_stock_sequence::NativeEditError::Rejected) => {
            DIAGNOSTIC.store(Diagnostic::RequestRejected as u8, Ordering::Release);
            STATUS.store(Status::Rejected as u8, Ordering::Release);
        }
        Err(_) => {
            DIAGNOSTIC.store(Diagnostic::NativeFailure as u8, Ordering::Release);
            STATUS.store(Status::Failed as u8, Ordering::Release);
        }
    }
}

fn observe_existing(port: &mut NativeStockSequencePort<'_>) -> bool {
    let mut receipt = RECEIPT.lock().unwrap_or_else(PoisonError::into_inner);
    let Some(existing) = receipt.as_mut() else {
        return false;
    };
    match port.observe(existing) {
        Ok((NativeEventPresence::Unique, scan))
            if matches!(status(), Status::RemoveRequested | Status::RemoveAccepted) =>
        {
            publish_scan(scan);
        }
        Ok((NativeEventPresence::Unique, scan)) => {
            publish_scan(scan);
            EVENT_OBSERVED.store(true, Ordering::Release);
            set_state(Status::Observed, Diagnostic::EventObserved);
        }
        Ok((NativeEventPresence::Absent, scan)) if status() == Status::RemoveAccepted => {
            publish_scan(scan);
            *receipt = None;
            EVENT_REMOVED.store(true, Ordering::Release);
            set_state(Status::Removed, Diagnostic::EventRemoved);
        }
        Ok((NativeEventPresence::Absent, scan)) => publish_scan(scan),
        Ok((NativeEventPresence::Ambiguous, scan)) => {
            publish_scan(scan);
            set_state(Status::Failed, Diagnostic::AmbiguousEvent);
        }
        Err(_) => set_state(Status::Failed, Diagnostic::NativeFailure),
    }
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
    PAINT_GENERATION.fetch_add(1, Ordering::AcqRel);
    if matches!(raw_status(), Status::Armed | Status::RemoveRequested) {
        DIAGNOSTIC.store(Diagnostic::PaintEntered as u8, Ordering::Release);
    }
    // SAFETY: pthread_self/equal do not dereference firmware state. The first
    // stock paint establishes the owner; every later native action must run on
    // that same thread.
    let thread = unsafe { libc::pthread_self() };
    let mut owner = UI_THREAD.lock().unwrap_or_else(PoisonError::into_inner);
    if let Some(expected) = *owner {
        if unsafe { libc::pthread_equal(expected, thread) } == 0 {
            set_state(Status::Failed, Diagnostic::WrongThread);
            return;
        }
    } else {
        *owner = Some(thread);
    }
    drop(owner);
    let mut port = unsafe { NativeStockSequencePort::from_step_paint(screen) };
    let Ok(current_context) = port.context_identity() else {
        if matches!(raw_status(), Status::Armed | Status::RemoveRequested) {
            set_state(Status::Failed, Diagnostic::ContextUnavailable);
        }
        return;
    };
    let Some(now) = monotonic_ns() else {
        set_state(Status::Failed, Diagnostic::NativeFailure);
        return;
    };
    LAST_CONTEXT_NS.store(now, Ordering::Release);
    *CURRENT_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner) = Some(current_context);

    if REMOVE.swap(false, Ordering::AcqRel) {
        if !within_deadline(now, REMOVE_DEADLINE_NS.load(Ordering::Acquire)) {
            set_state(Status::Expired, Diagnostic::ExpiredAfterPaint);
            return;
        }
        if !claim_status(&STATUS, Status::RemoveRequested, Status::Removing) {
            return;
        }
        let existing = *RECEIPT.lock().unwrap_or_else(PoisonError::into_inner);
        if let Some(existing) = existing.as_ref() {
            if port.remove(existing).is_ok() {
                DIAGNOSTIC.store(Diagnostic::RemoveAccepted as u8, Ordering::Release);
                STATUS.store(Status::RemoveAccepted as u8, Ordering::Release);
            } else {
                set_state(Status::Failed, Diagnostic::NativeFailure);
            }
        }
        return;
    }

    if observe_existing(&mut port) {
        return;
    }
    if !ARMED.swap(false, Ordering::AcqRel) {
        return;
    }
    if !within_deadline(now, ARM_DEADLINE_NS.load(Ordering::Acquire)) {
        set_state(Status::Expired, Diagnostic::ExpiredAfterPaint);
        return;
    }
    let armed_context = ARMED_CONTEXT
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
        .take();
    if armed_context != Some(current_context) {
        set_state(Status::Rejected, Diagnostic::ContextChanged);
        return;
    }
    if !claim_status(&STATUS, Status::Armed, Status::Inserting) {
        return;
    }
    insert_one(&mut port);
}

#[cfg(test)]
mod tests {
    use super::{
        AtomicBool, AtomicU8, Diagnostic, NativeContextIdentity, OperationGuard, Ordering, Status,
        claim_status, expiry_diagnostic, within_deadline,
    };

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

    #[test]
    fn expiry_distinguishes_missing_paint_from_late_callback() {
        assert_eq!(expiry_diagnostic(7, 7), Diagnostic::ExpiredBeforePaint);
        assert_eq!(expiry_diagnostic(8, 7), Diagnostic::ExpiredAfterPaint);
    }

    #[test]
    fn native_claim_and_host_expiry_are_mutually_exclusive() {
        let callback_wins = AtomicU8::new(Status::Armed as u8);
        assert!(claim_status(
            &callback_wins,
            Status::Armed,
            Status::Inserting
        ));
        assert!(
            callback_wins
                .compare_exchange(
                    Status::Armed as u8,
                    Status::Expired as u8,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                )
                .is_err()
        );

        let expiry_wins = AtomicU8::new(Status::Expired as u8);
        assert!(!claim_status(
            &expiry_wins,
            Status::Armed,
            Status::Inserting
        ));
    }
}
