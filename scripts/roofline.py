#!/usr/bin/env python3
# scripts/roofline.py
# Generates a roofline model plot from summary.json produced by parse_perf.py.
# Saves the plot as roofline.pdf (and roofline.png) in the specified output dir.
#
# Usage:
#   python3 scripts/roofline.py <summary.json> --output <plot_dir>

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")  # non-interactive backend for headless/server use
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Generate roofline model plot from benchmark summary JSON.")
    p.add_argument("summary", type=Path,
                   help="Path to summary.json produced by parse_perf.py")
    p.add_argument("--output", "-o", type=Path, default=Path("results/plots"),
                   help="Directory to write roofline.pdf and roofline.png")
    return p.parse_args()


# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------

def ridge_point(peak_gflops: float, peak_bw: float) -> float:
    """AI at which compute and memory limits intersect."""
    if peak_bw <= 0:
        return float("inf")
    return peak_gflops / peak_bw


def roofline_curve(ai: np.ndarray, peak_gflops: float,
                   peak_bw: float) -> np.ndarray:
    """Element-wise minimum of the compute roof and the bandwidth slope."""
    return np.minimum(peak_gflops, peak_bw * ai)


# ---------------------------------------------------------------------------
# Main plot function
# ---------------------------------------------------------------------------

def plot_roofline(summary: dict, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    peak_gflops      = float(summary.get("peak_gflops_sp",    0) or 0)
    peak_bw_dram     = float(summary.get("peak_bw_dram_gbs",  0) or 0)
    peak_bw_l3       = float(summary.get("peak_bw_l3_gbs",    0) or 0)
    peak_bw_l2       = float(summary.get("peak_bw_l2_gbs",    0) or 0)
    cpu_model        = summary.get("cpu_model", "Unknown CPU")
    kernels          = summary.get("kernels", [])

    # Guard against missing measurements -- show placeholder if data is absent
    if peak_gflops == 0:
        print("WARNING: peak_gflops_sp is 0. Run peak_flops benchmark first.",
              file=sys.stderr)
        peak_gflops = 100.0  # placeholder so the plot still renders

    if peak_bw_dram == 0:
        print("WARNING: peak_bw_dram_gbs is 0. Run stream_bench first.",
              file=sys.stderr)
        peak_bw_dram = 50.0  # placeholder

    # Arithmetic intensity range: 1/100 FLOP/byte to 10000 FLOP/byte
    ai_range = np.logspace(-2, 4, 2000)

    fig, ax = plt.subplots(figsize=(11, 7))
    fig.patch.set_facecolor("#FAFAFA")
    ax.set_facecolor("#FAFAFA")

    # ---- Bandwidth roof lines ----
    bw_specs = []
    if peak_bw_dram > 0:
        bw_specs.append((peak_bw_dram, "DRAM BW ({:.0f} GB/s)".format(peak_bw_dram),
                         "#2196F3", "-"))
    if peak_bw_l3 > 0:
        bw_specs.append((peak_bw_l3,   "L3 BW ({:.0f} GB/s)".format(peak_bw_l3),
                         "#FF9800", "--"))
    if peak_bw_l2 > 0:
        bw_specs.append((peak_bw_l2,   "L2 BW ({:.0f} GB/s)".format(peak_bw_l2),
                         "#4CAF50", "-."))

    for bw, label, color, ls in bw_specs:
        roof = roofline_curve(ai_range, peak_gflops, bw)
        ax.loglog(ai_range, roof, color=color, lw=2.5,
                  linestyle=ls, label=label, zorder=3)

    # ---- Compute ceiling ----
    ax.axhline(peak_gflops, color="#E53935", lw=2.5, ls="--",
               label="Peak SP GFLOPS ({:.0f})".format(peak_gflops), zorder=3)

    # ---- Ridge point annotation (DRAM) ----
    # Recompute after any placeholder substitution above.
    rp = ridge_point(peak_gflops, peak_bw_dram)
    # Clamp to the plot's x range so fill_betweenx always has a finite boundary.
    rp_clamped = min(rp, 1e4) if math.isfinite(rp) else 1e4
    if math.isfinite(rp):
        ax.axvline(rp, color="#9E9E9E", lw=1.0, ls=":", alpha=0.7, zorder=2)
        ax.text(rp * 1.05, peak_gflops * 0.6,
                "Ridge\n{:.2f} FLOP/B".format(rp),
                fontsize=8, color="#616161", va="top")

    # ---- Kernel data points ----
    for kernel in kernels:
        ai   = float(kernel.get("arithmetic_intensity", 0))
        perf = float(kernel.get("gflops", 0))
        ci   = float(kernel.get("gflops_ci_95", 0))
        name = kernel.get("name", "?")

        if ai <= 0 or perf <= 0:
            continue

        ax.errorbar(ai, perf, yerr=ci if ci > 0 else None,
                    fmt="o", ms=9, color="#212121",
                    ecolor="#757575", capsize=5, zorder=5)
        ax.annotate(name, (ai, perf),
                    textcoords="offset points", xytext=(8, 4),
                    fontsize=8, color="#212121")

    # ---- Axes formatting ----
    ax.set_xlabel("Arithmetic Intensity (FLOP / byte)", fontsize=13, labelpad=8)
    ax.set_ylabel("Performance (GFLOPS / s)", fontsize=13, labelpad=8)
    ax.set_title("Roofline Model -- {}".format(cpu_model),
                 fontsize=14, fontweight="bold", pad=12)

    ax.set_xlim(1e-2, 1e4)
    ax.set_ylim(0.1, peak_gflops * 3)

    ax.xaxis.set_major_formatter(ticker.LogFormatterSciNotation())
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())

    ax.grid(True, which="both", alpha=0.25, color="#BDBDBD", zorder=1)
    ax.legend(loc="upper left", fontsize=10, framealpha=0.9)

    # ---- Memory-bound / compute-bound shading ----
    ax.fill_betweenx([0.1, peak_gflops * 3],
                      1e-2, rp_clamped,
                      alpha=0.04, color="#2196F3", zorder=0,
                      label="_memory region")
    ax.fill_betweenx([0.1, peak_gflops * 3],
                      rp_clamped, 1e4,
                      alpha=0.04, color="#E53935", zorder=0,
                      label="_compute region")

    ax.text(0.015, peak_gflops * 2.0, "Memory\nBound",
            fontsize=9, color="#1565C0", alpha=0.7)
    ax.text(500, peak_gflops * 2.0, "Compute\nBound",
            fontsize=9, color="#B71C1C", alpha=0.7)

    plt.tight_layout()

    pdf_path = output_dir / "roofline.pdf"
    png_path = output_dir / "roofline.png"
    fig.savefig(pdf_path, bbox_inches="tight", dpi=300)
    fig.savefig(png_path, bbox_inches="tight", dpi=150)
    plt.close(fig)

    print("Saved: {}".format(pdf_path))
    print("Saved: {}".format(png_path))


