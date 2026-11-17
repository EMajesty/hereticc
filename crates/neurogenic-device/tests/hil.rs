#![cfg(feature = "hil-test-interface")]

use neurogenic_device::hil::{Error, MAX_COMMAND_BYTES, MAX_RESPONSE_BYTES, apply};
use neurogenic_device::reducer::UiState;

#[test]
fn complete_edit_sequence_uses_the_real_reducer() {
    let mut ui = UiState::default();
    assert!(apply(&mut ui, b"OPEN").unwrap().as_str().contains("open=1"));
    assert!(
        apply(&mut ui, b"MOVE -1")
            .unwrap()
            .as_str()
            .contains("step=16")
    );
    assert!(
        apply(&mut ui, b"SELECT 4")
            .unwrap()
            .as_str()
            .contains("track=4")
    );
    let toggled = apply(&mut ui, b"TOGGLE").unwrap();
    assert!(toggled.as_str().contains("bits=8000"));
    assert!(toggled.as_str().contains("flow=consume"));
    assert!(toggled.as_str().len() <= MAX_RESPONSE_BYTES);
}

#[test]
fn close_and_reset_are_deterministic() {
    let mut ui = UiState::default();
    apply(&mut ui, b"OPEN").unwrap();
    let close = apply(&mut ui, b"CLOSE\n").unwrap();
    assert!(close.as_str().contains("open=0"));
    assert!(close.as_str().contains("restore=1"));
    let reset = apply(&mut ui, b"RESET").unwrap();
    assert_eq!(
        reset.as_str(),
        "v=1 open=0 track=1 step=1 bits=0000 flow=consume repaint=1 restore=0 pending_repaint=0\n"
    );
}

#[test]
fn malformed_and_bounded_commands_fail_closed() {
    let mut ui = UiState::default();
    assert_eq!(apply(&mut ui, b""), Err(Error::Empty));
    assert_eq!(apply(&mut ui, &[0xff]), Err(Error::NonAscii));
    assert_eq!(
        apply(&mut ui, &[b'X'; MAX_COMMAND_BYTES + 1]),
        Err(Error::TooLong)
    );
    assert_eq!(apply(&mut ui, b"MOVE 0"), Err(Error::OutOfRange));
    assert_eq!(apply(&mut ui, b"MOVE 1 2"), Err(Error::Malformed));
    assert_eq!(apply(&mut ui, b"SELECT 17"), Err(Error::OutOfRange));
    assert_eq!(apply(&mut ui, b"QUERY now"), Err(Error::Malformed));
    assert_eq!(apply(&mut ui, b"SHELL"), Err(Error::Unknown));
}

#[cfg(feature = "native-sequence-adapter")]
#[test]
fn native_experiment_commands_are_bounded_and_inert_until_install() {
    let mut ui = UiState::default();
    let status = apply(&mut ui, b"NATIVE STATUS").expect("status");
    assert!(status.as_str().contains("native=unavailable"));
    assert!(status.as_str().contains("action_accepted=1"));
    assert!(status.as_str().contains("context=unavailable"));
    let arm = apply(&mut ui, b"NATIVE ARM").expect("arm");
    assert!(arm.as_str().contains("native=unavailable"));
    assert!(arm.as_str().contains("action_accepted=0"));
    assert_eq!(apply(&mut ui, b"NATIVE EXEC"), Err(Error::Unknown));
}

#[test]
fn inactive_edit_actions_preserve_forwarding() {
    let mut ui = UiState::default();
    assert!(
        apply(&mut ui, b"TOGGLE")
            .unwrap()
            .as_str()
            .contains("flow=forward")
    );
    assert!(
        apply(&mut ui, b"SELECT 2")
            .unwrap()
            .as_str()
            .contains("flow=forward")
    );
    assert!(
        apply(&mut ui, b"QUERY")
            .unwrap()
            .as_str()
            .contains("open=0")
    );
}
