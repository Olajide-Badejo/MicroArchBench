#!/usr/bin/env bash
# scripts/check_system.sh
# Pre-flight checker: verifies that every prerequisite for the benchmark
# suite is present and correctly configured before you invest time in a
# full build and run.
#
# Exit codes:
#   0 -- all hard requirements met (warnings may still be printed)
#   1 -- one or more hard requirements are missing
#
# Usage:
#   bash scripts/check_system.sh

set -euo pipefail

PASS=0
WARN=1
FAIL=2

n_errors=0
n_warnings=0

# ---- Output helpers ----

ok()   { printf "  [OK]  %s\n" "$*"; }
warn() { printf "  [--]  %s\n" "$*"; (( n_warnings++ )) || true; }
fail() { printf "  [XX]  %s\n" "$*"; (( n_errors++   )) || true; }

section() { echo ""; echo "==> $*"; }

# ---- Check: command exists ----

need_cmd() {
    local cmd="$1"
    local hint="${2:-}"
    if command -v "$cmd" &>/dev/null; then
        ok "$cmd found: $(command -v "$cmd")"
        return 0
    else
        fail "$cmd NOT found${hint:+ -- $hint}"
        return 1
    fi
}

want_cmd() {
    local cmd="$1"
    local hint="${2:-}"
    if command -v "$cmd" &>/dev/null; then
        ok "$cmd found (optional): $(command -v "$cmd")"
    else
        warn "$cmd not found (optional)${hint:+ -- $hint}"
    fi
}

# ---- Check: minimum version ----

version_ge() {
    # Returns 0 if actual >= required (both as "major.minor" strings)
    local actual="$1" required="$2"
    printf '%s\n%s\n' "$required" "$actual" | sort -V -C
}

# ---------------------------------------------------------------------------

echo ""
echo "CPU Microbenchmark Suite -- system pre-flight check"
echo "======================================================"

# ---- Operating system ----

section "Operating system"
if [[ "$(uname -s)" == "Linux" ]]; then
    KERNEL=$(uname -r)
    ok "Linux kernel: $KERNEL"
    # Extract major.minor
    KV=$(echo "$KERNEL" | grep -oP '^\d+\.\d+')
    if version_ge "$KV" "5.15"; then
        ok "Kernel version >= 5.15 (perf_event_open PMU access supported)"
    else
        warn "Kernel $KV < 5.15. Some perf counters may be unavailable."
    fi
else
    fail "Not Linux ($(uname -s)). This suite is Linux-only."
fi

# ---- Compiler ----

section "C++ compiler"
if command -v g++ &>/dev/null; then
    GCC_VER=$(g++ -dumpversion 2>/dev/null | cut -d. -f1)
    if [[ "$GCC_VER" -ge 12 ]]; then
        ok "g++ version $GCC_VER (>= 12)"
    else
        fail "g++ version $GCC_VER < 12. Upgrade: sudo apt install g++-12"
    fi
elif command -v clang++ &>/dev/null; then
    CLANG_VER=$(clang++ --version | grep -oP '\d+\.\d+\.\d+' | head -1 | cut -d. -f1)
    if [[ "$CLANG_VER" -ge 15 ]]; then
        ok "clang++ version $CLANG_VER (>= 15)"
    else
        fail "clang++ version $CLANG_VER < 15."
    fi
else
    fail "No C++ compiler found. Install: sudo apt install g++"
fi

# ---- CMake ----

section "Build system"
if command -v cmake &>/dev/null; then
    CMAKE_VER=$(cmake --version | grep -oP '\d+\.\d+\.\d+' | head -1)
    if version_ge "$CMAKE_VER" "3.25"; then
        ok "cmake $CMAKE_VER (>= 3.25)"
    else
        fail "cmake $CMAKE_VER < 3.25. Upgrade from cmake.org or via pip install cmake"
    fi
else
    fail "cmake not found. Install: sudo apt install cmake"
fi

