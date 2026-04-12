# Contributing

Contributions are welcome. This document explains how the project is structured
so changes fit cleanly with the existing design.

---

## Ground rules

- All benchmark executables must produce **reproducible, stationary results**.
  Before submitting a new benchmark, show that stddev / mean < 2% across five
  repetitions on a frequency-locked core.
- Never enable LTO (`BENCH_LTO`). It silently eliminates the loops being
  measured and produces meaningless numbers.
- New C++ code targets C++20. Headers go in `include/`, translation units in `src/`.
- Python scripts require Python 3.10+ and must have no dependencies beyond
  `numpy`, `matplotlib`, and `scipy`.
- Comments should be written in plain English. No jargon that is not defined in
  the same file or in `docs/methodology.md`.

---

## Adding a new benchmark

1. Create `src/<name>.cpp` with a Google Benchmark function and a `BENCHMARK()`
   registration macro.
2. Add `add_bench(<name> src/<name>.cpp)` to `CMakeLists.txt`.
3. Report arithmetic intensity via
   `state.counters["arith_intensity_flop_byte"] = ...` so the roofline script
   can place the kernel on the plot.
4. Add the kernel to `scripts/parse_perf.py`'s `build_kernel_list()`.
5. Add an integration step to `scripts/run_suite.sh` so `make run-all` picks it up.
6. Document expected values in `README.md` under "Interpreting results".

---

## Adding a new architecture

1. Add PMU event codes and latency reference values to
   `docs/hardware_notes/<arch>.md`.
2. Add a corresponding `#ifdef` block in `src/peak_flops.cpp` using the
   architecture's SIMD intrinsics (same 10-accumulator unroll strategy).
3. Extend `cmake/DetectArch.cmake` with a `check_cxx_source_runs` probe for
   the new ISA. Set `HAVE_<ARCH>` in cache.
4. Add event codes to `include/perf_event.hpp`'s dispatch table if the
   architecture uses non-standard PMU event numbers.
5. Create `cmake/toolchain-<arch>.cmake` following the pattern in
   `cmake/toolchain-aarch64.cmake`. Use `CMAKE_C_FLAGS_INIT` -- not
   `add_compile_options()` -- for architecture flags in toolchain files.

---

## Running tests before submitting

```bash
# Build with sanitizers (correctness only -- not for timing)
cmake -B build-san \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBENCH_SANITIZE=ON \
    -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON
cmake --build build-san -j$(nproc)
ctest --test-dir build-san -V --output-on-failure
```

All tests in `tests/` must pass with ASan + UBSan before a change is submitted.

---

## Code style

- C++: 4-space indent, no tabs, line limit 100 characters, `snake_case` for
  functions and variables, `PascalCase` for types and classes.
- Python: follow PEP 8. `black` with default settings is used as the formatter
  (see `.vscode/settings.json`).
- Shell: `bash` with `set -euo pipefail`. Quote all variable expansions.
  `shellcheck` must produce no warnings.

---

## Commit messages

Use the imperative mood and keep the subject line under 72 characters:

```
Add L4 working-set sweep to pointer_chase benchmark
Fix AVX-512 FMA accumulator count on Sapphire Rapids
Document AMD Zen 4 PMU event codes
```
