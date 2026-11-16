//! Opt-in host-side reader for a B0.2 shared counter snapshot.

use std::path::Path;

use hereticc_m03b_b0_input_probe::snapshot::{format_snapshot, read_verified};

fn main() {
    if let Err(error) = run() {
        eprintln!("B0.2 snapshot failed: {error:?}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), hereticc_m03b_b0_input_probe::snapshot::SnapshotError> {
    let mut arguments = std::env::args_os().skip(1);
    let Some(snapshot) = arguments.next() else {
        eprintln!("usage: b02-snapshot SNAPSHOT [PROC_ROOT]");
        return Err(hereticc_m03b_b0_input_probe::snapshot::SnapshotError::InvalidPath);
    };
    let proc_root = arguments.next().unwrap_or_else(|| "/proc".into());
    if arguments.next().is_some() {
        eprintln!("usage: b02-snapshot SNAPSHOT [PROC_ROOT]");
        return Err(hereticc_m03b_b0_input_probe::snapshot::SnapshotError::InvalidPath);
    }
    let verified = read_verified(Path::new(&snapshot), Path::new(&proc_root))?;
    let report = format_snapshot(verified)?;
    print!("{report}");
    Ok(())
}
