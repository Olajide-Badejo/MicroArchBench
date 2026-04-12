# FIXES.md

Complete audit and fix log for the CPU Microbenchmark Suite.
Every file in the repository was read in full and each issue below
was identified by code review, not by running the code.

---

## `include/perf_event.hpp`

### `read_raw()` read wrong number of bytes (correctness bug)

`read_raw()` called `::read(fd_, &val, sizeof(uint64_t))` — 8 bytes — on a
file descriptor whose `perf_event_attr.read_format` was constructed with
`PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING`. In that
mode the kernel writes a 24-byte struct (value + time_enabled + time_running).
Requesting only 8 bytes returns -1 with `ENOSPC`, so the function silently
returned 0 for every call.

**Fix:** Read the full 24-byte struct and return only the `value` field,
matching what the kernel actually writes.

### Signed/unsigned comparison in `read_scaled()` (warning, potential UB on exotic platforms)

`::read()` returns `ssize_t` (signed). The return value was compared directly
against `sizeof(data)` which is `size_t` (unsigned). On platforms where
`ssize_t` is narrower than `size_t` this comparison is undefined behaviour.

**Fix:** Cast `sizeof(data)` to `static_cast<ssize_t>(sizeof(data))` before
the comparison in both `read_raw()` and `read_scaled()`.

---

## `include/numa.hpp`

### Allocator mismatch causing undefined behaviour

When `BENCH_HAS_NUMA` was defined and `numa_alloc_local()` returned null,
the code fell through to `posix_memalign` and returned that pointer.
`numa_free_safe()` then called `numa_free()` on a `posix_memalign` block,
which is undefined behaviour (the two allocators use different internal
bookkeeping).

**Fix:** Return `nullptr` immediately when `numa_alloc_local()` fails.
The two code paths are now strictly separated and the allocator used is
unambiguous at the `free` site.

### Missing system headers

`pin_thread_to_core()` used `cpu_set_t`, `CPU_ZERO`, `CPU_SET`, and
`sched_setaffinity` without including `<sched.h>`. The function compiled
only because these symbols happened to be pulled in transitively through
other headers on most distributions, which is not guaranteed.

`numa_alloc_local_safe()` called `posix_memalign` without `<cstdlib>` being
unconditionally included (it was inside the `#ifdef BENCH_HAS_NUMA` guard).

**Fix:** Added unconditional `#include <sched.h>` and `#include <unistd.h>`
under `#ifdef __linux__` at the top of the file, outside all feature guards.

---

## `include/topology.hpp`

### Missing `<unistd.h>` for `sysconf`

`physical_core_count()` called `sysconf(_SC_NPROCESSORS_ONLN)` in the
fallback path without including `<unistd.h>`. This is a standards violation;
the function is declared in `<unistd.h>`.

**Fix:** Added `#include <unistd.h>` under `#ifdef __linux__`.

---

## `include/bench_utils.hpp`

### `fscanf` format-specifier type mismatch

`read_cpu_freq_hz()` declared `khz` as `uint64_t` and passed it to
`fscanf` with `"%llu"` via `(unsigned long long*)&khz`. On platforms where
`uint64_t` is `unsigned long` (not `unsigned long long`) this is a
type-punning violation and breaks strict aliasing.

**Fix:** Changed `khz` to `unsigned long long` and removed the cast, making
the type match the format specifier exactly.

---

## `src/stream.cpp`

### Memory leak in `StreamArrays` constructor

The original constructor called three `posix_memalign` allocations in a
single `if` chain. If the second or third call failed, any already-allocated
arrays were not freed before `std::bad_alloc` was thrown, leaking memory.

**Fix:** Rewritten with sequential allocation and explicit `free` of all
previously allocated arrays before rethrowing on each failure path.

### Memory leak and missing cleanup in `BM_Triad_MT_Scaling`

The multi-threaded benchmark called three `posix_memalign` allocations in
a single `if` condition. On partial failure it called `state.SkipWithError`
and returned without freeing the successfully allocated arrays.

**Fix:** Sequential allocation with a dedicated early-return cleanup path for
each allocation step. All three arrays are freed on every exit path including
`SkipWithError` returns.

### OpenMP global thread count not restored

`BM_Triad_MT_Scaling` called `omp_set_num_threads(n_threads)` at entry but
never restored the previous value. Subsequent benchmarks registered after this
one in the same process inherited the modified thread count.

**Fix:** Save `omp_get_max_threads()` at entry and restore it before every
return path, including the `SkipWithError` paths.

### GCC-only `__attribute__((optimize("no-tree-vectorize")))` used on Clang

The scalar baseline kernel functions were decorated with a GCC extension.
Clang ignores this attribute silently, meaning the Clang build's scalar
variants would be auto-vectorised and would not serve as honest baselines.

