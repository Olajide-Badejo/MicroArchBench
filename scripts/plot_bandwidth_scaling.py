#!/usr/bin/env python3
# scripts/plot_bandwidth_scaling.py
# Generates a thread-count vs bandwidth plot from the BM_Triad_MT_Scaling
# results in bandwidth.json.
#
# Usage:
#   python3 scripts/plot_bandwidth_scaling.py <results_dir> [--output <plot_dir>]
#
# The script reads bandwidth.json from <results_dir>, extracts the
# BM_Triad_MT_Scaling entries, and writes bandwidth_scaling.pdf and
# bandwidth_scaling.png to <plot_dir>.

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Plot bandwidth vs thread count from stream_bench JSON output.")
    p.add_argument("results_dir", type=Path,
                   help="Directory containing bandwidth.json")
    p.add_argument("--output", "-o", type=Path, default=Path("results/plots"),
                   help="Output directory for PDF and PNG")
    return p.parse_args()


def load_mt_data(bw_path: Path) -> list[tuple[int, float]]:
    """
    Parse BM_Triad_MT_Scaling benchmark entries from bandwidth.json.
    Returns a list of (thread_count, bw_gb_s) tuples sorted by thread count.
    """
    if not bw_path.exists():
        print("ERROR: {} not found".format(bw_path), file=sys.stderr)
        sys.exit(1)

    with open(bw_path) as f:
        data = json.load(f)

    points: dict[int, list[float]] = {}

    for bm in data.get("benchmarks", []):
        name: str = bm.get("name", "")
        if "MT_Scaling" not in name:
            continue

        # Thread count is state.counters["threads"] or parsed from the label
        threads = int(float(bm.get("threads",
                    bm.get("counters", {}).get("threads", 0))))
        if threads == 0:
            # Fall back: parse from benchmark name BM_Triad_MT_Scaling/N
            parts = name.split("/")
            if len(parts) >= 2:
                try:
                    threads = int(parts[-1])
                except ValueError:
                    continue

        bw = bm.get("BW_GB_s", bm.get("counters", {}).get("BW_GB_s", None))
        if bw is not None and threads > 0:
            points.setdefault(threads, []).append(float(bw))

    if not points:
        print("WARNING: no BM_Triad_MT_Scaling entries found in {}".format(bw_path),
              file=sys.stderr)
        return []

    # Return median per thread count
    result = []
    for t, vals in sorted(points.items()):
        vals_sorted = sorted(vals)
        median = vals_sorted[len(vals_sorted) // 2]
        result.append((t, median))

    return result


def plot(points: list[tuple[int, float]], output_dir: Path,
         cpu_model: str = "") -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    if not points:
        print("No data to plot.", file=sys.stderr)
        return

    threads = [p[0] for p in points]
    bws     = [p[1] for p in points]

    # Ideal linear scaling line (from 1-thread result)
    bw_1t = bws[0] if bws else 0.0
    ideal  = [bw_1t * t for t in threads]

    fig, ax = plt.subplots(figsize=(9, 5))
    fig.patch.set_facecolor("#FAFAFA")
    ax.set_facecolor("#FAFAFA")

    # Ideal scaling reference
    ax.plot(threads, ideal, color="#BDBDBD", lw=1.5, ls="--",
            label="Ideal linear scaling", zorder=2)

    # Measured bandwidth
    ax.plot(threads, bws, color="#2196F3", lw=2.5, marker="o",
            ms=7, label="Measured (AVX2 NT Triad)", zorder=3)

    # Annotate peak
    peak_bw = max(bws)
    peak_t  = threads[bws.index(peak_bw)]
    ax.annotate("Peak: {:.1f} GB/s @ {}T".format(peak_bw, peak_t),
                xy=(peak_t, peak_bw),
                xytext=(peak_t + max(1, len(threads) // 8), peak_bw * 0.93),
                fontsize=9, color="#0D47A1",
                arrowprops=dict(arrowstyle="->", color="#0D47A1", lw=1.0))

    # Indicate where HT siblings start (if detectable -- assume physical cores
    # are half the logical count; this is a heuristic only)
    try:
        import subprocess
        physical = int(subprocess.check_output(
            "grep -c ^processor /proc/cpuinfo", shell=True).decode().strip())
        # Count unique core_ids as physical core count
        core_ids = subprocess.check_output(
            "sort -u /sys/devices/system/cpu/cpu*/topology/core_id 2>/dev/null | wc -l",
            shell=True).decode().strip()
        phys = int(core_ids) if core_ids else physical // 2
        if 1 < phys < max(threads):
            ax.axvline(phys, color="#FF9800", lw=1.2, ls=":",
                       alpha=0.8, label="Physical core boundary ({})".format(phys))
    except Exception:
        pass  # sysfs unavailable (CI environment) -- skip annotation

    ax.set_xlabel("Thread count", fontsize=12, labelpad=8)
    ax.set_ylabel("Bandwidth (GB/s)", fontsize=12, labelpad=8)
    title = "DRAM Bandwidth Scaling (AVX2 NT Triad)"
    if cpu_model:
        title += "\n{}".format(cpu_model)
    ax.set_title(title, fontsize=13, fontweight="bold", pad=10)

    ax.set_xlim(0.5, max(threads) + 0.5)
    ax.set_ylim(0, max(max(bws), max(ideal)) * 1.15)
    ax.xaxis.set_major_locator(ticker.MaxNLocator(integer=True))
    ax.grid(True, alpha=0.25, color="#BDBDBD")
    ax.legend(fontsize=10, framealpha=0.9)

    plt.tight_layout()

    pdf_path = output_dir / "bandwidth_scaling.pdf"
    png_path = output_dir / "bandwidth_scaling.png"
    fig.savefig(pdf_path, bbox_inches="tight", dpi=300)
    fig.savefig(png_path, bbox_inches="tight", dpi=150)
    plt.close(fig)

    print("Saved: {}".format(pdf_path))
    print("Saved: {}".format(png_path))


def main() -> None:
    args = parse_args()

    bw_path = args.results_dir / "bandwidth.json"
    points  = load_mt_data(bw_path)

    # Try to read CPU model from summary.json if it exists alongside bandwidth.json
    cpu_model = ""
    summary_path = args.results_dir / "summary.json"
    if summary_path.exists():
        try:
            with open(summary_path) as f:
                cpu_model = json.load(f).get("cpu_model", "")
        except Exception:
            pass

    plot(points, args.output, cpu_model)

    # Print a quick table to stdout
    if points:
        print()
        print("{:>8s}  {:>10s}  {:>8s}".format("Threads", "BW (GB/s)", "Efficiency"))
        print("-" * 32)
        bw_1t = points[0][1]
        for t, bw in points:
            eff = bw / (bw_1t * t) * 100
            print("{:>8d}  {:>10.2f}  {:>7.1f}%".format(t, bw, eff))


if __name__ == "__main__":
    main()
