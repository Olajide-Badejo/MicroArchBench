#!/usr/bin/env python3
# scripts/tile_predict.py
# GEMM tile-size predictor using the BLIS analytical cache-blocking model.
# Reads hardware parameters from summary.json and prints recommended
# (Mc, Kc, Nc) tile sizes for a blocked FP32 GEMM on the measured hardware.
#
# Reference: Van Zee & van de Geijn, "BLIS: A Framework for Rapidly
# Instantiating BLAS Functionality", ACM TOMS 41(3), 2015.
#
# Usage:
#   python3 scripts/tile_predict.py <summary.json>

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path


# ---------------------------------------------------------------------------
# Hardware parameter model
# ---------------------------------------------------------------------------

@dataclass
class HardwareParams:
    # Cache capacities in bytes
    l1_bytes: int
    l2_bytes: int
    l3_bytes: int

    # FMA-unit and SIMD characteristics
    fma_units: int        # number of independent FMA execution ports
    simd_width_floats: int  # floats per vector register (8 for AVX2 SP)
    n_accum_registers: int  # number of independent accumulator registers to use

    # Measured peak values for reporting
    peak_gflops_sp: float
    peak_bw_dram_gbs: float

    @property
    def ridge_point(self) -> float:
        """Arithmetic intensity at which compute = memory limit (FLOP/byte)."""
        if self.peak_bw_dram_gbs <= 0:
            return float("inf")
        return self.peak_gflops_sp / self.peak_bw_dram_gbs


# ---------------------------------------------------------------------------
# Tile-size result
# ---------------------------------------------------------------------------

@dataclass
class GemmTiles:
    Mc: int  # rows of C tile (register block)
    Kc: int  # depth of A and B panels
    Nc: int  # cols of B panel (L2 block)

    def fits_l1(self, hw: HardwareParams) -> bool:
        a_panel = self.Mc * self.Kc * 4  # bytes, FP32
        return a_panel <= hw.l1_bytes * 0.8

    def fits_l2(self, hw: HardwareParams) -> bool:
        b_panel = self.Kc * self.Nc * 4
        return b_panel <= hw.l2_bytes * 0.8

    def summary(self) -> str:
        return "Mc={:4d},  Kc={:4d},  Nc={:6d}".format(self.Mc, self.Kc, self.Nc)


# ---------------------------------------------------------------------------
# Analytical model
# ---------------------------------------------------------------------------

