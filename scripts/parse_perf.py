#!/usr/bin/env python3
# scripts/parse_perf.py
# Reads raw JSON output from Google Benchmark, extracts hardware parameters
# needed for the roofline model, and writes a consolidated summary.json.
#
# Usage:
#   python3 scripts/parse_perf.py <results_dir>
#
# Writes to stdout; redirect to results_dir/summary.json.

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Utility: load a JSON file, return {} on failure
# ---------------------------------------------------------------------------

def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        with open(path) as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError) as exc:
        print(f"WARNING: could not load {path}: {exc}", file=sys.stderr)
        return {}


# ---------------------------------------------------------------------------
# CPU model string from /proc/cpuinfo
# ---------------------------------------------------------------------------

def read_cpu_model() -> str:
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return "Unknown CPU"


# ---------------------------------------------------------------------------
# Extract latency plateaus from latency.json
# Returns dict: {"L1": ns, "L2": ns, "L3": ns, "DRAM": ns}
# Each value is the median ns_per_load for working-set sizes in that level.
# ---------------------------------------------------------------------------

def extract_latencies(data: dict) -> dict[str, float]:
    latencies: dict[str, list[float]] = {"L1": [], "L2": [], "L3": [], "DRAM": []}

    for bm in data.get("benchmarks", []):
        label: str = bm.get("label", "")
        ns_per_load: Optional[float] = bm.get("ns_per_load", None)

        if ns_per_load is None:
            ns_per_load = bm.get("counters", {}).get("ns_per_load", None)

        if ns_per_load is not None and label in latencies:
            latencies[label].append(float(ns_per_load))

    result: dict[str, float] = {}
    for level, vals in latencies.items():
        if vals:
            vals_sorted = sorted(vals)
            result[level] = vals_sorted[len(vals_sorted) // 2]  # median

    return result


# ---------------------------------------------------------------------------
# Extract peak GFLOPS from flops.json
# Reports the maximum GFLOPS counter across all SP benchmark repetitions.
# ---------------------------------------------------------------------------

def extract_peak_gflops(data: dict) -> float:
    best = 0.0
    for bm in data.get("benchmarks", []):
        name: str = bm.get("name", "")
        if "SP" not in name and "NEON" not in name:
            continue
        gflops = bm.get("GFLOPS", None)
        if gflops is None:
            gflops = bm.get("counters", {}).get("GFLOPS", None)
        if gflops is not None:
            best = max(best, float(gflops))
    return best


# ---------------------------------------------------------------------------
# Extract peak bandwidth (GB/s) from bandwidth.json
# Returns dict with keys: "L1", "L2", "L3", "DRAM"
# ---------------------------------------------------------------------------

def extract_bandwidths(data: dict) -> dict[str, float]:
    bw: dict[str, list[float]] = {"L1": [], "L2": [], "L3": [], "DRAM": []}

    for bm in data.get("benchmarks", []):
        label: str = bm.get("label", "")
        bw_val = bm.get("BW_GB_s", None)
        if bw_val is None:
            bw_val = bm.get("counters", {}).get("BW_GB_s", None)
        if bw_val is not None and label in bw:
            bw[label].append(float(bw_val))

    result: dict[str, float] = {}
    for level, vals in bw.items():
        if vals:
            result[level] = max(vals)  # report peak, not average

    return result


# ---------------------------------------------------------------------------
# Extract GEMM tile results from gemm.json
# ---------------------------------------------------------------------------

def extract_gemm(data: dict) -> list[dict]:
    results = []
    for bm in data.get("benchmarks", []):
        label = bm.get("label", "")
        gflops = bm.get("GFLOPS", bm.get("counters", {}).get("GFLOPS", 0))
        mc = kc = nc = 0
        for part in label.split():
            # Guard against label parts that do not contain '='
            if "=" not in part:
                continue
            k, _, v = part.partition("=")
            try:
                if k == "Mc":   mc = int(v)
                elif k == "Kc": kc = int(v)
                elif k == "Nc": nc = int(v)
            except ValueError:
                pass
        results.append({"Mc": mc, "Kc": kc, "Nc": nc,
                         "gflops": float(gflops), "label": label})
    return results


# ---------------------------------------------------------------------------
# Build kernel list for the roofline plot
# Each entry: {name, arithmetic_intensity, gflops, gflops_ci_95}
#
# Arithmetic intensity for STREAM Triad:
#   2 FLOPs (one FMA) / 12 bytes (2 reads + 1 write x 4 bytes each)
#   = 0.1667 FLOP/byte
# STREAM Copy has 0 FLOPs -- shown on the plot to mark the bandwidth roof.
# ---------------------------------------------------------------------------

STREAM_KERNEL_SPECS = [
    {"name": "STREAM Triad (DRAM)",
     "arithmetic_intensity": 2.0 / 12.0,
     "level": "DRAM"},
    {"name": "STREAM Copy (DRAM)",
     "arithmetic_intensity": 0.0,    # pure memory movement, zero FLOPs
     "level": "DRAM"},
]


def build_kernel_list(bandwidths: dict[str, float]) -> list[dict]:
    """
    Build roofline kernel entries from measured bandwidth data.
    At the memory-bound limit, performance = AI * bandwidth.
    """
    kernels = []
    for spec in STREAM_KERNEL_SPECS:
        bw = bandwidths.get(spec["level"], 0.0)
        kernels.append({
            "name": spec["name"],
            "arithmetic_intensity": spec["arithmetic_intensity"],
            "gflops": bw * spec["arithmetic_intensity"],
            "gflops_ci_95": 0.0,
        })
    return kernels


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: parse_perf.py <results_dir>", file=sys.stderr)
        sys.exit(1)

    results_dir = Path(sys.argv[1])

    latency_data   = load_json(results_dir / "latency.json")
    bandwidth_data = load_json(results_dir / "bandwidth.json")
    flops_data     = load_json(results_dir / "flops.json")
    gemm_data      = load_json(results_dir / "gemm.json")

    latencies    = extract_latencies(latency_data)
    bandwidths   = extract_bandwidths(bandwidth_data)
    peak_gflops  = extract_peak_gflops(flops_data)
    gemm_results = extract_gemm(gemm_data)
    kernels      = build_kernel_list(bandwidths)

    # Read CPU frequency from sysfs (kHz -> GHz)
    cpu_freq_ghz: Optional[float] = None
    try:
        with open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq") as f:
            cpu_freq_ghz = int(f.read().strip()) / 1_000_000.0
    except OSError:
        pass

    summary = {
        "cpu_model":         read_cpu_model(),
        "cpu_freq_ghz":      cpu_freq_ghz,
        "latency_ns":        latencies,
        "peak_bw_dram_gbs":  bandwidths.get("DRAM", 0.0),
        "peak_bw_l3_gbs":    bandwidths.get("L3",   0.0),
        "peak_bw_l2_gbs":    bandwidths.get("L2",   0.0),
        "peak_bw_l1_gbs":    bandwidths.get("L1",   0.0),
        "peak_gflops_sp":    peak_gflops,
        "gemm_tile_results": gemm_results,
        "kernels":           kernels,
    }

    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
