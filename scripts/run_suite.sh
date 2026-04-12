#!/usr/bin/env bash
# scripts/run_suite.sh
# Full benchmark suite orchestration.
# Runs all benchmarks, collects perf counters, and calls Python post-processing.
#
# Usage:
#   bash scripts/run_suite.sh [--build-dir <dir>] [--no-freq-lock] [--help]
#
# Required tools in PATH:
#   cmake, perf, python3
# Optional:
#   cpupower (for frequency pinning), numactl (for NUMA binding)

set -euo pipefail

# ---- Argument parsing ----

BUILD_DIR="build"
FREQ_LOCK=1
NUMA_BIND=1

for arg in "$@"; do
    case "$arg" in
        --build-dir=*) BUILD_DIR="${arg#*=}" ;;
        --no-freq-lock) FREQ_LOCK=0 ;;
        --no-numa)  NUMA_BIND=0 ;;
        --help)
            echo "Usage: $0 [--build-dir=<dir>] [--no-freq-lock] [--no-numa]"
            exit 0 ;;
    esac
done

# ---- Verify build directory ----

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: build directory '$BUILD_DIR' not found."
    echo "Run: cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release ... && cmake --build $BUILD_DIR -j\$(nproc)"
    exit 1
fi

for bin in pointer_chase stream_bench peak_flops gemm_tile_bench; do
    if [[ ! -x "$BUILD_DIR/$bin" ]]; then
        echo "ERROR: $BUILD_DIR/$bin not found. Did the build complete?"
        exit 1
    fi
done

# ---- Create timestamped output directory ----

OUTDIR="results/raw/$(date +%Y%m%dT%H%M%S)"
PLOTDIR="results/plots"
mkdir -p "$OUTDIR" "$PLOTDIR"
echo "Output: $OUTDIR"

# ---- System configuration ----

if [[ $FREQ_LOCK -eq 1 ]]; then
    echo "[*] Locking CPU frequency to performance governor..."

    if command -v cpupower &>/dev/null; then
        sudo cpupower frequency-set -g performance
    else
        echo "    WARNING: cpupower not found. Skipping governor change."
        echo "    Latency results may be inaccurate due to frequency scaling."
    fi

    # Disable Intel Turbo Boost if available
    if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
        echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo > /dev/null
        echo "    Intel Turbo Boost disabled."
    fi

    # Disable AMD Boost if available
    if [[ -f /sys/devices/system/cpu/cpufreq/boost ]]; then
        echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost > /dev/null
        echo "    AMD boost disabled."
    fi
fi

# Allow perf_event_open without root (if not already set)
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "3")
if [[ "$PARANOID" -gt 1 ]]; then
    echo "[*] Setting kernel.perf_event_paranoid=1..."
    sudo sysctl -w kernel.perf_event_paranoid=1
fi

# Optionally bind to NUMA node 0
NUMA_PREFIX=""
if [[ $NUMA_BIND -eq 1 ]] && command -v numactl &>/dev/null; then
    NUMA_PREFIX="numactl --cpunodebind=0 --membind=0"
    echo "[*] NUMA binding: node 0"
fi

# ---- Helper: run benchmark with optional NUMA prefix ----

run_bench() {
    local bin="$1"; shift
    # shellcheck disable=SC2086
    $NUMA_PREFIX "$BUILD_DIR/$bin" "$@"
}

# ---- Run benchmarks ----

echo ""
echo "=== Pointer-Chase Latency ==="
run_bench pointer_chase \
    --benchmark_format=json \
    --benchmark_repetitions=3 \
    --benchmark_min_warmup_time=0.5 \
    > "$OUTDIR/latency.json"
echo "    -> $OUTDIR/latency.json"

echo ""
echo "=== STREAM Bandwidth ==="
run_bench stream_bench \
    --benchmark_format=json \
    --benchmark_repetitions=3 \
    --benchmark_min_warmup_time=0.5 \
    > "$OUTDIR/bandwidth.json"
echo "    -> $OUTDIR/bandwidth.json"

echo ""
echo "=== Peak FLOPS ==="
run_bench peak_flops \
    --benchmark_format=json \
    --benchmark_repetitions=5 \
    --benchmark_min_warmup_time=0.5 \
    > "$OUTDIR/flops.json"
echo "    -> $OUTDIR/flops.json"

echo ""
echo "=== GEMM Tile Validation ==="
run_bench gemm_tile_bench \
    --benchmark_format=json \
    > "$OUTDIR/gemm.json"
echo "    -> $OUTDIR/gemm.json"

# ---- perf stat for representative kernel (if available) ----

if command -v perf &>/dev/null; then
    echo ""
    echo "=== perf stat (Triad_AVX2_NT) ==="
    PERF_CMD="perf stat \
        -e cache-misses,LLC-load-misses,fp_arith_inst_retired.256b_packed_single \
        -j -o $OUTDIR/perf_counters.json \
        -- $NUMA_PREFIX $BUILD_DIR/stream_bench \
        --benchmark_filter=BM_Triad_AVX2_NT \
        --benchmark_repetitions=3"
    eval "$PERF_CMD" 2>/dev/null || {
        echo "    WARNING: perf stat failed (possibly missing events on this CPU)."
        echo "    Skipping perf counter collection."
    }
fi

# ---- Python post-processing ----

echo ""
echo "=== Post-processing ==="

if command -v python3 &>/dev/null; then
    python3 scripts/parse_perf.py "$OUTDIR" > "$OUTDIR/summary.json" \
        && echo "    -> $OUTDIR/summary.json"

    python3 scripts/roofline.py "$OUTDIR/summary.json" --output "$PLOTDIR" \
        && echo "    -> $PLOTDIR/roofline.pdf"

    python3 scripts/plot_bandwidth_scaling.py "$OUTDIR" --output "$PLOTDIR" \
        && echo "    -> $PLOTDIR/bandwidth_scaling.pdf"

    python3 scripts/tile_predict.py "$OUTDIR/summary.json" \
        && echo "    Tile prediction complete."
else
    echo "    WARNING: python3 not found. Skipping post-processing."
fi

# ---- Summary ----

echo ""
echo "Done. Results in: $OUTDIR"
echo "Plots in:         $PLOTDIR"
