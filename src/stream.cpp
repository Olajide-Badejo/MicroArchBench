// src/stream.cpp
// STREAM-style memory bandwidth benchmarks.
//
// Implements four kernels (Copy, Scale, Add, Triad) in three variants:
//   1. Scalar C++ with auto-vectorisation disabled (baseline)
//   2. Compiler-vectorised with #pragma GCC ivdep hint
//   3. Hand-written AVX2 with non-temporal stores (DRAM-level only)
//
// Working set is 4x LLC size for DRAM-level measurements, and 70% of
// target cache size for cache-level bandwidth.
//
// Multi-threaded scaling is controlled via OpenMP; NUMA first-touch
// initialisation binds pages to the local socket.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef BENCH_HAS_AVX2
#include <immintrin.h>
#endif

#include <benchmark/benchmark.h>

#include "bench_utils.hpp"
#include "numa.hpp"
#include "topology.hpp"

// ---------------------------------------------------------------------------
// Allocation helpers
// All arrays are 64-byte aligned for AVX2 load/store instructions.
// ---------------------------------------------------------------------------

struct StreamArrays {
    float* a = nullptr;
    float* b = nullptr;
    float* c = nullptr;
    std::size_t n = 0;

    explicit StreamArrays(std::size_t n_floats) : n(n_floats) {
        // posix_memalign is used (not new[]) so that NUMA first-touch works
        // correctly with OpenMP and so that NT-store alignment is guaranteed.
        if (posix_memalign(reinterpret_cast<void**>(&a), 64, n * sizeof(float))) {
            throw std::bad_alloc{};
        }
        if (posix_memalign(reinterpret_cast<void**>(&b), 64, n * sizeof(float))) {
            free(a); a = nullptr;
            throw std::bad_alloc{};
        }
        if (posix_memalign(reinterpret_cast<void**>(&c), 64, n * sizeof(float))) {
            free(a); a = nullptr;
            free(b); b = nullptr;
            throw std::bad_alloc{};
        }

        // NUMA first-touch: touch every page from the calling thread so that
        // the OS maps physical pages to the local NUMA node.
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (std::size_t i = 0; i < n; ++i) {
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(i) * 2.0f;
            c[i] = 0.0f;
        }
    }

    ~StreamArrays() {
        free(a);
        free(b);
        free(c);
    }

    StreamArrays(const StreamArrays&)            = delete;
    StreamArrays& operator=(const StreamArrays&) = delete;
};

// ---------------------------------------------------------------------------
// Scalar kernel implementations
//
// We need to prevent the compiler from auto-vectorising these functions so
// they serve as honest scalar baselines. The approach differs by compiler:
//   - GCC:   __attribute__((optimize("no-tree-vectorize")))
//   - Clang: __attribute__((optnone)) disables ALL optimisation, which is too
//            heavy. Instead we rely on -fno-tree-vectorize applied per-target
//            in CMakeLists.txt (target_compile_options for stream_bench).
//
// The BENCH_NO_VECTORIZE macro expands to the GCC attribute where supported
// and is empty on Clang (the CMake flag covers it there).
// ---------------------------------------------------------------------------

#if defined(__GNUC__) && !defined(__clang__)
#  define BENCH_NO_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
#else
#  define BENCH_NO_VECTORIZE   /* Clang: rely on -fno-tree-vectorize per-target */
#endif

BENCH_NO_VECTORIZE
static void scalar_copy(float* __restrict__ c,
                         const float* __restrict__ a,
                         std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i];
    ClobberMemory();
}

BENCH_NO_VECTORIZE
static void scalar_scale(float* __restrict__ b,
                          const float* __restrict__ a,
                          float s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        b[i] = s * a[i];
    ClobberMemory();
}

BENCH_NO_VECTORIZE
static void scalar_add(float* __restrict__ c,
                        const float* __restrict__ a,
                        const float* __restrict__ b,
                        std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i] + b[i];
    ClobberMemory();
}

BENCH_NO_VECTORIZE
static void scalar_triad(float* __restrict__ c,
                          const float* __restrict__ a,
                          const float* __restrict__ b,
                          float s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i] + s * b[i];
    ClobberMemory();
}

// ---------------------------------------------------------------------------
// Compiler-vectorised kernels
// #pragma GCC ivdep tells the compiler there are no aliasing dependencies;
// combined with -O3 -march=native this triggers auto-vectorisation.
// ---------------------------------------------------------------------------