want_cmd ninja  "Install: sudo apt install ninja-build"
want_cmd make   ""

# ---- Python ----

section "Python"
if command -v python3 &>/dev/null; then
    PY_VER=$(python3 -c 'import sys; print("{}.{}".format(*sys.version_info[:2]))')
    if version_ge "$PY_VER" "3.10"; then
        ok "python3 $PY_VER (>= 3.10)"
    else
        fail "python3 $PY_VER < 3.10"
    fi
else
    fail "python3 not found. Install: sudo apt install python3"
fi

for pkg in numpy matplotlib scipy; do
    if python3 -c "import $pkg" &>/dev/null; then
        VER=$(python3 -c "import $pkg; print($pkg.__version__)" 2>/dev/null || echo "?")
        ok "Python package '$pkg' $VER"
    else
        warn "Python package '$pkg' not installed. Run: pip3 install -r requirements.txt"
    fi
done

# ---- ISA features ----

section "CPU ISA features"
if grep -q avx2 /proc/cpuinfo 2>/dev/null; then
    ok "AVX2 supported"
else
    warn "AVX2 not detected in /proc/cpuinfo. AVX2 benchmarks will be skipped."
fi

if grep -q fma /proc/cpuinfo 2>/dev/null; then
    ok "FMA3 supported"
else
    warn "FMA3 not detected. Peak FLOPS benchmark will use scalar fallback."
fi

if grep -q avx512f /proc/cpuinfo 2>/dev/null; then
    ok "AVX-512 supported (optional -- disabled by default due to frequency throttling)"
fi

# ---- perf_event_open ----

section "Linux perf"
want_cmd perf "Install: sudo apt install linux-tools-generic"

PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
if [[ "$PARANOID" == "unknown" ]]; then
    warn "Cannot read kernel.perf_event_paranoid"
elif [[ "$PARANOID" -le 1 ]]; then
    ok "kernel.perf_event_paranoid = $PARANOID (perf_event_open available)"
else
    warn "kernel.perf_event_paranoid = $PARANOID (> 1). Hardware counters will fail."
    warn "Fix: sudo sysctl -w kernel.perf_event_paranoid=1"
fi

# ---- Optional tools ----

section "Optional tools"
want_cmd cpupower  "Install: sudo apt install linux-tools-generic (for frequency locking)"
want_cmd numactl   "Install: sudo apt install numactl (for NUMA binding)"
want_cmd likwid-perfctr "Install from likwid.org (for cross-validated counters)"

# Check libnuma dev headers
if [[ -f /usr/include/numa.h ]]; then
    ok "libnuma headers found (/usr/include/numa.h)"
else
    warn "libnuma-dev not found. NUMA-local allocation disabled."
    warn "Install: sudo apt install libnuma-dev"
fi

# ---- CPU frequency ----

section "CPU frequency"
GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
if [[ "$GOV" == "performance" ]]; then
    ok "CPU governor: performance"
else
    warn "CPU governor: '$GOV' (not 'performance')"
    warn "Fix: sudo cpupower frequency-set -g performance"
fi

if [[ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
    TURBO=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
    if [[ "$TURBO" == "1" ]]; then
        ok "Intel Turbo Boost: disabled (correct for benchmarking)"
    else
        warn "Intel Turbo Boost: enabled. Latency measurements may drift."
        warn "Fix: echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
    fi
fi

# ---- Summary ----

echo ""
echo "======================================================"
if [[ $n_errors -eq 0 ]]; then
    echo "Result: READY ($n_warnings warning(s))"
    echo ""
    echo "Next steps:"
    echo "  1. make                         # configure and build"
    echo "  2. make test                    # run correctness tests"
    echo "  3. make run-all                 # run full benchmark suite"
    echo "  4. open results/plots/          # view roofline and latency plots"
    exit 0
else
    echo "Result: $n_errors error(s), $n_warnings warning(s) -- fix errors before building"
    exit 1
fi