**Fix:** Introduced a `BENCH_NO_VECTORIZE` macro that expands to the GCC
attribute on GCC and is empty on Clang. The CMake target for `stream_bench`
now also receives `-fno-tree-vectorize` at the translation-unit level, which
Clang does honour and which suppresses auto-vectorisation for the entire file.
The vectorised variants opt back in explicitly via `#pragma GCC ivdep`.

### Missing `#include <string>`

`std::to_string` was called inside `BM_Triad_MT_Scaling` without `<string>`
being included. The code compiled on GCC because `<string>` was pulled in
transitively through `<benchmark/benchmark.h>`, but this is not guaranteed.

**Fix:** Added `#include <string>` explicitly.

---

## `src/pointer_chase.cpp`

### Memory leak in `build_chase_list` on exception

If the `std::vector<std::size_t> perm(n_nodes)` allocation inside
`build_chase_list` threw `std::bad_alloc`, the already-allocated `nodes`
array was leaked because there was no cleanup path.

**Fix:** Wrapped the permutation construction and linking in a `try/catch`
block. The catch handler calls `numa_free_safe(nodes, ...)` before rethrowing,
ensuring no leak regardless of which allocation fails.

---

## `src/peak_flops.cpp`

### FP overflow to infinity across benchmark iterations

The FMA accumulator arrays were declared outside the `for (auto _ : state)`
loop, so their values persisted across every timed iteration. With 100 000
repetitions per iteration and Google Benchmark running thousands of iterations
during warm-up and measurement, accumulator values reached IEEE 754 infinity
after approximately `log(FLT_MAX) / log(1.0000001f)` iterations. Once
infinite, the FMA chain collapses to a no-op (inf * b = inf regardless of b)
and the measured throughput becomes meaningless.

**Fix:** Moved accumulator initialisation inside the `for (auto _ : state)`
loop so values are reset to small finite constants at the start of each timed
iteration. Applied to all three benchmark functions: `BM_PeakFLOPS_AVX2_SP`,
`BM_PeakFLOPS_AVX2_DP`, and `BM_PeakFLOPS_NEON_SP`.

### Unused `#include <cstdlib>` and `#include <cstring>`

These headers were included but nothing in the file used them.

**Fix:** Removed both includes.

---

## `src/gemm_tile_predict.cpp`

No bugs found. File is correct as written.

---

## `CMakeLists.txt`

### `option()` used with non-boolean `"AUTO"` default

CMake's `option()` command only accepts `ON` or `OFF` as the default value.
Passing `"AUTO"` as the default produces a cache entry of type `BOOL` but
with the string value `"AUTO"`, which CMake then coerces unpredictably
depending on version and generator. The `if(BENCH_ENABLE_AVX2 OR
BENCH_ENABLE_AVX2 STREQUAL "AUTO")` guard in `DetectArch.cmake` relied on
this undocumented string-coercion behaviour.

**Fix:** Replaced `option()` with `set(... CACHE STRING ...)` for all
tri-state variables (`BENCH_ENABLE_AVX2`, `BENCH_ENABLE_FMA`,
`BENCH_ENABLE_NEON`, `BENCH_WITH_NUMA`). These now have documented
`AUTO / ON / OFF` semantics that `DetectArch.cmake` and `PerfCounters.cmake`
already handle correctly with `STREQUAL "AUTO"` guards.

### Missing `-fno-tree-vectorize` for Clang builds of `stream_bench`

The fix to `stream.cpp`'s scalar baseline functions (the `BENCH_NO_VECTORIZE`
macro) requires the compiler flag `-fno-tree-vectorize` to be present at the
CMake target level for Clang, since Clang ignores the GCC attribute silently.

**Fix:** Added `-fno-tree-vectorize` alongside `-funroll-loops` in the
`target_compile_options` call for `stream_bench`.

---

## `scripts/parse_perf.py`

### `build_kernel_list` defined but never called

The function that populates the `kernels` array in `summary.json` (used by
`roofline.py` to place data points on the roofline plot) was defined but
not called in `main()`. As a result, the `kernels` key in every
`summary.json` was always an empty list and no kernel points ever appeared
on the roofline plot.

**Fix:** Added `kernels = build_kernel_list(bandwidths)` and
`"kernels": kernels` in `main()`.

### `extract_gemm` raised `ValueError` on malformed benchmark labels

`part.split("=")` was used to parse tile-size label parts. If any part of
the label string did not contain `=` (e.g. extra whitespace or a benchmark
name suffix), `k, v = part.split("=")` raised `ValueError` with
"too many values to unpack".

**Fix:** Replaced `split("=")` with `part.partition("=")` and added an
`if "=" not in part: continue` guard before the parse.

### Unused imports (`math`, `os`, `re`, `Any`)

Four imports were present that were never referenced anywhere in the file.
These caused `pylint` failures in the CI Python job.

**Fix:** Removed all four unused imports. The file now imports only `json`,
`sys`, `Path`, and `Optional`.

### Duplicate function definitions

