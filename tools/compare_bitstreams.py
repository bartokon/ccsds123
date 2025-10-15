#!/usr/bin/env python3
"""Compare HDL payload bytes against the C++ container payload."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HEADER_V2_FORMAT = "<4s7H3I"
HEADER_V3_FORMAT = "<4s8H5h4H2I"
HEADER_FORMATS = {
    2: (HEADER_V2_FORMAT, struct.calcsize(HEADER_V2_FORMAT), 8),
    3: (HEADER_V3_FORMAT, struct.calcsize(HEADER_V3_FORMAT), 18),
}
MAGIC = b"C123"


def parse_container(path: Path) -> tuple[tuple[int, ...], bytes]:
    data = path.read_bytes()
    min_header_size = min(size for _, size, _ in HEADER_FORMATS.values())
    if len(data) < min_header_size:
        raise ValueError(f"Container '{path}' is too small ({len(data)} bytes)")

    if data[:4] != MAGIC:
        raise ValueError(f"Container '{path}' has invalid magic {data[:4]!r}")

    (version,) = struct.unpack_from("<H", data, 4)

    if version not in HEADER_FORMATS:
        raise ValueError(f"Container '{path}' has unsupported version {version}")

    fmt, header_size, payload_index = HEADER_FORMATS[version]
    if len(data) < header_size:
        raise ValueError(
            f"Container '{path}' is too small for version {version} header "
            f"({len(data)} < {header_size})"
        )

    header = struct.unpack(fmt, data[:header_size])
    if header[0] != MAGIC:
        raise ValueError(f"Container '{path}' has invalid magic {header[0]!r}")

    payload_bits = header[payload_index]
    payload_bytes = (payload_bits + 7) // 8
    payload_offset = header_size
    payload = data[payload_offset:payload_offset + payload_bytes]
    if len(payload) != payload_bytes:
        raise ValueError(
            f"Container '{path}' payload truncated: expected {payload_bytes} bytes, got {len(payload)}"
        )
    return header, payload


def compare_payloads(container: Path, hdl_payload: Path, payload_output: Path | None, input_bytes: int) -> int:
    header, reference_payload = parse_container(container)
    if payload_output is not None:
        payload_output.parent.mkdir(parents=True, exist_ok=True)
        payload_output.write_bytes(reference_payload)

    hdl_bytes = hdl_payload.read_bytes()
    exit_code = 0

    if len(hdl_bytes) != len(reference_payload):
        print(
            f"Length mismatch: HDL payload has {len(hdl_bytes)} bytes, reference has {len(reference_payload)} bytes",
            file=sys.stderr,
        )
        exit_code = 1

    compare_len = min(len(hdl_bytes), len(reference_payload))
    for idx in range(compare_len):
        if hdl_bytes[idx] != reference_payload[idx]:
            if exit_code == 0:
                print("Payload contents differ", file=sys.stderr)
            if idx < 20:
                print(
                    f"Mismatch at byte {idx}: HDL=0x{hdl_bytes[idx]:02X}, REF=0x{reference_payload[idx]:02X}",
                    file=sys.stderr,
                )
            exit_code = 1
            break

    if len(reference_payload) > 0:
        ratio = input_bytes / len(reference_payload)
        print(f"Compression ratio (input/output): {ratio:.6f}")
    else:
        print("Compression ratio unavailable: reference payload is empty")

    version, nx, ny, nz, depth = header[1], header[2], header[3], header[4], header[5]
    print(f"C++ container summary: version={version}, NX={nx}, NY={ny}, NZ={nz}, D={depth}")
    if exit_code == 0:
        print("HDL payload matches C++ reference payload")
    return exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--container", type=Path, required=True, help="Path to the C++ container file")
    parser.add_argument("--hdl-payload", type=Path, required=True, help="Path to the HDL payload dump")
    parser.add_argument("--payload-output", type=Path, help="Optional path to export the reference payload bytes")
    parser.add_argument("--input-bytes", type=int, required=True, help="Number of uncompressed input bytes")
    args = parser.parse_args(argv)

    try:
        return compare_payloads(args.container, args.hdl_payload, args.payload_output, args.input_bytes)
    except Exception as exc:  # pragma: no cover - defensive reporting
        print(f"compare_bitstreams: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
