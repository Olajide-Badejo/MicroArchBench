# CPU Microbenchmark Suite

Characterises memory hierarchy latency, bandwidth, and peak SIMD throughput on x86-64 (AVX2/FMA3) and AArch64 (NEON) platforms, then uses those measurements to build a roofline model and predict optimal GEMM tile sizes analytically.

Built on top of [Google Benchmark](https://github.com/google/benchmark). No external benchmark library is needed beyond that -- CMake fetches it automatically.

---

## What it measures

**Latency** -- pointer-chase microbenchmarks that defeat hardware prefetchers sweep from 4 KiB to 512 MiB to expose L1, L2, L3, and DRAM latency plateaus.

**Bandwidth** -- STREAM-style Copy / Scale / Add / Triad kernels in three variants: scalar, compiler-vectorised, and hand-written AVX2 with non-temporal stores (for DRAM-level measurement).

**Peak FLOPS** -- unrolled FMA chains with 10 independent accumulator registers to saturate FMA execution ports on AVX2 and NEON cores.

**Roofline model** -- Python post-processing takes the measured peak GFLOPS and per-level bandwidth numbers, plots the roofline, and places kernels on the graph.

**GEMM tile-size prediction** -- applies the BLIS analytical cache-blocking model to the measured hardware parameters to recommend (Mc, Kc, Nc) panel sizes for a blocked FP32 GEMM, then cross-validates against a naive blocked implementation.

---

## Repository layout

```
cpu-microbench/
- CMakeLists.txt
- Makefile                    # convenience wrapper around cmake
- LICENSE
- CONTRIBUTING.md
- requirements.txt            # numpy, matplotlib, scipy
- .gitignore
- cmake/
  - DetectArch.cmake          # ISA feature detection (AVX2, FMA, NEON)
  - PerfCounters.cmake        # perf_event_open availability check
  - Sanitizers.cmake          # ASan + UBSan (for tests only)
  - toolchain-aarch64.cmake   # cross-compilation for AArch64
- include/
  - bench_utils.hpp           # timing helpers and cache-level classifier
  - perf_event.hpp            # RAII wrapper around perf_event_open(2)
  - topology.hpp              # /proc/cpuinfo and sysfs reader
  - numa.hpp                  # libnuma wrappers with graceful fallback
- src/
  - pointer_chase.cpp         # L1/L2/L3/DRAM latency benchmarks
  - stream.cpp                # Copy/Scale/Add/Triad bandwidth benchmarks
  - peak_flops.cpp            # AVX2 and NEON peak GFLOPS benchmarks
  - gemm_tile_predict.cpp     # blocked GEMM tile-size validation
- scripts/
  - check_system.sh           # pre-flight system checker
  - run_suite.sh              # orchestration + perf stat wrapper
  - parse_perf.py             # JSON aggregator -> summary.json
  - roofline.py               # roofline + latency + bandwidth plots
  - plot_bandwidth_scaling.py # thread-count vs bandwidth scaling plot
  - tile_predict.py           # BLIS tile-size calculator
- tests/
  - test_permutation.cpp
  - test_stream_verify.cpp
- docs/
  - methodology.md
  - reproducibility.md
  - hardware_notes/
- .github/workflows/ci.yml
- .vscode/                    # local editor tasks/launch configs
- results/                    # auto-generated; gitignored
  - raw/
  - plots/
```

---

## Project docs

- `docs/methodology.md`: schema, analysis assumptions, and extension method.
- `docs/reproducibility.md`: publication-grade experimental protocol.
- `CONTRIBUTING.md`: contribution and code-quality standards.
- `CODE_OF_CONDUCT.md`: community conduct expectations.
- `SECURITY.md`: responsible vulnerability disclosure workflow.
- `CITATION.cff`: citation metadata for papers and reports.
- `CHANGELOG.md`: release and repository hardening history.

---

## Quick start

```bash
# 1. Verify your system has everything needed
bash scripts/check_system.sh

# 2. Install Python dependencies
pip3 install -r requirements.txt

# 3. Build (make handles cmake configure + build)
make

# 4. Run correctness tests
make test

# 5. Run all benchmarks and generate plots
make run-all

# 6. View results
ls results/raw/        # timestamped JSON files
ls results/plots/      # roofline.pdf, latency_vs_size.png, bandwidth_by_level.png
```

`make help` lists all available targets.

---

## Prerequisites

### System

| Requirement | Minimum | Notes |
|-------------|---------|-------|
| Linux kernel | 5.15 | `perf_event_open` with full PMU access |
| GCC | 12 | or Clang 15+ |
| CMake | 3.25 | |
| Python | 3.10 | for post-processing scripts |
| Ninja | any | recommended; Make also works |

### Optional

| Package | Purpose |
|---------|---------|
| `libnuma-dev` | NUMA-local allocation (auto-detected by CMake) |
| `linux-tools-generic` | `perf` command |
| `cpupower` | locking CPU frequency |
| `numactl` | NUMA binding |
| `likwid` | cross-validation of hardware counter readings |

Install on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build python3 python3-pip \
                 libnuma-dev linux-tools-generic cpuidtool
pip3 install -r requirements.txt
```

---

## Building

```bash
# Configure (CMake downloads Google Benchmark automatically)
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DBENCH_ENABLE_AVX2=ON \
  -DBENCH_ENABLE_FMA=ON \
  -DBENCH_ENABLE_PERF=ON \
  -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON \
  -G Ninja

# Build all benchmarks and tests
cmake --build build -j$(nproc)
```

CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `BENCH_ENABLE_AVX2` | `AUTO` | Enable `-mavx2` |
| `BENCH_ENABLE_FMA` | `AUTO` | Enable `-mfma` |
| `BENCH_ENABLE_AVX512` | `OFF` | Enable `-mavx512f` (Skylake-X+) |
| `BENCH_ENABLE_PERF` | `ON` | Compile perf_event_open wrappers |
| `BENCH_WITH_NUMA` | `AUTO` | Link libnuma |
| `BENCH_SANITIZE` | `OFF` | ASan + UBSan (tests only, never benchmarks) |
| `BENCH_LTO` | `OFF` | Link-time optimisation (intentionally discouraged) |

> **Never enable LTO for benchmarks.** The linker may constant-fold or eliminate the loops being measured.

---

## Running

### Full suite (recommended)

```bash
# Lock CPU frequency and disable turbo (requires sudo)
sudo cpupower frequency-set -g performance
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo  # Intel
sudo sysctl -w kernel.perf_event_paranoid=1

# Run on a single NUMA node
numactl --cpunodebind=0 --membind=0 bash scripts/run_suite.sh

# View results
ls results/raw/        # timestamped directory with JSON files
ls results/plots/      # roofline.pdf, latency_vs_size.png, bandwidth_by_level.png
```

Expected runtime on a modern desktop CPU: around 8 minutes for the full sweep.

### Individual benchmarks

```bash
# Latency sweep
./build/pointer_chase --benchmark_format=console --benchmark_repetitions=3

# Bandwidth sweep
./build/stream_bench  --benchmark_format=console

# Peak FLOPS
./build/peak_flops    --benchmark_format=console --benchmark_repetitions=5

# GEMM tile validation
./build/gemm_tile_bench --benchmark_format=console
```

### Correctness tests

```bash
ctest --test-dir build -V
```

---

## VS Code setup

Open the folder directly in VS Code. The `.vscode/` directory includes:

- `settings.json` -- CMake Tools integration, C++ IntelliSense, Python formatter
- `tasks.json` -- build tasks (`Ctrl+Shift+B` triggers `CMake: build`)
- `launch.json` -- run/debug configurations for each benchmark binary
- `extensions.json` -- recommended extension list

**Install recommended extensions when prompted** (or via `Extensions: Show Recommended Extensions`). The most important ones are:

| Extension | ID |
|-----------|----|
| C/C++ | `ms-vscode.cpptools` |
| CMake Tools | `ms-vscode.cmake-tools` |
| CMake | `twxs.cmake` |
| Python | `ms-python.python` |

After installing CMake Tools, press `Ctrl+Shift+P` and run `CMake: Configure`, then `CMake: Build`. The integrated terminal then lets you run any benchmark directly.

---

## Interpreting results

### Latency plateaus

The pointer-chase sweep exposes distinct latency levels corresponding to each cache. Sharp transitions between plateaus indicate the cache capacity boundaries. If all levels collapse to a single latency, turbo boost is active and inflating the CPU frequency -- disable it and re-run.

### Bandwidth numbers

The AVX2 non-temporal Triad result (`BM_Triad_AVX2_NT`) gives the most accurate picture of sustainable DRAM bandwidth because non-temporal stores bypass the cache on write, eliminating read-for-ownership (RFO) traffic.

### Roofline classification

A kernel sits on the **bandwidth roof** if it is memory-bound (left of the ridge point). It sits on the **compute roof** if it is compute-bound (right of the ridge point). A kernel well below both roofs has a bottleneck not captured by the two-roof model -- typically instruction latency, branch misprediction, or lock contention.

### Expected values (i7-12700K, DDR5-4800, 1 core)

| Metric | Expected | Acceptable range |
|--------|----------|-----------------|
| L1 load latency | 4-5 cycles (~1.4 ns) | 3-6 cycles |
| L2 load latency | 12-14 cycles (~4 ns) | 10-16 cycles |
| L3 load latency | 40-50 cycles (~14 ns) | 35-60 cycles |
| DRAM latency | 60-80 ns | 50-100 ns |
| DRAM bandwidth (1 thread) | 40-52 GB/s | 35-60 GB/s |
| Peak SP GFLOPS (1 core) | 120-150 | 100-175 |

---

## Measurement pitfalls

**CPU frequency scaling** -- always lock frequency before running. Turbo Boost causes non-stationary distributions and inflated FLOPS counts.

**NUMA remote access** -- on multi-socket systems, use `numactl` to bind to a single node. Remote memory can appear as 2x DRAM latency.

**Hyperthreading** -- bandwidth benchmarks do not model contention between HT siblings sharing L1/L2. Pin to physical cores only.

**DRAM refresh** -- refresh cycles inject ~7.8 us latency spikes (LPDDR4). These show up in p99 latency but average out in the mean. Report both.

**Prefetcher interaction** -- the pointer-chase benchmark defeats the hardware stride prefetcher by design (random permutation). Bandwidth benchmarks intentionally keep the prefetcher enabled, since that is what production kernels experience.

---

## Extending the suite

### Adding a new kernel

1. Implement in `src/` as a Google Benchmark function.
2. Report `state.counters["arith_intensity_flop_byte"]` from the benchmark.
3. Add perf counter collection in `scripts/run_suite.sh`.
4. Add a record to `kernels` in `scripts/parse_perf.py`'s `build_kernel_list()`.

### Adding a new architecture

1. Document PMU event codes in `docs/hardware_notes/<arch>.md`.
2. Add event codes to `include/perf_event.hpp`'s dispatch table.
3. Add SIMD intrinsic variant in `src/peak_flops.cpp` under `#ifdef __<ARCH>__`.
4. Add toolchain file `cmake/toolchain-<arch>.cmake`.
5. Update `cmake/DetectArch.cmake` to probe the new ISA feature.

---

## References

1. McCalpin, J.D. (1995). *Memory Bandwidth and Machine Balance in Current High Performance Computers*. IEEE Technical Committee on Computer Architecture.
2. Williams, S., Waterman, A., Patterson, D. (2009). *Roofline: An Insightful Visual Performance Model for Multicore Architectures*. CACM 52(4).
3. Van Zee, F.G., van de Geijn, R.A. (2015). *BLIS: A Framework for Rapidly Instantiating BLAS Functionality*. ACM TOMS 41(3).
4. Fog, A. (2024). *Instruction Tables*. Technical University of Denmark. https://agner.org/optimize/
5. Drepper, U. (2007). *What Every Programmer Should Know About Memory*. Red Hat.
6. Intel Corp. (2024). *Intel 64 and IA-32 Architectures Optimization Reference Manual*, Vol. 1.
7. ARM Ltd. (2021). *Cortex-A72 Software Optimization Guide*.

---

## License

MIT

## Citation

If you use this repository in academic or industrial research, cite using `CITATION.cff` metadata.

