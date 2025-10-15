"""Reference implementation of the CCSDS-123 local difference stage."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class CtrlSignals:
    """Control flags that describe the sample position within the image."""

    first_line: bool
    first_in_line: bool
    last_in_line: bool


@dataclass(frozen=True)
class LocalSamples:
    """Neighbourhood samples that feed the local difference logic."""

    cur: int
    north: int
    north_east: int
    north_west: int
    west: int


@dataclass(frozen=True)
class LocalDiffOutputs:
    """Outputs produced by the local difference logic."""

    local_sum: int
    d_c: int
    d_n: int
    d_nw: int
    d_w: int


def _local_sum_terms(ctrl: CtrlSignals, samples: LocalSamples, column_oriented: bool) -> tuple[int, int]:
    """Return the two terms that are accumulated to form the local sum."""
    term1 = 0
    term2 = 0
    if column_oriented:
        term1 = 4 * (samples.north if not ctrl.first_line else samples.west)
    else:
        if not ctrl.first_line and not ctrl.first_in_line and not ctrl.last_in_line:
            term1 = samples.west + samples.north_west
            term2 = samples.north + samples.north_east
        elif ctrl.first_line and not ctrl.first_in_line:
            term1 = 4 * samples.west
        elif not ctrl.first_line and ctrl.first_in_line:
            term1 = 2 * samples.north
            term2 = 2 * samples.north_east
        elif not ctrl.first_line and ctrl.last_in_line:
            term1 = samples.west + samples.north_west
            term2 = 2 * samples.north
    return term1, term2


def local_diff_reference(ctrl: CtrlSignals, samples: LocalSamples, column_oriented: bool) -> LocalDiffOutputs:
    """Compute the expected local difference outputs for a single pixel."""

    term1, term2 = _local_sum_terms(ctrl, samples, column_oriented)
    local_sum = term1 + term2

    if ctrl.first_line and ctrl.first_in_line:
        d_c = 0
        local_sum_out = 0
    else:
        d_c = 4 * samples.cur - local_sum
        local_sum_out = local_sum

    if ctrl.first_line:
        d_n = 0
    else:
        d_n = 4 * samples.north - local_sum

    if ctrl.first_line:
        d_w = 0
        d_nw = 0
    elif ctrl.first_in_line:
        reflected = 4 * samples.north - local_sum
        d_w = reflected
        d_nw = reflected
    else:
        d_w = 4 * samples.west - local_sum
        d_nw = 4 * samples.north_west - local_sum

    return LocalDiffOutputs(
        local_sum=local_sum_out,
        d_c=d_c,
        d_n=d_n,
        d_nw=d_nw,
        d_w=d_w,
    )
