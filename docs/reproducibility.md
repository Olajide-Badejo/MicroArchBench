# Reproducibility Protocol

This document defines a minimum protocol for producing defensible benchmark
results suitable for publication, technical reports, and peer review.

## 1. Experimental Controls

Use the following controls before every run:

- Fixed CPU frequency governor (`performance`)
- Turbo and vendor boost disabled
- Single NUMA node pinning
- Isolated benchmark cores (no competing workloads)
- Stable ambient temperature and adequate cooling
- Repeat runs with identical binary and system settings

Record all control settings in your report appendix.

## 2. Required Metadata

Each benchmark campaign should preserve:

- Git commit SHA
- Compiler and version (`gcc --version` or `clang --version`)
- CMake configuration flags
- Kernel version (`uname -a`)
- CPU model and microarchitecture stepping
- DRAM configuration (channels, frequency, timings if known)
- Affinity policy and thread count

The commit SHA and exact CMake cache values are mandatory.

## 3. Statistical Reporting

For each reported number:

- Use at least 3 repetitions (prefer 5 for publication)
- Report central tendency (median preferred) and dispersion
- Include confidence interval where available
- Report outlier policy and whether outliers were removed

Never report a single best-case run as the primary result.

## 4. Artifact Layout

Use a timestamped results directory under `results/raw/` with:

- `latency.json`
- `bandwidth.json`
- `flops.json`
- `gemm.json`
- `summary.json`
- optional `perf_counters.json`

Generated figures belong in `results/plots/`.

## 5. Recommended Run Flow

```bash
bash scripts/check_system.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j"$(nproc)"
ctest --test-dir build -V --output-on-failure
bash scripts/run_suite.sh
```

For frozen-frequency measurements, do not use `--no-freq-lock`.

## 6. Publication Checklist

- [ ] Methodology statement included (see `docs/methodology.md`)
- [ ] Hardware/software metadata complete
- [ ] Raw JSON artifacts archived
- [ ] Plot scripts and command lines archived
- [ ] Confidence intervals reported
- [ ] Limitations and threats to validity documented

## 7. Threats to Validity

Discuss these explicitly in papers and reports:

- Microcode or firmware updates between runs
- Thermal throttling and transient boost behavior
- Counter multiplexing and event aliasing
- Cross-architecture ISA semantic differences
- OS scheduler jitter and interrupt noise

When uncertain, publish both the raw artifacts and your parsing scripts.

