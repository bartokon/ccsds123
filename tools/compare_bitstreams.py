#!/usr/bin/env python3
"""Compare HDL payload bytes against the C++ container payload."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

DEBUG_BYTES = 32
MISMATCH_LIMIT = 8

HEADER_V2_FORMAT = "<4s7H3I"
HEADER_V3_FORMAT = "<4s8H5h4H2I"
HEADER_FORMATS = {
    2: (HEADER_V2_FORMAT, struct.calcsize(HEADER_V2_FORMAT), 8),
    3: (HEADER_V3_FORMAT, struct.calcsize(HEADER_V3_FORMAT), 18),
}
MAGIC = b"C123"


def format_slice(data: bytes, base_index: int, width: int = 16) -> str:
    if not data:
        return f"{base_index:08X}: <empty>"
    lines: list[str] = []
    for offset in range(0, len(data), width):
        chunk = data[offset : offset + width]
        hex_bytes = " ".join(f"{value:02X}" for value in chunk)
        ascii_repr = "".join(chr(value) if 32 <= value < 127 else "." for value in chunk)
        lines.append(
            f"{base_index + offset:08X}: {hex_bytes:<{width * 3 - 1}} |{ascii_repr}|"
        )
    return "\n".join(lines)


def decode_container(
    decoder: Path | None, container: Path, destination: Path
) -> subprocess.CompletedProcess[str] | None:
    if decoder is None:
        return None
    command = [str(decoder), "-i", str(container), "-o", str(destination)]
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "Decoding failed with exit code "
            f"{result.returncode}.\nSTDOUT:{result.stdout}\nSTDERR:{result.stderr}"
        )
    return result


def compare_raw_streams(original: Path, reconstructed: Path) -> int:
    original_bytes = original.read_bytes()
    reconstructed_bytes = reconstructed.read_bytes()
    if original_bytes == reconstructed_bytes:
        print("Round-trip verification: decoded samples match the input stream")
        return 0

    print(
        "Round-trip verification failed: decoded samples differ from the input stream",
        file=sys.stderr,
    )
    mismatch_limit = min(MISMATCH_LIMIT, len(reconstructed_bytes), len(original_bytes))
    differing_indices: list[int] = []
    for idx in range(min(len(original_bytes), len(reconstructed_bytes))):
        if original_bytes[idx] != reconstructed_bytes[idx]:
            differing_indices.append(idx)
            if len(differing_indices) >= mismatch_limit:
                break

    if len(original_bytes) != len(reconstructed_bytes):
        print(
            f"  Length mismatch: input={len(original_bytes)} bytes, "
            f"decoded={len(reconstructed_bytes)} bytes",
            file=sys.stderr,
        )

    if differing_indices:
        first_diff = differing_indices[0]
        print(
            f"  First mismatch at byte {first_diff}: "
            f"input=0x{original_bytes[first_diff]:02X} decoded=0x{reconstructed_bytes[first_diff]:02X}",
            file=sys.stderr,
        )
        start = max(0, first_diff - DEBUG_BYTES // 2)
        end = min(len(original_bytes), first_diff + DEBUG_BYTES // 2)
        print("  Input slice:", file=sys.stderr)
        print(format_slice(original_bytes[start:end], start), file=sys.stderr)
        end_decoded = min(len(reconstructed_bytes), first_diff + DEBUG_BYTES // 2)
        print("  Decoded slice:", file=sys.stderr)
        print(format_slice(reconstructed_bytes[start:end_decoded], start), file=sys.stderr)
    return 1


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


def compare_payloads(
    container: Path,
    hdl_payload: Path,
    payload_output: Path | None,
    input_bytes: int,
    input_file: Path | None,
    decoder: Path | None,
) -> int:
    header, reference_payload = parse_container(container)
    if payload_output is not None:
        payload_output.parent.mkdir(parents=True, exist_ok=True)
        payload_output.write_bytes(reference_payload)

    hdl_bytes = hdl_payload.read_bytes()
    print(
        f"Payload lengths: HDL={len(hdl_bytes)} bytes, reference={len(reference_payload)} bytes"
    )
    exit_code = 0

    if len(hdl_bytes) != len(reference_payload):
        print(
            f"Length mismatch: HDL payload has {len(hdl_bytes)} bytes, reference has {len(reference_payload)} bytes",
            file=sys.stderr,
        )
        exit_code = 1

    compare_len = min(len(hdl_bytes), len(reference_payload))
    differing_indices: list[int] = []
    for idx in range(compare_len):
        if hdl_bytes[idx] != reference_payload[idx]:
            differing_indices.append(idx)
            if len(differing_indices) >= MISMATCH_LIMIT:
                break

    if differing_indices:
        exit_code = 1
        first_diff = differing_indices[0]
        last_diff = differing_indices[-1]
        print(
            f"Payload contents differ. First mismatch at byte {first_diff}, "
            f"last reported mismatch at byte {last_diff}.",
            file=sys.stderr,
        )
        for idx in differing_indices:
            print(
                f"  byte {idx:05d}: HDL=0x{hdl_bytes[idx]:02X} REF=0x{reference_payload[idx]:02X}",
                file=sys.stderr,
            )
        start = max(0, first_diff - DEBUG_BYTES // 2)
        end = min(compare_len, first_diff + DEBUG_BYTES // 2)
        print("Reference payload slice:", file=sys.stderr)
        print(format_slice(reference_payload[start:end], start), file=sys.stderr)
        print("HDL payload slice:", file=sys.stderr)
        print(format_slice(hdl_bytes[start:end], start), file=sys.stderr)

    if len(reference_payload) > 0:
        ratio = input_bytes / len(reference_payload)
        print(f"Compression ratio (input/output): {ratio:.6f}")
    else:
        print("Compression ratio unavailable: reference payload is empty")

    version, nx, ny, nz, depth = header[1], header[2], header[3], header[4], header[5]
    print(f"C++ container summary: version={version}, NX={nx}, NY={ny}, NZ={nz}, D={depth}")
    if exit_code == 0:
        print("HDL payload matches C++ reference payload")

    roundtrip_status = 0
    if input_file is not None:
        if not input_file.exists():
            print(
                f"Input reference file '{input_file}' does not exist; skipping round-trip check",
                file=sys.stderr,
            )
            roundtrip_status = 1
        elif decoder is None:
            print(
                "Decoder path not provided; skipping round-trip verification",
                file=sys.stderr,
            )
        elif not decoder.exists():
            print(
                f"Decoder binary '{decoder}' not found; skipping round-trip verification",
                file=sys.stderr,
            )
        else:
            with TemporaryDirectory() as tmp_dir:
                decoded_path = Path(tmp_dir) / "decoded.bsq"
                try:
                    decode_container(decoder, container, decoded_path)
                except Exception as exc:  # pragma: no cover - external tool invocation
                    print(f"Decoder invocation failed: {exc}", file=sys.stderr)
                    roundtrip_status = 1
                else:
                    roundtrip_status = compare_raw_streams(input_file, decoded_path)

    return 1 if exit_code or roundtrip_status else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--container", type=Path, required=True, help="Path to the C++ container file")
    parser.add_argument("--hdl-payload", type=Path, required=True, help="Path to the HDL payload dump")
    parser.add_argument("--payload-output", type=Path, help="Optional path to export the reference payload bytes")
    parser.add_argument("--input-bytes", type=int, required=True, help="Number of uncompressed input bytes")
    parser.add_argument("--input-file", type=Path, help="Optional path to the uncompressed input file")
    parser.add_argument(
        "--decoder",
        type=Path,
        help="Optional path to the ccsds123_decode binary for round-trip verification",
    )
    args = parser.parse_args(argv)

    try:
        return compare_payloads(
            args.container,
            args.hdl_payload,
            args.payload_output,
            args.input_bytes,
            args.input_file,
            args.decoder,
        )
    except Exception as exc:  # pragma: no cover - defensive reporting
        print(f"compare_bitstreams: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