static void vec_copy(float* __restrict__ c,
                     const float* __restrict__ a,
                     std::size_t n) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i];
    ClobberMemory();
}

static void vec_scale(float* __restrict__ b,
                      const float* __restrict__ a,
                      float s, std::size_t n) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
    for (std::size_t i = 0; i < n; ++i)
        b[i] = s * a[i];
    ClobberMemory();
}

static void vec_add(float* __restrict__ c,
                    const float* __restrict__ a,
                    const float* __restrict__ b,
                    std::size_t n) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i] + b[i];
    ClobberMemory();
}

static void vec_triad(float* __restrict__ c,
                      const float* __restrict__ a,
                      const float* __restrict__ b,
                      float s, std::size_t n) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
    for (std::size_t i = 0; i < n; ++i)
        c[i] = a[i] + s * b[i];
    ClobberMemory();
}

// ---------------------------------------------------------------------------
// AVX2 non-temporal store variants
// NT stores bypass the cache on write, eliminating read-for-ownership (RFO)
// traffic. Use ONLY for DRAM-level bandwidth measurement -- NT stores on
// cache-resident data will evict lines and give artificially low numbers.
// ---------------------------------------------------------------------------

#ifdef BENCH_HAS_AVX2

static void avx2_nt_copy(float* __restrict__ c,
                          const float* __restrict__ a,
                          std::size_t n) {
    const std::size_t vec_n = (n / 8) * 8;
    for (std::size_t i = 0; i < vec_n; i += 8) {
        __m256 va = _mm256_load_ps(a + i);
        _mm256_stream_ps(c + i, va);
    }
    // Handle tail (should be 0 if n is a multiple of 8)
    for (std::size_t i = vec_n; i < n; ++i)
        c[i] = a[i];
    _mm_sfence();
}

static void avx2_nt_scale(float* __restrict__ b,
                           const float* __restrict__ a,
                           float s, std::size_t n) {
    const __m256 vs = _mm256_set1_ps(s);
    const std::size_t vec_n = (n / 8) * 8;
    for (std::size_t i = 0; i < vec_n; i += 8) {
        __m256 va = _mm256_load_ps(a + i);
        __m256 vb = _mm256_mul_ps(vs, va);
        _mm256_stream_ps(b + i, vb);
    }
    for (std::size_t i = vec_n; i < n; ++i)
        b[i] = s * a[i];
    _mm_sfence();
}

static void avx2_nt_add(float* __restrict__ c,
                         const float* __restrict__ a,
                         const float* __restrict__ b,
                         std::size_t n) {
    const std::size_t vec_n = (n / 8) * 8;
    for (std::size_t i = 0; i < vec_n; i += 8) {
        __m256 va = _mm256_load_ps(a + i);
        __m256 vb = _mm256_load_ps(b + i);
        __m256 vc = _mm256_add_ps(va, vb);
        _mm256_stream_ps(c + i, vc);
    }
    for (std::size_t i = vec_n; i < n; ++i)
        c[i] = a[i] + b[i];
    _mm_sfence();
}

static void avx2_nt_triad(float* __restrict__ c,
                           const float* __restrict__ a,
                           const float* __restrict__ b,
                           float s, std::size_t n) {
    const __m256 vs = _mm256_set1_ps(s);
    const std::size_t vec_n = (n / 8) * 8;
    for (std::size_t i = 0; i < vec_n; i += 8) {
        __m256 va = _mm256_load_ps(a + i);
        __m256 vb = _mm256_load_ps(b + i);
        // c = a + s*b  --  single FMA instruction: fmadd(s, b, a)
        __m256 vc = _mm256_fmadd_ps(vs, vb, va);
        _mm256_stream_ps(c + i, vc);
    }
    for (std::size_t i = vec_n; i < n; ++i)
        c[i] = a[i] + s * b[i];
    _mm_sfence();
}

#endif // BENCH_HAS_AVX2

// ---------------------------------------------------------------------------
// Benchmark functions
// All four STREAM kernels in three variants (scalar, vec, AVX2-NT).
// state.range(0) = working-set size in bytes per array.
//
// bytes_per_element notation (floats, 4 bytes each):
//   Copy / Scale : 2 streams (1R + 1W) = 8 bytes/element
//   Add / Triad  : 3 streams (2R + 1W) = 12 bytes/element
// ---------------------------------------------------------------------------