# ---------------------------------------------------------------------------
# Bandwidth scaling plot (thread count vs GB/s)
# Reads raw bandwidth.json if available alongside summary.json
# ---------------------------------------------------------------------------

def plot_bandwidth_scaling(summary_path: Path, output_dir: Path) -> None:
    bw_path = summary_path.parent / "bandwidth.json"
    if not bw_path.exists():
        return

    with open(bw_path) as f:
        data = json.load(f)

    # Group by working-set size label
    by_label: dict[str, list[float]] = {}
    for bm in data.get("benchmarks", []):
        label = bm.get("label", "")
        bw = bm.get("BW_GB_s", bm.get("counters", {}).get("BW_GB_s", None))
        if label and bw is not None:
            by_label.setdefault(label, []).append(float(bw))

    if not by_label:
        return

    fig, ax = plt.subplots(figsize=(8, 5))
    fig.patch.set_facecolor("#FAFAFA")
    ax.set_facecolor("#FAFAFA")

    colors = {"L1": "#4CAF50", "L2": "#FF9800", "L3": "#2196F3", "DRAM": "#E53935"}
    for level in ["L1", "L2", "L3", "DRAM"]:
        vals = by_label.get(level, [])
        if not vals:
            continue
        mean_bw = sum(vals) / len(vals)
        ax.bar(level, mean_bw, color=colors.get(level, "gray"),
               edgecolor="white", zorder=3)
        ax.text(level, mean_bw + 1, "{:.1f}".format(mean_bw),
                ha="center", fontsize=9)

    ax.set_xlabel("Memory Level", fontsize=12)
    ax.set_ylabel("Peak Bandwidth (GB/s)", fontsize=12)
    ax.set_title("Bandwidth by Memory Level (Triad Kernel)", fontsize=13)
    ax.grid(True, axis="y", alpha=0.3)

    plt.tight_layout()
    out = output_dir / "bandwidth_by_level.png"
    fig.savefig(out, bbox_inches="tight", dpi=150)
    plt.close(fig)
    print("Saved: {}".format(out))


