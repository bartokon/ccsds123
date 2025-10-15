#!/usr/bin/env python3
"""Generate synthetic multi-channel BSQ frames for regression tests."""

from __future__ import annotations

import argparse
import pathlib
import struct
from typing import Iterable


def generate_frame(nx: int, ny: int, nz: int, seed: int) -> bytes:
    data = bytearray()
    for z in range(nz):
        for y in range(ny):
            for x in range(nx):
                value = (x * 37 + y * 23 + z * 59 + seed * 131) % (1 << 16)
                data.extend(struct.pack("<H", value))
    return bytes(data)


def frame_names(count: int) -> Iterable[str]:
    for index in range(1, count + 1):
        yield f"frame_{index:04d}.bsq"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=pathlib.Path, default=pathlib.Path("tests/data"),
                        help="Destination directory for generated frames")
    parser.add_argument("--frames", type=int, default=1, help="Number of frames to generate")
    parser.add_argument("--nx", type=int, default=16, help="Samples per line")
    parser.add_argument("--ny", type=int, default=12, help="Number of lines")
    parser.add_argument("--nz", type=int, default=4, help="Spectral components")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(frame_names(args.frames)):
        payload = generate_frame(args.nx, args.ny, args.nz, seed=index)
        (args.output_dir / name).write_bytes(payload)
if __name__ == "__main__":
    main()
