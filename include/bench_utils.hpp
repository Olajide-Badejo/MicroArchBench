#pragma once
// include/bench_utils.hpp
// Portable timing utilities, compiler-barrier helpers, and working-set
// classification. All helpers are header-only.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// DoNotOptimize / ClobberMemory
// Borrowed from Google Benchmark's internal implementation.
// Use these to prevent the compiler from eliminating or hoisting loop bodies.
// ---------------------------------------------------------------------------

template <class T>
inline void DoNotOptimize(T const& val) {
    // "r,m" allows the compiler to choose register or memory operand.
    // The "memory" clobber prevents store/load reordering across this point.
    asm volatile("" : : "r,m"(val) : "memory");
}

template <class T>
inline void DoNotOptimize(T& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}

// Inserts a full compiler memory fence -- all prior writes are visible to
// subsequent reads as far as the compiler is concerned.
inline void ClobberMemory() {
    asm volatile("" : : : "memory");
}

// ---------------------------------------------------------------------------
// High-resolution wall-clock timer
// ---------------------------------------------------------------------------

struct WallTimer {
    using Clock = std::chrono::steady_clock;
    using Tp    = Clock::time_point;

    Tp start_{};

    void begin() { start_ = Clock::now(); }

    // Returns elapsed time in nanoseconds.
    double elapsed_ns() const {
        auto end = Clock::now();
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count());
    }

    double elapsed_s() const { return elapsed_ns() * 1e-9; }
};

// ---------------------------------------------------------------------------
// CPU frequency reader
// Reads the current frequency from sysfs. Returns 0 if unavailable (non-Linux
// or no cpufreq driver). Frequency is in Hz.
// ---------------------------------------------------------------------------

inline uint64_t read_cpu_freq_hz() {
#ifdef __linux__
    FILE* f = fopen(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (!f) return 0;
    unsigned long long khz = 0;
    if (fscanf(f, "%llu", &khz) != 1) khz = 0;
    fclose(f);
    return static_cast<uint64_t>(khz) * 1000ULL;  // kHz to Hz
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Working-set classifier
// Given a buffer size in bytes, returns a human-readable label indicating
// which cache level the working set is expected to reside in.
// Cache sizes are read from /sys/devices/system/cpu/ when possible; otherwise
// conservative defaults are used.
// ---------------------------------------------------------------------------

struct CacheSizes {
    std::size_t l1_bytes = 32   * 1024;
    std::size_t l2_bytes = 256  * 1024;
    std::size_t l3_bytes = 8    * 1024 * 1024;
};

inline CacheSizes read_cache_sizes() {
    CacheSizes cs{};
#ifdef __linux__
    // Try index0=L1d, index2=L2, index3=L3 (kernel cache topology layout)
    static const char* paths[] = {
        "/sys/devices/system/cpu/cpu0/cache/index0/size",  // L1d
        "/sys/devices/system/cpu/cpu0/cache/index2/size",  // L2
        "/sys/devices/system/cpu/cpu0/cache/index3/size",  // L3
    };
    std::size_t* targets[] = { &cs.l1_bytes, &cs.l2_bytes, &cs.l3_bytes };
    for (int i = 0; i < 3; ++i) {
        FILE* f = fopen(paths[i], "r");
        if (!f) continue;
        char buf[32]{};
        if (fgets(buf, sizeof(buf), f)) {
            std::size_t val = 0;
            char suffix = 0;
            if (sscanf(buf, "%zu%c", &val, &suffix) >= 1) {
                if (suffix == 'K' || suffix == 'k') val *= 1024;
                else if (suffix == 'M' || suffix == 'm') val *= 1024 * 1024;
                *targets[i] = val;
            }
        }
        fclose(f);
    }
#endif
    return cs;
}

inline std::string classify_working_set(std::size_t bytes) {
    static const CacheSizes cs = read_cache_sizes();
    if (bytes <= cs.l1_bytes)       return "L1";
    else if (bytes <= cs.l2_bytes)  return "L2";
    else if (bytes <= cs.l3_bytes)  return "L3";
    else                            return "DRAM";
}

// ---------------------------------------------------------------------------
// Alignment helpers
// ---------------------------------------------------------------------------

// Returns the next multiple of `align` that is >= `val`.
inline std::size_t align_up(std::size_t val, std::size_t align) {
    return (val + align - 1) & ~(align - 1);
}
