#![cfg(feature = "native-sequence-adapter")]

use std::collections::VecDeque;

use neurogenic_core::{EventId, MaterializationIdentity, MusicalTime};
use neurogenic_device::native_batch::{NativeBatchCoordinator, NativeBatchItem, NativeBatchState};
use neurogenic_device::native_grid::GridEditPort;
use neurogenic_device::native_stock_sequence::{NativeEventPresence, NativeScanEvidence};
use neurogenic_device::stock_sequence::StockNoteRequest;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Receipt(u8);

#[derive(Default)]
struct FakePort {
    next: u8,
    insert_fail_at: Option<u8>,
    remove_fail_at: Option<u8>,
    observe: VecDeque<Result<NativeEventPresence, &'static str>>,
    removed: Vec<u8>,
}

impl GridEditPort for FakePort {
    type Receipt = Receipt;
    type Error = &'static str;

    fn insert(&mut self, _: StockNoteRequest) -> Result<Self::Receipt, Self::Error> {
        if self.insert_fail_at == Some(self.next) {
            return Err("insert");
        }
        let receipt = Receipt(self.next);
        self.next += 1;
        Ok(receipt)
    }

    fn observe(
        &mut self,
        receipt: &mut Self::Receipt,
    ) -> Result<(NativeEventPresence, NativeScanEvidence), Self::Error> {
        self.observe
            .pop_front()
            .unwrap_or_else(|| {
                Ok(if self.removed.contains(&receipt.0) {
                    NativeEventPresence::Absent
                } else {
                    NativeEventPresence::Unique
                })
            })
            .map(|presence| (presence, NativeScanEvidence::default()))
    }

    fn remove(&mut self, receipt: &Self::Receipt) -> Result<(), Self::Error> {
        if self.remove_fail_at == Some(receipt.0) {
            return Err("remove");
        }
        self.removed.push(receipt.0);
        Ok(())
    }
}

fn items(count: u8) -> Vec<NativeBatchItem> {
    (0..count)
        .map(|index| NativeBatchItem {
            identity: MaterializationIdentity {
                event: EventId::new(u64::from(index) + 1),
                at: MusicalTime::new(u64::from(index) * 240),
            },
            request: StockNoteRequest::from_absolute(index % 2, i64::from(index) * 240, 240, 100)
                .unwrap(),
        })
        .collect()
}

fn drive_ready(batch: &mut NativeBatchCoordinator<Receipt>, port: &mut FakePort) {
    while batch.state() == NativeBatchState::Inserting {
        batch.drive_insert(port).unwrap();
    }
}

fn drive_clean(batch: &mut NativeBatchCoordinator<Receipt>, port: &mut FakePort) {
    while batch.state() == NativeBatchState::Cleaning {
        batch.drive_cleanup(port).unwrap();
    }
}

#[test]
fn batch_inserts_verifies_and_removes_exact_receipts_in_reverse() {
    let mut batch = NativeBatchCoordinator::default();
    let mut port = FakePort::default();
    batch.load(&items(4)).unwrap();
    drive_ready(&mut batch, &mut port);
    assert_eq!(batch.state(), NativeBatchState::ReadyForPlayback);
    assert_eq!(batch.counts().accepted, 4);
    assert_eq!(batch.counts().verified, 4);

    batch.begin_cleanup().unwrap();
    drive_clean(&mut batch, &mut port);
    assert_eq!(batch.state(), NativeBatchState::Cleaned);
    assert_eq!(batch.counts().removed, 4);
    assert_eq!(port.removed, vec![3, 2, 1, 0]);
}

#[test]
fn partial_insert_failure_cleans_only_uniquely_verified_receipts() {
    let mut batch = NativeBatchCoordinator::default();
    let mut port = FakePort {
        insert_fail_at: Some(2),
        ..FakePort::default()
    };
    batch.load(&items(4)).unwrap();
    while batch.state() == NativeBatchState::Inserting {
        let _ = batch.drive_insert(&mut port);
    }
    assert_eq!(batch.state(), NativeBatchState::Failed);
    assert_eq!(batch.counts().verified, 2);
    batch.begin_cleanup().unwrap();
    drive_clean(&mut batch, &mut port);
    assert_eq!(batch.state(), NativeBatchState::Cleaned);
    assert_eq!(port.removed, vec![1, 0]);
}

#[test]
fn accepted_but_ambiguous_event_is_never_removed_speculatively() {
    let mut batch = NativeBatchCoordinator::default();
    let mut port = FakePort {
        observe: VecDeque::from([
            Ok(NativeEventPresence::Unique),
            Ok(NativeEventPresence::Ambiguous),
        ]),
        ..FakePort::default()
    };
    batch.load(&items(2)).unwrap();
    while batch.state() == NativeBatchState::Inserting {
        let _ = batch.drive_insert(&mut port);
    }
    assert_eq!(batch.counts().verified, 1);
    assert_eq!(batch.counts().unverified_accepted, 1);
    batch.begin_cleanup().unwrap();
    drive_clean(&mut batch, &mut port);
    assert_eq!(batch.state(), NativeBatchState::Dirty);
    assert_eq!(port.removed, vec![0]);
}

#[test]
fn removal_failure_stops_before_another_receipt_is_touched() {
    let mut batch = NativeBatchCoordinator::default();
    let mut port = FakePort::default();
    batch.load(&items(3)).unwrap();
    drive_ready(&mut batch, &mut port);
    port.remove_fail_at = Some(2);
    batch.begin_cleanup().unwrap();
    assert!(batch.drive_cleanup(&mut port).is_err());
    assert_eq!(batch.state(), NativeBatchState::Dirty);
    assert_eq!(port.removed, Vec::<u8>::new());
}

#[test]
fn duplicate_identity_and_capacity_fail_before_native_work() {
    let mut duplicate = items(2);
    duplicate[1].identity = duplicate[0].identity;
    assert!(
        NativeBatchCoordinator::<Receipt>::default()
            .load(&duplicate)
            .is_err()
    );
    assert!(
        NativeBatchCoordinator::<Receipt>::default()
            .load(&items(13))
            .is_err()
    );
}