A partial `str_replace` operation during a previous fix pass inserted new
function definitions without removing the old ones, leaving two copies of
`extract_gemm`, `build_kernel_list`, and `main` in the file. Python uses
the last definition, so the first copies were silently dead.

**Fix:** Rewrote the file completely from scratch as a single clean version.

---

## `scripts/roofline.py`

### Shading regions rendered with zero width when `peak_bw_dram` was zero

`fill_betweenx` for the memory-bound shading region used
`rp if math.isfinite(rp) else 1e4` as the right boundary and
`rp if math.isfinite(rp) else 1e-2` as the left boundary for the
compute-bound shading. When both were evaluated with `rp = inf`, the
memory region had boundary `1e4` and the compute region had boundary
`1e-2`, but the ridge-point vertical line was not drawn. After the
placeholder substitution (`peak_bw_dram = 50.0`), `rp` became finite
but the boundary expressions had already been evaluated, leading to
inconsistent shading widths.

**Fix:** Introduced `rp_clamped = min(rp, 1e4)` computed after all
placeholder substitutions. Both `fill_betweenx` calls and the ridge-point
annotation use this single consistent value.

### Unused `Optional` import

`Optional` was imported from `typing` but not referenced anywhere in the file.

**Fix:** Removed the import.

---

## `scripts/tile_predict.py`

### Unused `math` import

`math` was imported but the only use of an infinity value in the file was
`return float("inf")`, which does not require the `math` module.

**Fix:** Removed `import math`.

---

## `tests/test_stream_verify.cpp`

### `posix_memalign` return values not checked in `Bufs` constructor

All four `posix_memalign` calls were made in a single chained `||`
expression. If any call failed, the remaining allocations were skipped
(short-circuit evaluation) but the test would proceed with null pointers,
producing a segfault rather than a clean failure message.

**Fix:** Each allocation is now checked individually. On any failure, all
previously allocated buffers are freed and the pointers are set to `nullptr`.
All test functions call `if (!b.ok()) return;` before accessing buffer data.

### Only 2 of 4 kernels tested; `ref_scale` and `ref_add` were dead code

`ref_scale` and `ref_add` were defined but no test called them. Only
`vec_copy` and `vec_triad` had test functions. The AVX2-NT section only
tested `avx2_nt_triad`.

**Fix:** Added test functions for all four kernels in both the vectorised
and AVX2-NT variants: `test_vec_scale`, `test_vec_add`, `test_avx2_nt_copy`,
`test_avx2_nt_scale`, `test_avx2_nt_add`.

### Zero-length test only covered 2 of 4 kernels

Only `vec_copy` and `vec_triad` were tested at `n=0`.

**Fix:** All four vectorised kernels are now tested at `n=0`.

### No large-buffer test

The benchmark suite's critical measurement range is L3/DRAM working sets.
No test verified kernel correctness at those sizes.

**Fix:** Added `test_large_buffer()` which runs `vec_triad` on a 16 MiB
per-array working set (64 MiB total) and compares against the scalar reference.

---

## `tests/test_permutation.cpp`

### Memory leak in `build_chase_list` on OOM

If the `std::vector<std::size_t> perm(n_nodes)` constructor threw
`std::bad_alloc`, the already-allocated `nodes` array was leaked.

**Fix:** Wrapped the permutation construction in `try/catch`. The handler
calls `free(nodes)` before returning `nullptr`.

### Memory leak in `test_seed_independence` on partial allocation failure

If the second `build_chase_list` call returned `nullptr`, the function
called `exit(1)` without freeing the first successfully allocated list.

**Fix:** Check each allocation separately and free any prior successful
allocation before calling `exit(1)`.

---

## `.github/workflows/ci.yml`

### Missing `libomp-dev` for Clang OpenMP support

The Clang CI matrix job installed the compiler package but not `libomp-dev`.
`stream_bench` requires OpenMP for the multi-threaded scaling benchmark.
Without the runtime library, the Clang build would either fail to link or
silently build without OpenMP, meaning the multi-threaded benchmark would
not be compiled and the CI job would not verify it.

**Fix:** Added `libomp-dev` to the system dependencies install step.

### `BENCH_WITH_NUMA=ON` hard-coded in CI

The CI configure step passed `-DBENCH_WITH_NUMA=ON`. If the apt install of
`libnuma-dev` was silently skipped (e.g. package not available in the runner
image version), CMake would fail at link time rather than gracefully degrading.

**Fix:** Changed to `-DBENCH_WITH_NUMA=AUTO` so CMake probes for the library
and disables NUMA support cleanly if it is absent.

### No dependency caching for FetchContent (Google Benchmark)

Every CI run re-cloned and re-built Google Benchmark from source, adding
several minutes to each job.

**Fix:** Added `actions/cache@v4` steps for the FetchContent build directory
in all three C++ jobs (`build-test-x86`, `sanitizers`, `build-aarch64`),
keyed on the compiler name and the `CMakeLists.txt` hash.
