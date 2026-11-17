#!/usr/bin/env python3
"""Static boundary checks for the test-only AArch64 HIL library."""

from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: inspect.py LIBRARY")
    library = Path(sys.argv[1])
    if not library.is_file() or library.is_symlink() or library.stat().st_size > 16 * 1024 * 1024:
        raise SystemExit("unsafe HIL library path or size")
    header = subprocess.run(
        ["readelf", "-h", str(library)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if "AArch64" not in header or "DYN (Shared object file)" not in header:
        raise SystemExit("HIL artifact is not an AArch64 shared object")
    imports = subprocess.run(
        ["readelf", "-Ws", str(library)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    prohibited = (" system(", " execve", " ptrace", " listen", " accept", " connect")
    found = [name.strip() for name in prohibited if name in imports]
    if found:
        raise SystemExit("prohibited HIL import: " + ", ".join(found))
    print("hil_library_inspection=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
