"""Cocotb testbench for the `local_diff` module."""

from __future__ import annotations

import csv
import os
from collections import deque
from pathlib import Path

import cocotb
from cocotb.binary import BinaryValue
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge

from python_reference import CtrlSignals, LocalSamples, local_diff_reference


@cocotb.test()
async def local_diff_matches_reference(dut) -> None:
    """Stimulate the HDL with shared vectors and compare against the Python model."""

    column_oriented = bool(int(os.environ.get("LOCAL_DIFF_COLUMN_ORIENTED", "0")))
    csv_path = Path(os.environ.get("LOCAL_DIFF_VECTOR_CSV", Path(__file__).resolve().parents[2] / "tests" / "vectors" / "local_diff_vectors.csv"))
    if not csv_path.exists():
        raise FileNotFoundError(f"Vector CSV not found at {csv_path}")

    vectors: list[dict[str, int]] = []
    with csv_path.open("r", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if int(row["column_oriented"]) == int(column_oriented):
                vectors.append({key: int(value) for key, value in row.items()})

    if not vectors:
        raise RuntimeError("No vectors loaded for selected orientation")

    dut.aresetn.value = 0
    dut.in_valid.value = 0
    dut.in_z.value = 0
    dut.in_prev_s.value = 0
    await cocotb.start(Clock(dut.clk, 5, units="ns").start())
    for _ in range(3):
        await RisingEdge(dut.clk)
    dut.aresetn.value = 1

    pending = deque()

    for vector in vectors:
        ctrl = CtrlSignals(
            first_line=bool(vector["first_line"]),
            first_in_line=bool(vector["first_in_line"]),
            last_in_line=bool(vector["last_in_line"]),
        )
        samples = LocalSamples(
            cur=vector["cur"],
            north=vector["north"],
            north_east=vector["north_east"],
            north_west=vector["north_west"],
            west=vector["west"],
        )
        expected = local_diff_reference(ctrl, samples, column_oriented)
        pending.append((ctrl, samples, expected))

        dut.in_valid.value = 1
        dut.in_ctrl.first_line.value = int(ctrl.first_line)
        dut.in_ctrl.first_in_line.value = int(ctrl.first_in_line)
        dut.in_ctrl.last_in_line.value = int(ctrl.last_in_line)
        dut.in_ctrl.last.value = 0
        dut.in_ctrl.scale_exponent.value = 0
        dut.s_cur.value = _to_signed(samples.cur, dut.s_cur.value.n_bits)
        dut.s_n.value = _to_signed(samples.north, dut.s_n.value.n_bits)
        dut.s_ne.value = _to_signed(samples.north_east, dut.s_ne.value.n_bits)
        dut.s_nw.value = _to_signed(samples.north_west, dut.s_nw.value.n_bits)
        dut.s_w.value = _to_signed(samples.west, dut.s_w.value.n_bits)

        await RisingEdge(dut.clk)

        if dut.out_valid.value and pending:
            ctrl_ref, sample_ref, expected_ref = pending.popleft()
            _check_outputs(dut, expected_ref, ctrl_ref, sample_ref)

    dut.in_valid.value = 0
    width = dut.s_cur.value.n_bits
    dut.s_cur.value = _to_signed(0, width)
    dut.s_n.value = _to_signed(0, width)
    dut.s_ne.value = _to_signed(0, width)
    dut.s_nw.value = _to_signed(0, width)
    dut.s_w.value = _to_signed(0, width)

    for _ in range(5):
        await RisingEdge(dut.clk)
        if dut.out_valid.value and pending:
            ctrl_ref, sample_ref, expected_ref = pending.popleft()
            _check_outputs(dut, expected_ref, ctrl_ref, sample_ref)

    if pending:
        raise AssertionError(f"Pipeline did not drain, {len(pending)} vectors left")


def _check_outputs(dut, expected, ctrl, samples) -> None:
    actual_local_sum = dut.local_sum.value.signed_integer
    actual_d_c = dut.d_c.value.signed_integer
    actual_d_n = dut.d_n.value.signed_integer
    actual_d_nw = dut.d_nw.value.signed_integer
    actual_d_w = dut.d_w.value.signed_integer

    assert actual_local_sum == expected.local_sum, _format_message("local_sum", expected.local_sum, actual_local_sum, ctrl, samples)
    assert actual_d_c == expected.d_c, _format_message("d_c", expected.d_c, actual_d_c, ctrl, samples)
    assert actual_d_n == expected.d_n, _format_message("d_n", expected.d_n, actual_d_n, ctrl, samples)
    assert actual_d_nw == expected.d_nw, _format_message("d_nw", expected.d_nw, actual_d_nw, ctrl, samples)
    assert actual_d_w == expected.d_w, _format_message("d_w", expected.d_w, actual_d_w, ctrl, samples)


def _format_message(name: str, expected: int, actual: int, ctrl: CtrlSignals, samples: LocalSamples) -> str:
    return (
        f"Mismatch for {name}: expected {expected}, got {actual}. "
        f"ctrl=({int(ctrl.first_line)}, {int(ctrl.first_in_line)}, {int(ctrl.last_in_line)}) "
        f"samples=({samples.cur}, {samples.north}, {samples.north_east}, {samples.north_west}, {samples.west})"
    )


def _to_signed(value: int, width: int) -> BinaryValue:
    return BinaryValue(value=value, n_bits=width, bigEndian=False, binaryRepresentation=BinaryValue.SIGNED)
