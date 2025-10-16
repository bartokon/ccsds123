#!/usr/bin/env python3
"""Compare HDL and C++ video compression using Alternative 1 approach.

Alternative 1: Each RGB channel is treated as an independent temporal sequence,
with frames replacing spectral bands in the CCSDS 123 algorithm.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple


class CompressionResult:
    """Results from compressing a single channel."""

    def __init__(
        self,
        channel: str,
        input_bytes: int,
        cpp_output_bytes: int,
        hdl_output_bytes: int,
        cpp_cr: float,
        hdl_cr: float,
        match: bool,
        error_msg: str = "",
    ):
        self.channel = channel
        self.input_bytes = input_bytes
        self.cpp_output_bytes = cpp_output_bytes
        self.hdl_output_bytes = hdl_output_bytes
        self.cpp_cr = cpp_cr
        self.hdl_cr = hdl_cr
        self.match = match
        self.error_msg = error_msg


def run_command(cmd: List[str], description: str) -> subprocess.CompletedProcess[str]:
    """Run a shell command and return the result."""
    print(f"  Running: {description}")
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        print(f"    FAILED with exit code {result.returncode}", file=sys.stderr)
        if result.stderr:
            print(f"    STDERR: {result.stderr}", file=sys.stderr)
    return result


def compress_channel(
    channel: str,
    bsq_path: Path,
    output_dir: Path,
    encoder: Path,
    decoder: Path,
    nx: int,
    ny: int,
    nz: int,
    depth: int,
    hdl_config: Path,
    vivado_script: Path,
    hdl_build_dir: Path,
    settings_script: str,
    vivado: str,
    gen_impl_params: Path,
) -> CompressionResult:
    """Compress a single channel and compare HDL vs C++ output.

    Args:
        channel: Channel name (red, green, blue)
        bsq_path: Path to input BSQ file
        output_dir: Directory for outputs
        encoder: Path to ccsds123_encode
        decoder: Path to ccsds123_decode
        nx, ny, nz: Image dimensions
        depth: Bit depth
        hdl_config: HDL configuration JSON
        vivado_script: Vivado TCL script
        hdl_build_dir: HDL build directory
        settings_script: Vivado settings script
        vivado: Vivado binary path
        gen_impl_params: Path to gen_impl_params.py

    Returns:
        CompressionResult with comparison details
    """
    print(f"\n{'='*60}")
    print(f"Processing {channel.upper()} channel")
    print(f"{'='*60}")

    cpp_dir = output_dir / "cpp"
    hdl_dir = output_dir / "hdl"
    cpp_dir.mkdir(parents=True, exist_ok=True)
    hdl_dir.mkdir(parents=True, exist_ok=True)

    cpp_container = cpp_dir / f"{channel}.c123"
    cpp_payload = cpp_dir / f"{channel}_payload.bin"
    hdl_payload = hdl_dir / f"{channel}.bin"

    input_bytes = bsq_path.stat().st_size

    # Step 1: Encode with C++
    print(f"\n[1/5] Encoding {channel} channel with C++ codec")
    encode_cmd = [
        str(encoder),
        "-i", str(bsq_path),
        "-o", str(cpp_container),
        "-nx", str(nx),
        "-ny", str(ny),
        "-nz", str(nz),
        "-d", str(depth),
    ]
    encode_result = run_command(encode_cmd, "C++ encode")
    if encode_result.returncode != 0:
        return CompressionResult(
            channel, input_bytes, 0, 0, 0.0, 0.0, False,
            f"C++ encoding failed: {encode_result.stderr}"
        )

    if not cpp_container.exists():
        return CompressionResult(
            channel, input_bytes, 0, 0, 0.0, 0.0, False,
            f"C++ container not created: {cpp_container}"
        )

    cpp_output_bytes = cpp_container.stat().st_size
    cpp_cr = input_bytes / cpp_output_bytes if cpp_output_bytes > 0 else 0.0
    print(f"    C++ output: {cpp_output_bytes} bytes, CR: {cpp_cr:.2f}")

    # Step 2: Regenerate HDL parameters
    print(f"\n[2/5] Regenerating HDL parameters for {nx}×{ny}×{nz}")
    params_cmd = ["python3", str(gen_impl_params), str(hdl_config)]
    params_result = run_command(params_cmd, "HDL parameter generation")
    if params_result.returncode != 0:
        return CompressionResult(
            channel, input_bytes, cpp_output_bytes, 0, cpp_cr, 0.0, False,
            f"HDL parameter generation failed: {params_result.stderr}"
        )

    # Step 3: Run Vivado simulation
    print(f"\n[3/5] Running Vivado HDL simulation for {channel} channel")
    hdl_build_dir.mkdir(parents=True, exist_ok=True)

    # Set environment variables for Vivado
    vivado_env = {
        "HDL_SKIP_PARAM_GEN": "1",
        "HDL_INPUT_FILE": str(bsq_path.absolute()),
        "HDL_OUTPUT_DIR": str(hdl_dir.absolute()),
    }

    vivado_cmd = [
        "bash", "-c",
        f'source "{settings_script}" && "{vivado}" -mode batch -source "{vivado_script}" '
        f'-tclargs "{hdl_build_dir.absolute()}" ccsds123_hdl xc7z020clg484-1 '
        f'em.avnet.com:zed:part0:0.9 top_tb simulate'
    ]

    print(f"    Simulating with input: {bsq_path.name}")
    sim_result = subprocess.run(
        vivado_cmd,
        capture_output=True,
        text=True,
        check=False,
        env={**subprocess.os.environ, **vivado_env},
    )

    if sim_result.returncode != 0:
        return CompressionResult(
            channel, input_bytes, cpp_output_bytes, 0, cpp_cr, 0.0, False,
            f"Vivado simulation failed: {sim_result.stderr[:200]}"
        )

    # Rename generic out.bin to channel-specific name
    # Testbench always writes to out.bin, but we need channel-specific files
    generic_output = hdl_dir / "out.bin"
    if generic_output.exists():
        generic_output.rename(hdl_payload)
        print(f"    Renamed {generic_output.name} → {hdl_payload.name}")
    elif not hdl_payload.exists():
        return CompressionResult(
            channel, input_bytes, cpp_output_bytes, 0, cpp_cr, 0.0, False,
            f"HDL payload not created: expected {generic_output} or {hdl_payload}"
        )

    hdl_output_bytes = hdl_payload.stat().st_size
    hdl_cr = input_bytes / hdl_output_bytes if hdl_output_bytes > 0 else 0.0
    print(f"    HDL output: {hdl_output_bytes} bytes, CR: {hdl_cr:.2f}")

    # Step 4: Compare payloads
    print(f"\n[4/5] Comparing HDL and C++ payloads for {channel} channel")
    compare_cmd = [
        "python3",
        str(Path(__file__).parent / "compare_bitstreams.py"),
        "--container", str(cpp_container),
        "--hdl-payload", str(hdl_payload),
        "--payload-output", str(cpp_payload),
        "--input-bytes", str(input_bytes),
        "--input-file", str(bsq_path),
        "--decoder", str(decoder),
    ]

    compare_result = run_command(compare_cmd, "Payload comparison")
    match = compare_result.returncode == 0

    # Check if both round-trips succeeded even if payloads differ
    roundtrip_success = "round-trip: decoded samples match" in compare_result.stdout.lower()

    if not match:
        if roundtrip_success:
            error_msg = "Payload size difference (expected: HDL uses word-aligned packing)"
            print(f"    ℹ Payload sizes differ, but both round-trips are lossless")
            print(f"    ℹ See docs/hdl_cpp_encoder_comparison.md for details")
        else:
            error_msg = "Payload mismatch detected (see output above)"
    else:
        error_msg = ""
        print(f"    ✓ Payloads match")

    print(f"\n[5/5] {channel.upper()} channel compression complete")

    return CompressionResult(
        channel,
        input_bytes,
        cpp_output_bytes,
        hdl_output_bytes,
        cpp_cr,
        hdl_cr,
        match,
        error_msg,
    )


def print_summary(results: List[CompressionResult]) -> None:
    """Print a summary table of all channel results."""
    print(f"\n{'='*80}")
    print("VIDEO COMPRESSION SUMMARY (Alternative 1: RGB Channels Independent)")
    print(f"{'='*80}\n")

    print(f"{'Channel':<10} {'Input(KB)':<12} {'C++ Out(KB)':<13} {'HDL Out(KB)':<13} "
          f"{'C++ CR':<10} {'HDL CR':<10} {'Status':<10}")
    print("-" * 80)

    total_input = 0
    total_cpp_output = 0
    total_hdl_output = 0
    all_match = True

    for result in results:
        input_kb = result.input_bytes / 1024.0
        cpp_kb = result.cpp_output_bytes / 1024.0
        hdl_kb = result.hdl_output_bytes / 1024.0
        status = "MATCH" if result.match else "MISMATCH"

        print(f"{result.channel.capitalize():<10} {input_kb:<12.2f} {cpp_kb:<13.2f} "
              f"{hdl_kb:<13.2f} {result.cpp_cr:<10.2f} {result.hdl_cr:<10.2f} {status:<10}")

        total_input += result.input_bytes
        total_cpp_output += result.cpp_output_bytes
        total_hdl_output += result.hdl_output_bytes
        all_match = all_match and result.match

        if result.error_msg:
            print(f"           Error: {result.error_msg}")

    print("-" * 80)
    overall_cpp_cr = total_input / total_cpp_output if total_cpp_output > 0 else 0.0
    overall_hdl_cr = total_input / total_hdl_output if total_hdl_output > 0 else 0.0
    overall_status = "SUCCESS" if all_match else "FAILED"

    print(f"{'Overall':<10} {total_input/1024.0:<12.2f} {total_cpp_output/1024.0:<13.2f} "
          f"{total_hdl_output/1024.0:<13.2f} {overall_cpp_cr:<10.2f} {overall_hdl_cr:<10.2f} "
          f"{overall_status:<10}")

    print(f"\n{'='*80}")

    # Add explanatory note if there are size differences but lossless compression
    if not all_match:
        lossless_count = sum(1 for r in results if "word-aligned packing" in r.error_msg)
        if lossless_count > 0:
            print(f"\nNote: {lossless_count} channel(s) show expected size differences due to HDL")
            print("word-aligned packing (see docs/hdl_cpp_encoder_comparison.md).")
            print("Both implementations produce valid lossless compression.")

    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--bsq-dir",
        type=Path,
        required=True,
        help="Directory containing channel BSQ files",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Output directory for cpp/ and hdl/ subdirectories",
    )
    parser.add_argument(
        "--encoder",
        type=Path,
        required=True,
        help="Path to ccsds123_encode binary",
    )
    parser.add_argument(
        "--decoder",
        type=Path,
        required=True,
        help="Path to ccsds123_decode binary",
    )
    parser.add_argument(
        "--hdl-config",
        type=Path,
        required=True,
        help="HDL configuration JSON file",
    )
    parser.add_argument("--nx", type=int, required=True, help="Frame width")
    parser.add_argument("--ny", type=int, required=True, help="Frame height")
    parser.add_argument("--nz", type=int, required=True, help="Number of frames")
    parser.add_argument("--depth", type=int, required=True, help="Bit depth")
    parser.add_argument(
        "--vivado-script",
        type=Path,
        required=True,
        help="Vivado TCL script path",
    )
    parser.add_argument(
        "--hdl-build-dir",
        type=Path,
        required=True,
        help="HDL build directory",
    )
    parser.add_argument(
        "--settings-script",
        type=str,
        required=True,
        help="Vivado settings script path",
    )
    parser.add_argument(
        "--vivado",
        type=str,
        required=True,
        help="Vivado binary path",
    )

    args = parser.parse_args()

    # Find channel BSQ files
    if args.nz == 1:
        channel_files = {
            "red": args.bsq_dir / "red_frame_001.bsq",
            "green": args.bsq_dir / "green_frame_001.bsq",
            "blue": args.bsq_dir / "blue_frame_001.bsq",
        }
    else:
        channel_files = {
            "red": args.bsq_dir / "red_frames.bsq",
            "green": args.bsq_dir / "green_frames.bsq",
            "blue": args.bsq_dir / "blue_frames.bsq",
        }

    # Verify all files exist
    missing = [name for name, path in channel_files.items() if not path.exists()]
    if missing:
        print(f"Error: Missing channel files: {', '.join(missing)}", file=sys.stderr)
        return 1

    gen_impl_params = Path(__file__).parent / "gen_impl_params.py"
    if not gen_impl_params.exists():
        print(f"Error: gen_impl_params.py not found at {gen_impl_params}", file=sys.stderr)
        return 1

    # Process each channel
    results = []
    for channel, bsq_path in channel_files.items():
        result = compress_channel(
            channel,
            bsq_path,
            args.output_dir,
            args.encoder,
            args.decoder,
            args.nx,
            args.ny,
            args.nz,
            args.depth,
            args.hdl_config,
            args.vivado_script,
            args.hdl_build_dir,
            args.settings_script,
            args.vivado,
            gen_impl_params,
        )
        results.append(result)

        # Stop early if a channel fails
        if not result.match:
            print(f"\n⚠ {channel.upper()} channel failed, stopping early", file=sys.stderr)
            break

    # Print summary
    print_summary(results)

    # Return exit code
    return 0 if all(r.match for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