// Helper shared by all benchmark bodies
static void record_bw(benchmark::State& state, std::size_t n, int streams) {
    state.SetLabel(classify_working_set(state.range(0)));
    const double bytes = static_cast<double>(state.iterations()) *
                         static_cast<double>(n) *
                         static_cast<double>(streams) * sizeof(float);
    state.counters["BW_GB_s"] = benchmark::Counter(
        bytes / 1.0e9, benchmark::Counter::kIsRate);
}

// ---- Scalar baselines (auto-vectorisation disabled) ----

static void BM_Copy_Scalar(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) scalar_copy(arr.c, arr.a, n);
    record_bw(state, n, 2);
}

static void BM_Scale_Scalar(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) scalar_scale(arr.b, arr.a, 2.3f, n);
    record_bw(state, n, 2);
}

static void BM_Add_Scalar(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) scalar_add(arr.c, arr.a, arr.b, n);
    record_bw(state, n, 3);
}

static void BM_Triad_Scalar(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) scalar_triad(arr.c, arr.a, arr.b, 2.3f, n);
    record_bw(state, n, 3);
}

// ---- Compiler-vectorised variants ----

static void BM_Copy_Vec(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) vec_copy(arr.c, arr.a, n);
    record_bw(state, n, 2);
}

static void BM_Scale_Vec(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) vec_scale(arr.b, arr.a, 2.3f, n);
    record_bw(state, n, 2);
}

static void BM_Add_Vec(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) vec_add(arr.c, arr.a, arr.b, n);
    record_bw(state, n, 3);
}

static void BM_Triad_Vec(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    for (auto _ : state) vec_triad(arr.c, arr.a, arr.b, 2.3f, n);
    record_bw(state, n, 3);
}

// ---- AVX2 non-temporal store variants ----
// Discard the first pass (TLB misses + physical page allocation).
// Only steady-state passes are timed.

#ifdef BENCH_HAS_AVX2

static void BM_Copy_AVX2_NT(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    avx2_nt_copy(arr.c, arr.a, n);
    for (auto _ : state) avx2_nt_copy(arr.c, arr.a, n);
    record_bw(state, n, 2);
}

static void BM_Scale_AVX2_NT(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    avx2_nt_scale(arr.b, arr.a, 2.3f, n);
    for (auto _ : state) avx2_nt_scale(arr.b, arr.a, 2.3f, n);
    record_bw(state, n, 2);
}

static void BM_Add_AVX2_NT(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    avx2_nt_add(arr.c, arr.a, arr.b, n);
    for (auto _ : state) avx2_nt_add(arr.c, arr.a, arr.b, n);
    record_bw(state, n, 3);
}

static void BM_Triad_AVX2_NT(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0)) / sizeof(float);
    StreamArrays arr(n);
    avx2_nt_triad(arr.c, arr.a, arr.b, 2.3f, n);
    for (auto _ : state) avx2_nt_triad(arr.c, arr.a, arr.b, 2.3f, n);
    record_bw(state, n, 3);
}

#endif // BENCH_HAS_AVX2

// ---------------------------------------------------------------------------
// Multi-threaded DRAM bandwidth scaling sweep
// state.range(0) = thread count (1 .. nproc)
//
// Working set is 4x the L3 cache size to guarantee DRAM-level pressure.
// Each thread first-touches its own pages so physical pages are bound to the
// local NUMA node's DRAM channels (NUMA first-touch policy).
//
// The resulting curve shows whether bandwidth saturates at the physical core
// count (expected on dual-channel memory) or continues rising with HT siblings
// (which would indicate the benchmark is still L3-bound).
// ---------------------------------------------------------------------------

#ifdef _OPENMP