# ---------------------------------------------------------------------------
# Latency vs working-set plot
# ---------------------------------------------------------------------------

def plot_latency_curve(summary: dict, summary_path: Path,
                        output_dir: Path) -> None:
    latency_path = summary_path.parent / "latency.json"
    if not latency_path.exists():
        return

    with open(latency_path) as f:
        data = json.load(f)

    points: list[tuple[int, float, str]] = []
    for bm in data.get("benchmarks", []):
        ws = bm.get("range_x", None)
        if ws is None:
            # Try to parse from name: BM_PointerChase/4096
            name = bm.get("name", "")
            m = name.split("/")
            if len(m) >= 2:
                try:
                    ws = int(m[-1])
                except ValueError:
                    continue
            else:
                continue
        ns = bm.get("ns_per_load", bm.get("counters", {}).get("ns_per_load", None))
        label = bm.get("label", "")
        if ns is not None:
            points.append((int(ws), float(ns), label))

    if not points:
        return

    points.sort(key=lambda x: x[0])

    ws_vals  = [p[0] / 1024 for p in points]  # bytes to KiB
    ns_vals  = [p[1] for p in points]
    labels   = [p[2] for p in points]

    level_colors = {"L1": "#4CAF50", "L2": "#FF9800", "L3": "#2196F3", "DRAM": "#E53935"}

    fig, ax = plt.subplots(figsize=(10, 5))
    fig.patch.set_facecolor("#FAFAFA")
    ax.set_facecolor("#FAFAFA")

    for i in range(len(ws_vals) - 1):
        color = level_colors.get(labels[i], "#9E9E9E")
        ax.semilogx([ws_vals[i], ws_vals[i+1]], [ns_vals[i], ns_vals[i+1]],
                    color=color, lw=2, zorder=3)

    # Draw scatter points colored by level
    seen: set[str] = set()
    for ws, ns, lvl in zip(ws_vals, ns_vals, labels):
        color = level_colors.get(lvl, "#9E9E9E")
        lbl = lvl if lvl not in seen else "_nolegend_"
        seen.add(lvl)
        ax.semilogx(ws, ns, "o", color=color, ms=7, label=lbl, zorder=4)

    ax.set_xlabel("Working Set Size (KiB)", fontsize=12)
    ax.set_ylabel("Load Latency (ns)", fontsize=12)
    ax.set_title("Pointer-Chase Latency vs Working Set Size", fontsize=13)
    ax.legend(fontsize=10, framealpha=0.9)
    ax.grid(True, which="both", alpha=0.25)

    plt.tight_layout()
    out = output_dir / "latency_vs_size.png"
    fig.savefig(out, bbox_inches="tight", dpi=150)
    plt.close(fig)
    print("Saved: {}".format(out))


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()

    if not args.summary.exists():
        print("ERROR: summary file not found: {}".format(args.summary),
              file=sys.stderr)
        sys.exit(1)

    with open(args.summary) as f:
        summary = json.load(f)

    plot_roofline(summary, args.output)
    plot_bandwidth_scaling(args.summary, args.output)
    plot_latency_curve(summary, args.summary, args.output)


if __name__ == "__main__":
    main()
