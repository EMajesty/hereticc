//! Fixed-mode, host-inspectable P3.2f integration executable.

use std::path::Path;
use std::process::ExitCode;
use std::time::{Duration, Instant};

use neurogenic_b1_collector_linux::firmware::verify_stock_prefix;
use neurogenic_b1_collector_linux::sd::{SdDestination, write_collector_stage};
use neurogenic_b1_collector_linux::{
    AcquireError, AcquisitionStage, BoundedCommandRunner, ExactFileReader, MAX_DIRECT_READ_BYTES,
    SystemFileReader, acquire_with_observer,
};
use neurogenic_b1_collector_model::{CollectionError, MonotonicClock, collect};

const TOTAL_LIFETIME: Duration = Duration::from_secs(10);
const TOTAL_LIFETIME_MS: u64 = 10_000;
fn record_stage(stage: &'static str) {
    let _ = write_collector_stage(stage.as_bytes());
}

#[derive(Debug)]
enum RunError {
    Acquire,
    Collect,
}

impl From<AcquireError> for RunError {
    fn from(_value: AcquireError) -> Self {
        Self::Acquire
    }
}

impl From<CollectionError> for RunError {
    fn from(_value: CollectionError) -> Self {
        Self::Collect
    }
}

struct Clock {
    started: Instant,
}

impl MonotonicClock for Clock {
    fn now_ms(&mut self) -> u64 {
        u64::try_from(self.started.elapsed().as_millis()).unwrap_or(u64::MAX)
    }
}

fn run() -> Result<(), RunError> {
    record_stage("entry-reached\n");
    let started = Instant::now();
    let deadline = started + TOTAL_LIFETIME;
    verify_stock_prefix(deadline)?;
    record_stage("firmware-passed\n");

    let mut files = SystemFileReader;
    let initial_mounts = files.read(Path::new("/proc/mounts"), MAX_DIRECT_READ_BYTES, deadline)?;
    let destination = SdDestination::open(&initial_mounts)?;
    record_stage("sd-validation-passed\n");

    let observations = acquire_with_observer(
        &mut BoundedCommandRunner,
        &mut files,
        deadline,
        |stage| match stage {
            AcquisitionStage::ServiceProcessAcquired => {
                record_stage("service-process-acquired\n");
            }
        },
    )?;
    record_stage("acquisition-passed\n");
    let mut clock = Clock { started };
    let outcome = collect(&observations, &mut clock, TOTAL_LIFETIME_MS)?;
    record_stage("collection-passed\n");
    if Instant::now() >= deadline {
        return Err(RunError::Acquire);
    }
    let current_mounts = files.read(Path::new("/proc/mounts"), MAX_DIRECT_READ_BYTES, deadline)?;
    record_stage("report-publication-attempted\n");
    if destination
        .publish(&current_mounts, &outcome.report)
        .is_err()
    {
        record_stage("report-publication-failed\n");
        return Err(RunError::Acquire);
    }
    record_stage("report-publication-passed\n");
    if Instant::now() >= deadline {
        return Err(RunError::Acquire);
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(_error) => ExitCode::FAILURE,
    }
}
