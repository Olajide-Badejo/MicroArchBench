# Methodology

This document describes the measurement methodology, JSON schema, and
interpretation guidelines used by the CPU Microbenchmark Suite.

---

## JSON Schema: summary.json

`parse_perf.py` produces a `summary.json` with the following top-level fields:

```json
{
  "cpu_model":          "Intel(R) Core(TM) i7-12700K CPU @ 3.60GHz",
  "cpu_freq_ghz":       3.6,
  "latency_ns": {
    "L1":   1.4,
    "L2":   4.0,
    "L3":  14.0,
    "DRAM": 70.0
  },
  "peak_bw_dram_gbs":   48.0,
  "peak_bw_l3_gbs":    200.0,
  "peak_bw_l2_gbs":    350.0,
  "peak_bw_l1_gbs":    800.0,
  "peak_gflops_sp":    130.0,
  "gemm_tile_results": [...],
  "kernels": [
    {
      "name": "STREAM Triad (DRAM)",
      "arithmetic_intensity": 0.167,
      "gflops": 8.0,
      "gflops_ci_95": 0.3
    }
  ]
}
```

### Adding a kernel to the schema

To add a new kernel to the roofline plot:

1. Implement the kernel in `src/` as a Google Benchmark function.
2. Report `state.counters["arith_intensity_flop_byte"]` from within the benchmark.
3. Add perf counter collection for it in `scripts/run_suite.sh`.
4. Append a record to the `kernels` array in `summary.json` by extending
   `scripts/parse_perf.py`'s `build_kernel_list()` function.

---

## Confidence Intervals

Benchmark repetitions are run 3-5 times. Google Benchmark reports mean,
median, and stddev. The `gflops_ci_95` field is computed as:

```
CI_95 = t(0.975, n-1) * stddev / sqrt(n)
```

where `t` is the Student-t critical value. When multiplexing occurs in perf
counters the field `"multiplexed": true` is set and the CI is widened by the
scaling factor uncertainty.

---

## Pitfalls and Checklist

Before running the full suite, verify:

- [ ] CPU frequency is locked (`cpupower frequency-set -g performance`)
- [ ] Turbo Boost is disabled (`/sys/devices/system/cpu/intel_pstate/no_turbo = 1`)
- [ ] `kernel.perf_event_paranoid <= 1` (`sysctl -w kernel.perf_event_paranoid=1`)
- [ ] Running on a single NUMA node (`numactl --cpunodebind=0 --membind=0 ...`)
- [ ] No other compute-intensive processes are running
- [ ] Hyperthreading siblings are excluded from physical-core bandwidth tests

---

## Extending to a New Architecture

1. Add PMU event codes to `include/perf_event.hpp`'s dispatch table.
2. Create `docs/hardware_notes/<arch>.md` with event code documentation.
3. Add SIMD intrinsic variant in `src/peak_flops.cpp` under `#ifdef __<ARCH>__`.
4. Add toolchain file `cmake/toolchain-<arch>.cmake`.
5. Update `DetectArch.cmake` to probe for the new ISA feature.