static void BM_Triad_MT_Scaling(benchmark::State& state) {
    const int n_threads = static_cast<int>(state.range(0));

    // Save and restore the global OMP thread count so sibling benchmarks
    // are not affected by this function's thread-count choice.
    const int saved_threads = omp_get_max_threads();
    omp_set_num_threads(n_threads);

    // Size the working set at 4x L3 to guarantee DRAM pressure at all thread counts
    const CacheSizes cs = read_cache_sizes();
    const std::size_t ws_bytes = 4 * cs.l3_bytes;
    const std::size_t n = ws_bytes / sizeof(float);

    // Allocate with explicit leak-safe sequencing: free already-allocated
    // arrays before reporting the error if a later allocation fails.
    float* a = nullptr;
    float* b = nullptr;
    float* c = nullptr;

    if (posix_memalign(reinterpret_cast<void**>(&a), 64, n * sizeof(float))) {
        omp_set_num_threads(saved_threads);
        state.SkipWithError("allocation failed (a)");
        return;
    }
    if (posix_memalign(reinterpret_cast<void**>(&b), 64, n * sizeof(float))) {
        free(a);
        omp_set_num_threads(saved_threads);
        state.SkipWithError("allocation failed (b)");
        return;
    }
    if (posix_memalign(reinterpret_cast<void**>(&c), 64, n * sizeof(float))) {
        free(a); free(b);
        omp_set_num_threads(saved_threads);
        state.SkipWithError("allocation failed (c)");
        return;
    }

    // First-touch: each thread initialises its own stripe so the OS binds
    // physical pages to the thread's local NUMA node.
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = static_cast<float>(i) * 0.5f;
        b[i] = static_cast<float>(i) * 0.3f;
        c[i] = 0.0f;
    }

    const float s = 2.3f;

    for (auto _ : state) {
#ifdef BENCH_HAS_AVX2
        const __m256 vs = _mm256_set1_ps(s);
        const std::size_t vec_n = (n / 8) * 8;
#pragma omp parallel for schedule(static) num_threads(n_threads)
        for (std::size_t i = 0; i < vec_n; i += 8) {
            __m256 va = _mm256_load_ps(a + i);
            __m256 vb = _mm256_load_ps(b + i);
            __m256 vc = _mm256_fmadd_ps(vs, vb, va);
            _mm256_stream_ps(c + i, vc);
        }
        _mm_sfence();
#else
#pragma omp parallel for schedule(static) num_threads(n_threads)
        for (std::size_t i = 0; i < n; ++i)
            c[i] = a[i] + s * b[i];
        ClobberMemory();
#endif
    }

    state.SetLabel("DRAM Triad " + std::to_string(n_threads) + "T");

    const double bytes = static_cast<double>(state.iterations()) *
                         static_cast<double>(n) * 3 * sizeof(float);
    state.counters["BW_GB_s"] = benchmark::Counter(
        bytes / 1.0e9, benchmark::Counter::kIsRate);
    state.counters["threads"] = static_cast<double>(n_threads);

    free(a); free(b); free(c);
    omp_set_num_threads(saved_threads);
}

// Register a separate benchmark entry for each thread count from 1 to nproc
// (capped at 32 so the sweep stays within a reasonable runtime).
static const bool kMTRegistered = []() -> bool {
    const int cap = 32;
    const int top = std::min(cap, omp_get_max_threads());
    auto* bm = benchmark::RegisterBenchmark(
        "BM_Triad_MT_Scaling", BM_Triad_MT_Scaling);
    for (int t = 1; t <= top; ++t)
        bm->Arg(t);
    bm->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.5);
    return true;
}();

#endif // _OPENMP

// ---------------------------------------------------------------------------
// Single-threaded registration
// Range: 64 KiB to 256 MiB in 4x steps (L1 cache through DRAM)
// ---------------------------------------------------------------------------

static constexpr int64_t WS_MIN = 64  << 10;   //  64 KiB
static constexpr int64_t WS_MAX = 256 << 20;   // 256 MiB

// Scalar baselines
BENCHMARK(BM_Copy_Scalar) ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Scale_Scalar)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Add_Scalar)  ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Triad_Scalar)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);

// Compiler-vectorised
BENCHMARK(BM_Copy_Vec) ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Scale_Vec)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Add_Vec)  ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);
BENCHMARK(BM_Triad_Vec)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.2);

#ifdef BENCH_HAS_AVX2
// AVX2 non-temporal store variants
BENCHMARK(BM_Copy_AVX2_NT) ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.5);
BENCHMARK(BM_Scale_AVX2_NT)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.5);
BENCHMARK(BM_Add_AVX2_NT)  ->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.5);
BENCHMARK(BM_Triad_AVX2_NT)->RangeMultiplier(4)->Range(WS_MIN, WS_MAX)->Unit(benchmark::kMillisecond)->MinWarmUpTime(0.5);
#endif

