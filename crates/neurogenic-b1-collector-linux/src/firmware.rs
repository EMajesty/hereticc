//! Exact, allocation-bounded firmware-prefix compatibility guard.

use std::fs::File;
use std::io::{self, Read};
use std::path::Path;
use std::time::Instant;

use crate::AcquireError;

pub const STOCK_EXECUTABLE: &str = "/usr/bin/MPC Sample";
pub const PREFIX_BYTES: usize = 4096;
pub const EXPECTED_PREFIX_SHA256: [u8; 32] = [
    0x44, 0x50, 0x7c, 0x7a, 0x7d, 0x30, 0x29, 0xc4, 0x11, 0xb2, 0xe3, 0x78, 0x0e, 0xa2, 0x86, 0xca,
    0x3e, 0x08, 0xb7, 0xe3, 0xff, 0xd7, 0xb5, 0x04, 0x7d, 0x52, 0xf7, 0x3b, 0xf5, 0x29, 0x9d, 0x1f,
];

/// Verify the exact 4096-byte stock executable prefix before acquisition.
///
/// This is a compatibility gate, separate from process executable-path identity.
///
/// # Errors
///
/// Returns an acquisition error on I/O, a short prefix, deadline expiry, or mismatch.
pub fn verify_stock_prefix(deadline: Instant) -> Result<(), AcquireError> {
    verify_prefix_path(
        Path::new(STOCK_EXECUTABLE),
        deadline,
        EXPECTED_PREFIX_SHA256,
    )
}

fn verify_prefix_path(
    path: &Path,
    deadline: Instant,
    expected: [u8; 32],
) -> Result<(), AcquireError> {
    if Instant::now() >= deadline {
        return Err(AcquireError::Deadline);
    }
    let mut file = File::open(path).map_err(|_| AcquireError::File("firmware-open"))?;
    let mut prefix = [0_u8; PREFIX_BYTES];
    file.read_exact(&mut prefix)
        .map_err(|error| match error.kind() {
            io::ErrorKind::UnexpectedEof => AcquireError::File("firmware-short"),
            _ => AcquireError::File("firmware-read"),
        })?;
    if Instant::now() >= deadline {
        return Err(AcquireError::Deadline);
    }
    if sha256(&prefix) == expected {
        Ok(())
    } else {
        Err(AcquireError::File("firmware-mismatch"))
    }
}

#[allow(clippy::many_single_char_names, clippy::unreadable_literal)]
fn sha256(input: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut h = [
        0x6a09e667_u32,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    ];
    let bit_len = (input.len() as u64) * 8;
    let mut padded = input.to_vec();
    padded.push(0x80);
    while padded.len() % 64 != 56 {
        padded.push(0);
    }
    padded.extend_from_slice(&bit_len.to_be_bytes());
    for chunk in padded.chunks_exact(64) {
        let mut w = [0_u32; 64];
        for (index, word) in chunk.chunks_exact(4).enumerate() {
            w[index] = u32::from_be_bytes(word.try_into().expect("four-byte chunk"));
        }
        for index in 16..64 {
            let s0 = w[index - 15].rotate_right(7)
                ^ w[index - 15].rotate_right(18)
                ^ (w[index - 15] >> 3);
            let s1 = w[index - 2].rotate_right(17)
                ^ w[index - 2].rotate_right(19)
                ^ (w[index - 2] >> 10);
            w[index] = w[index - 16]
                .wrapping_add(s0)
                .wrapping_add(w[index - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh] = h;
        for index in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let choice = (e & f) ^ ((!e) & g);
            let temp1 = hh
                .wrapping_add(s1)
                .wrapping_add(choice)
                .wrapping_add(K[index])
                .wrapping_add(w[index]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let majority = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0.wrapping_add(majority);
            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }
        for (state, value) in h.iter_mut().zip([a, b, c, d, e, f, g, hh]) {
            *state = state.wrapping_add(value);
        }
    }
    let mut output = [0_u8; 32];
    for (target, value) in output.chunks_exact_mut(4).zip(h) {
        target.copy_from_slice(&value.to_be_bytes());
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn sha256_matches_standard_vector() {
        assert_eq!(
            sha256(b"abc"),
            [
                0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae,
                0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61,
                0xf2, 0x00, 0x15, 0xad,
            ]
        );
    }

    #[test]
    fn guard_rejects_wrong_short_and_expired_fixtures() {
        let path = std::env::temp_dir().join(format!("p32f-fw-{}", std::process::id()));
        std::fs::write(&path, vec![1_u8; PREFIX_BYTES]).expect("fixture");
        assert_eq!(
            verify_prefix_path(&path, Instant::now() + Duration::from_secs(1), [0; 32]),
            Err(AcquireError::File("firmware-mismatch"))
        );
        std::fs::write(&path, b"short").expect("short fixture");
        assert_eq!(
            verify_prefix_path(&path, Instant::now() + Duration::from_secs(1), [0; 32]),
            Err(AcquireError::File("firmware-short"))
        );
        assert_eq!(
            verify_prefix_path(&path, Instant::now(), [0; 32]),
            Err(AcquireError::Deadline)
        );
        std::fs::remove_file(path).expect("cleanup");
    }

    #[test]
    fn guard_accepts_exact_fixture_hash() {
        let path = std::env::temp_dir().join(format!("p32f-fw-ok-{}", std::process::id()));
        let bytes = vec![0x5a_u8; PREFIX_BYTES];
        std::fs::write(&path, &bytes).expect("fixture");
        verify_prefix_path(
            &path,
            Instant::now() + Duration::from_secs(1),
            sha256(&bytes),
        )
        .expect("matching prefix");
        std::fs::remove_file(path).expect("cleanup");
    }
}
