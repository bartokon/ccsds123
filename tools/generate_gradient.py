#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def generate_gradient(nx: int, ny: int, nz: int, path: Path) -> None:
    data = bytearray()
    for band in range(nz):
        for y in range(ny):
            for x in range(nx):
                value = (x * 5 + y * 3 + band * 11) & 0xFF
                data.extend(value.to_bytes(2, "little"))
    path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a synthetic RGB gradient test pattern")
    parser.add_argument("--nx", type=int, required=True)
    parser.add_argument("--ny", type=int, required=True)
    parser.add_argument("--nz", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate_gradient(args.nx, args.ny, args.nz, args.output)


if __name__ == "__main__":
    main()
