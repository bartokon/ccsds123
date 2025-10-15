import argparse
from pathlib import Path
from typing import Sequence


def read_bsq(path: Path, nx: int, ny: int, nz: int) -> list[int]:
    data = path.read_bytes()
    expected = nx * ny * nz * 2
    if len(data) != expected:
        raise ValueError(f"Unexpected BSQ size: {len(data)} bytes, expected {expected}")
    values: list[int] = []
    for i in range(0, len(data), 2):
        values.append(int.from_bytes(data[i : i + 2], "little"))
    return values


def compare_sequences(name: str, expected: Sequence[int], actual: Sequence[int]) -> int:
    mismatches = 0
    for idx, (exp, act) in enumerate(zip(expected, actual)):
        if exp != act:
            if mismatches < 20:
                print(f"Mismatch in {name} at sample {idx}: expected {exp}, got {act}")
            mismatches += 1
    if len(expected) != len(actual):
        print(f"Length mismatch for {name}: expected {len(expected)}, got {len(actual)}")
        mismatches += abs(len(expected) - len(actual))
    if mismatches == 0:
        print(f"{name}: all {len(expected)} samples match")
    else:
        print(f"{name}: {mismatches} mismatches detected")
    return mismatches


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare CCSDS-123 reference data against HDL dumps")
    parser.add_argument("--nx", type=int, required=True)
    parser.add_argument("--ny", type=int, required=True)
    parser.add_argument("--nz", type=int, required=True)
    parser.add_argument("--depth", type=int, default=8, help="Sample bit depth")
    parser.add_argument("--reference", type=Path, required=True, help="Reference BSQ image")
    parser.add_argument("--hdl-recon", type=Path, help="HDL reconstructed BSQ image")
    parser.add_argument("--hdl-residuals", type=Path, help="(Not implemented) HDL residual dump")
    args = parser.parse_args()

    reference = read_bsq(args.reference, args.nx, args.ny, args.nz)

    if args.hdl_residuals:
        raise NotImplementedError("Residual comparison is not implemented in this helper")

    if args.hdl_recon:
        hdl_image = read_bsq(args.hdl_recon, args.nx, args.ny, args.nz)
        compare_sequences("reconstructed", reference, hdl_image)
    else:
        print("No HDL reconstruction provided; only reference data read")


if __name__ == "__main__":
    main()
