#!/usr/bin/env python3
"""Convert PNG video frames to separate RGB channel BSQ files for CCSDS 123 Alternative 1.

Alternative 1 approach: treat temporal dimension as spectral dimension.
Each color channel (R, G, B) becomes an independent temporal sequence compressed separately.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

try:
    from PIL import Image
except ImportError:
    print("Error: PIL/Pillow is required. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)


def convert_frames_to_channels(
    input_dir: Path,
    output_dir: Path,
    num_frames: int,
    nx: int,
    ny: int,
) -> None:
    """Convert PNG frames to separate R, G, B channel BSQ files.

    Args:
        input_dir: Directory containing frame_NNN.png files
        output_dir: Directory for output BSQ files
        num_frames: Number of frames to process (1 or 10)
        nx: Expected width
        ny: Expected height
    """
    # Find and sort PNG files
    png_files = sorted(input_dir.glob("frame_*.png"))
    if len(png_files) < num_frames:
        raise ValueError(
            f"Expected at least {num_frames} PNG files, found {len(png_files)}"
        )

    selected_frames = png_files[:num_frames]

    print(f"Processing {num_frames} frame(s) from {input_dir}")
    for idx, frame_path in enumerate(selected_frames, 1):
        print(f"  {idx}. {frame_path.name}")

    # Initialize channel buffers
    red_channel = bytearray()
    green_channel = bytearray()
    blue_channel = bytearray()

    # Process each frame
    for frame_path in selected_frames:
        img = Image.open(frame_path)

        # Verify RGB mode and dimensions
        if img.mode != "RGB":
            raise ValueError(f"{frame_path.name}: expected RGB mode, got {img.mode}")

        width, height = img.size
        if width != nx or height != ny:
            raise ValueError(
                f"{frame_path.name}: expected {nx}×{ny}, got {width}×{height}"
            )

        # Extract pixels in BSQ order (all R, then all G, then all B for this frame)
        pixels = list(img.getdata())

        # Split into channels and append to temporal sequence
        # BSQ layout: band_0 (all pixels), band_1 (all pixels), ...
        # For video: frame_0 (all pixels), frame_1 (all pixels), ...
        for r, g, b in pixels:
            # Store 8-bit values in 16-bit little-endian format
            red_channel.extend(r.to_bytes(2, "little"))
            green_channel.extend(g.to_bytes(2, "little"))
            blue_channel.extend(b.to_bytes(2, "little"))

    # Write channel files
    output_dir.mkdir(parents=True, exist_ok=True)

    if num_frames == 1:
        red_path = output_dir / "red_frame_001.bsq"
        green_path = output_dir / "green_frame_001.bsq"
        blue_path = output_dir / "blue_frame_001.bsq"
    else:
        red_path = output_dir / "red_frames.bsq"
        green_path = output_dir / "green_frames.bsq"
        blue_path = output_dir / "blue_frames.bsq"

    red_path.write_bytes(red_channel)
    green_path.write_bytes(green_channel)
    blue_path.write_bytes(blue_channel)

    expected_size = nx * ny * num_frames * 2
    print(f"\nOutput files written:")
    print(f"  {red_path.name}: {len(red_channel)} bytes (expected {expected_size})")
    print(f"  {green_path.name}: {len(green_channel)} bytes (expected {expected_size})")
    print(f"  {blue_path.name}: {len(blue_channel)} bytes (expected {expected_size})")

    if len(red_channel) != expected_size:
        raise ValueError(
            f"Size mismatch: generated {len(red_channel)} bytes, expected {expected_size}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--input",
        type=Path,
        required=True,
        help="Input directory containing frame_NNN.png files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output directory for BSQ channel files",
    )
    parser.add_argument(
        "--num-frames",
        type=int,
        required=True,
        choices=[1, 10],
        help="Number of frames to process (1 for single, 10 for full)",
    )
    parser.add_argument(
        "--nx",
        type=int,
        default=128,
        help="Expected frame width (default: 128)",
    )
    parser.add_argument(
        "--ny",
        type=int,
        default=128,
        help="Expected frame height (default: 128)",
    )

    args = parser.parse_args()

    try:
        convert_frames_to_channels(
            args.input,
            args.output,
            args.num_frames,
            args.nx,
            args.ny,
        )
        return 0
    except Exception as exc:
        print(f"convert_video_to_channels: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
