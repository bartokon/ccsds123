"""Generate local-diff verification vectors shared by Python, C++, and HDL tests."""

from __future__ import annotations

import argparse
import csv
import json
import random
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_ROOT = REPO_ROOT / "src"

for path in (SRC_ROOT, REPO_ROOT):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

from python_reference import CtrlSignals, LocalSamples, local_diff_reference


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="Simulation configuration JSON")
    parser.add_argument("output", type=Path, help="Destination CSV file")
    parser.add_argument("--cases", type=int, default=64, help="Number of cases per orientation")
    parser.add_argument("--seed", type=int, default=0x1234, help="Random seed")
    return parser.parse_args()


def _sample_value(depth: int, signed: bool, rng: random.Random) -> int:
    if signed:
        lo = -(1 << (depth - 1))
        hi = (1 << (depth - 1)) - 1
    else:
        lo = 0
        hi = (1 << depth) - 1
    return rng.randint(lo, hi)


def main() -> None:
    args = _parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    params = config["parameters"]
    image = config["images"][0]
    depth = int(params["D"])
    signed = str(image.get("signed", "false")).lower() == "true"

    rng = random.Random(args.seed)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "column_oriented",
        "first_line",
        "first_in_line",
        "last_in_line",
        "cur",
        "north",
        "north_east",
        "north_west",
        "west",
        "local_sum",
        "d_c",
        "d_n",
        "d_nw",
        "d_w",
    ]

    with args.output.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        for column_oriented in (False, True):
            for _ in range(args.cases):
                ctrl = CtrlSignals(
                    first_line=bool(rng.randrange(2)),
                    first_in_line=bool(rng.randrange(2)),
                    last_in_line=bool(rng.randrange(2)),
                )
                samples = LocalSamples(
                    cur=_sample_value(depth, signed, rng),
                    north=_sample_value(depth, signed, rng),
                    north_east=_sample_value(depth, signed, rng),
                    north_west=_sample_value(depth, signed, rng),
                    west=_sample_value(depth, signed, rng),
                )
                outputs = local_diff_reference(ctrl, samples, column_oriented)
                writer.writerow(
                    {
                        "column_oriented": int(column_oriented),
                        "first_line": int(ctrl.first_line),
                        "first_in_line": int(ctrl.first_in_line),
                        "last_in_line": int(ctrl.last_in_line),
                        "cur": samples.cur,
                        "north": samples.north,
                        "north_east": samples.north_east,
                        "north_west": samples.north_west,
                        "west": samples.west,
                        "local_sum": outputs.local_sum,
                        "d_c": outputs.d_c,
                        "d_n": outputs.d_n,
                        "d_nw": outputs.d_nw,
                        "d_w": outputs.d_w,
                    }
                )


if __name__ == "__main__":
    main()
