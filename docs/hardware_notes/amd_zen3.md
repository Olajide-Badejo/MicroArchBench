# AMD Zen 3 -- PMU Notes

## CPU Family / Model IDs

| Micro-architecture | Family | Model (hex) |
|--------------------|--------|------------|
| Zen 3 (Vermeer)    | 25     | 0x21       |
| Zen 3 (Cezanne)    | 25     | 0x50       |
| Zen 4 (Raphael)    | 25     | 0x61       |

## Key Hardware Events

AMD uses a subset of the standard perf event names as well as vendor-specific
raw events. Use `perf list` on the target machine to confirm availability.

| Symbolic Name                          | Raw Event | Description |
|----------------------------------------|-----------|-------------|
| `l1-dcache-loads`                      | generic   | L1d loads |
| `l1-dcache-load-misses`                | generic   | L1d load misses |
| `l2_cache_miss.from_l1_miss`           | 0x060064  | L2 miss from L1 miss |
| `l3_cache_misses`                      | 0xFF0500  | LLC misses (approximation) |
| `FpRetSseAvxOps.SingleMulAddOps`       | 0x01003  | SP FMA ops retired |
| `FpRetSseAvxOps.SingleDivOps`          | 0x08003  | SP divide ops retired |
| `IcOp.FpDispatchFaults`               | vendor    | FP dispatch faults |

## Latency Reference Values (Ryzen 9 5950X @ 3.4 GHz base)

| Level | Cycles | ns (at 3.4 GHz) |
|-------|--------|-----------------|
| L1d   | 4      | 1.18 |
| L2    | 12     | 3.5 |
| L3    | 40     | 11.8 |
| DRAM  | varies | 60-90 |

Zen 3 has a unified L3 (32 MB per CCD) with very low intra-CCD latency.
Cross-CCD access adds 40-50 ns.

## Bandwidth Reference Values (5950X, DDR4-3200, dual channel)

| Level | Read BW  |
|-------|----------|
| L1    | 600 GB/s |
| L2    | 400 GB/s |
| L3    | 200 GB/s |
| DRAM  | 50 GB/s  |

## Boost Disable

```bash
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
sudo cpupower frequency-set -g performance
```

Re-enable:

```bash
echo 1 | sudo tee /sys/devices/system/cpu/cpufreq/boost
```

## perf Event Notes

AMD Zen 3 supports the standard `perf stat` generic events reasonably well.
For vendor-specific events use raw format with `-e r<eventcode>`:

```bash
perf stat -e r060064 ./build/pointer_chase  # L2 miss from L1 miss
```

The `likwid-perfctr` tool provides better AMD PMU coverage than raw perf
for Zen micro-architectures.
