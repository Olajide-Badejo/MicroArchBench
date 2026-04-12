# ARM Cortex-A72 -- PMU Notes (Raspberry Pi 4)

## Core Identification

The Cortex-A72 is found in the Raspberry Pi 4 (BCM2711).
Architecture: ARMv8-A, in-order dual-issue pipeline.

## Key PMU Events (perf_event_attr config values)

| Event Name            | Config | Description |
|-----------------------|--------|-------------|
| `INST_RETIRED`        | 0x08   | Instructions retired |
| `CPU_CYCLES`          | 0x11   | Core cycles |
| `L1D_CACHE_REFILL`    | 0x03   | L1d cache refill (miss) |
| `L1D_CACHE`           | 0x04   | L1d cache access |
| `L2D_CACHE_REFILL`    | 0x17   | L2 cache refill |
| `L2D_CACHE`           | 0x16   | L2 cache access |
| `BUS_ACCESS`          | 0x19   | Bus (DRAM) access |
| `VFP_SPEC`            | 0x75   | FP/SIMD operations speculatively executed |
| `BR_MIS_PRED`         | 0x10   | Branch mispredictions |

## Enabling PMU Access on Raspberry Pi 4

By default, user-space PMU access is blocked. Enable via the kernel module:

```bash
# Load the module (included in Raspberry Pi OS kernel)
sudo modprobe enable_arm_pmu

# Or set paranoid level
sudo sysctl -w kernel.perf_event_paranoid=1
```

An alternative is the `armv8_pmu` kernel patch for full user-mode PMU access.

## Latency Reference Values (Cortex-A72 @ 1.5 GHz)

| Level | Cycles | ns (at 1.5 GHz) |
|-------|--------|-----------------|
| L1d   | 4      | 2.7 |
| L2    | 15     | 10.0 |
| DRAM  | varies | 80-130 |

The Cortex-A72 does not have a dedicated L3; DRAM latency is relatively high.

## Peak FLOPS (Cortex-A72 @ 1.5 GHz)

- NEON: 1 FP/SIMD pipeline (dual-issue but one FP pipe)
- Peak SP: 1 FMA/cycle x 2 FLOPs x 4 floats = 8 GFLOPS/cycle/core (at 1.5 GHz)
- Theoretical peak: 1.5 GHz x 8 GFLOPS/cycle = 12 GFLOPS/s per core

## Frequency Scaling on RPi4

```bash
# Set performance governor
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Verify current frequency
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
```

## Cross-Compilation

Use the AArch64 toolchain file when building from an x86-64 host:

```bash
cmake -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBENCH_ENABLE_NEON=ON
```

Then copy binaries to the RPi4 and run them there.