def predict_gemm_tiles(hw: HardwareParams) -> GemmTiles:
    """
    Solve for (Mc, Kc, Nc) using the BLIS packing hierarchy:

    Level 0 -- registers:
        The Mc x Nc micro-tile of C lives in registers.
        Mc = nr (row-panel width) and Nc = mr (col-panel width) in BLIS notation.
        We derive nr = fma_units * simd_width and mr = n_accum_registers.

    Level 1 -- L1 cache:
        The A panel (Mc x Kc) must fit in L1 together with Mc x Nc of C.
        We solve for Kc.

    Level 2 -- L2 cache:
        The B panel (Kc x Nc) must fit in L2.
        Nc is set to fit B given the Kc derived above.
    """
    bytes_per_float = 4

    # Register tile: number of accumulators determines the micro-kernel shape
    # Mc (rows) corresponds to SIMD register width * FMA units
    Mc = hw.fma_units * hw.simd_width_floats
    # Nc (cols) uses additional accumulator registers
    Nc_reg = hw.n_accum_registers

    # Kc: A panel (Mc x Kc) in L1, leaving 20% headroom for TLB and other data
    l1_usable = int(hw.l1_bytes * 0.8)
    Kc = l1_usable // (Mc * bytes_per_float)

    if Kc < 1:
        Kc = 1  # degenerate fallback

    # Nc: B panel (Kc x Nc) in L2, with 20% headroom
    l2_usable = int(hw.l2_bytes * 0.8)
    Nc = l2_usable // (Kc * bytes_per_float)

    if Nc < Nc_reg:
        # If Nc is smaller than the register tile, Kc is too large; shrink it
        Nc = Nc_reg
        Kc = l2_usable // (Nc * bytes_per_float)
        # Recompute Kc so A panel fits in L1
        Kc = min(Kc, l1_usable // (Mc * bytes_per_float))

    # Round down to multiples of SIMD width for alignment
    w = hw.simd_width_floats
    Mc = max(w, (Mc // w) * w)
    Nc = max(w, (Nc // w) * w)
    Kc = max(1, Kc)

    return GemmTiles(Mc=Mc, Kc=Kc, Nc=Nc)


# ---------------------------------------------------------------------------
# Detect hardware parameters from summary.json
# Uses sysfs as ground truth; falls back to summary.json bandwidth numbers.
# ---------------------------------------------------------------------------

def hw_params_from_summary(summary: dict) -> HardwareParams:
    # Cache sizes from sysfs (preferred) or conservative defaults
    def sysfs_cache(index: int, default: int) -> int:
        try:
            p = "/sys/devices/system/cpu/cpu0/cache/index{}/size".format(index)
            with open(p) as f:
                raw = f.read().strip()
            mul = 1
            if raw.endswith("K"): mul = 1024;      raw = raw[:-1]
            elif raw.endswith("M"): mul = 1024*1024; raw = raw[:-1]
            return int(raw) * mul
        except OSError:
            return default

    l1 = sysfs_cache(0, 32   * 1024)
    l2 = sysfs_cache(2, 256  * 1024)
    l3 = sysfs_cache(3, 8    * 1024 * 1024)

    # SIMD characteristics -- detect from compile-time flags embedded in
    # the peak_gflops benchmark name if available; otherwise assume AVX2.
    # AVX2: 8 SP floats / register, 2 FMA ports (Skylake+), 10 accumulators
    fma_units          = 2
    simd_width_floats  = 8   # AVX2 SP
    n_accum_registers  = 10  # matches UNROLL in peak_flops.cpp

    peak_gflops    = float(summary.get("peak_gflops_sp",   100.0) or 100.0)
    peak_bw_dram   = float(summary.get("peak_bw_dram_gbs",  50.0) or  50.0)

    return HardwareParams(
        l1_bytes           = l1,
        l2_bytes           = l2,
        l3_bytes           = l3,
        fma_units          = fma_units,
        simd_width_floats  = simd_width_floats,
        n_accum_registers  = n_accum_registers,
        peak_gflops_sp     = peak_gflops,
        peak_bw_dram_gbs   = peak_bw_dram,
    )


# ---------------------------------------------------------------------------
# Pretty-print results table
# ---------------------------------------------------------------------------

def print_report(hw: HardwareParams, tiles: GemmTiles,
                 gemm_results: list[dict]) -> None:
    print()
    print("=" * 60)
    print("  GEMM Tile-Size Prediction (FP32, BLIS model)")
    print("=" * 60)
    print()
    print("Hardware parameters:")
    print("  L1 data cache    : {:8.1f} KiB".format(hw.l1_bytes / 1024))
    print("  L2 cache         : {:8.1f} KiB".format(hw.l2_bytes / 1024))
    print("  L3 cache         : {:8.1f} MiB".format(hw.l3_bytes / 1024 / 1024))
    print("  FMA units        : {:8d}".format(hw.fma_units))
    print("  SIMD width (SP)  : {:8d} floats".format(hw.simd_width_floats))
    print("  Peak SP GFLOPS   : {:8.1f}".format(hw.peak_gflops_sp))
    print("  Peak DRAM BW     : {:8.1f} GB/s".format(hw.peak_bw_dram_gbs))
    print("  Ridge point      : {:8.3f} FLOP/byte".format(hw.ridge_point))
    print()
    print("Predicted optimal tiles:")
    print("  {}".format(tiles.summary()))
    print()
    print("  A panel (Mc x Kc): {:6.1f} KiB -- L1 fit: {}".format(
        tiles.Mc * tiles.Kc * 4 / 1024,
        "yes" if tiles.fits_l1(hw) else "NO (check L1 size)"))
    print("  B panel (Kc x Nc): {:6.1f} KiB -- L2 fit: {}".format(
        tiles.Kc * tiles.Nc * 4 / 1024,
        "yes" if tiles.fits_l2(hw) else "NO (check L2 size)"))
    print()

    if gemm_results:
        print("Validation (blocked GEMM benchmark results):")
        print("  {:<40s}  {:>10s}".format("Tile configuration", "GFLOPS"))
        print("  " + "-" * 55)
        best = max(gemm_results, key=lambda r: r.get("gflops", 0))
        for r in sorted(gemm_results, key=lambda r: -r.get("gflops", 0)):
            marker = " <-- predicted best" if r is best else ""
            print("  {:<40s}  {:>8.1f}{}".format(
                r.get("label", "?"),
                r.get("gflops", 0),
                marker))
        print()

    print("=" * 60)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: tile_predict.py <summary.json>", file=sys.stderr)
        sys.exit(1)

    path = Path(sys.argv[1])
    if not path.exists():
        print("ERROR: {} not found".format(path), file=sys.stderr)
        sys.exit(1)

    with open(path) as f:
        summary = json.load(f)

    hw     = hw_params_from_summary(summary)
    tiles  = predict_gemm_tiles(hw)
    gemm_r = summary.get("gemm_tile_results", [])

    print_report(hw, tiles, gemm_r)


if __name__ == "__main__":
    main()
