# Intel Skylake / Cascade Lake / Ice Lake -- PMU Notes

## CPU Family / Model IDs (Family 6)

| Micro-architecture | Model (hex) |
|--------------------|------------|
| Skylake            | 0x4E, 0x5E |
| Skylake-X          | 0x55       |
| Cascade Lake       | 0x55 (stepping 7+) |
| Ice Lake           | 0x7D, 0x7E, 0x6A, 0x6C |
| Alder Lake         | 0x97, 0x9A |

## Key Hardware Events

| Symbolic Name                              | Event | Umask | Description |
|--------------------------------------------|-------|-------|-------------|
| `INST_RETIRED.ANY`                         | 0xC0  | 0x00  | Instructions retired |
| `CPU_CLK_UNHALTED.THREAD`                  | 0x3C  | 0x00  | Core cycles (thread) |
| `MEM_LOAD_RETIRED.L1_HIT`                  | 0xD1  | 0x01  | Loads that hit L1 |
| `MEM_LOAD_RETIRED.L2_HIT`                  | 0xD1  | 0x02  | Loads that hit L2 |
| `MEM_LOAD_RETIRED.L3_HIT`                  | 0xD1  | 0x04  | Loads that hit LLC |
| `MEM_LOAD_RETIRED.L3_MISS`                 | 0xD1  | 0x20  | Loads that miss LLC |
| `LLC_REFERENCES`                           | 0x2E  | 0x4F  | LLC references (generic) |
| `LLC_MISSES`                               | 0x2E  | 0x41  | LLC misses (generic) |
| `FP_ARITH_INST_RETIRED.128B_PACKED_SINGLE` | 0xC7  | 0x02  | 128-bit SIMD SP retired |
| `FP_ARITH_INST_RETIRED.256B_PACKED_SINGLE` | 0xC7  | 0x04  | 256-bit SIMD SP retired |

## Latency Reference Values (i7-6700K @ 4.0 GHz)

| Level | Cycles | ns |
|-------|--------|----|
| L1d   | 4      | 1.0 |
| L2    | 12     | 3.0 |
| L3    | 42     | 10.5 |
| DRAM  | varies | 60-80 |

## Bandwidth Reference Values (i7-6700K, DDR4-2133, single channel)

| Level | Read BW  | Notes |
|-------|----------|-------|
| L1    | 300 GB/s | 2 loads + 1 store per cycle |
| L2    | 200 GB/s | 64 bytes/cycle read |
| L3    | 100 GB/s | shared across cores |
| DRAM  | 30 GB/s  | single-channel DDR4-2133 |

## Turbo Boost and Frequency

Disable Turbo Boost before any latency measurement:

```bash
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

Re-enable after benchmarking:

```bash
echo 0 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

## Prefetcher Control (MSR 0x1A4)

To disable all hardware prefetchers on Intel (for latency-without-prefetch measurement):

```bash
sudo modprobe msr
sudo wrmsr -a 0x1A4 0xF    # disable all 4 prefetchers
sudo wrmsr -a 0x1A4 0x0    # re-enable after measurement
```

Bit assignments in MSR 0x1A4:
- Bit 0: L2 Hardware Prefetcher
- Bit 1: L2 Adjacent Cache Line Prefetcher
- Bit 2: L1 DCU Prefetcher
- Bit 3: L1 DCU IP Prefetcher
