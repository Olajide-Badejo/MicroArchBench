// src/peak_flops.cpp
// Peak FLOPS benchmarks for AVX2/FMA3 (x86-64) and NEON (AArch64).
//
// Strategy: unroll 10 independent FMA accumulator chains to overcome
// the FMA pipeline latency (4-5 cycles on most modern cores) without
// introducing data dependencies between chains. This saturates the FMA
// execution ports at their maximum throughput.
//
// Key correctness notes:
//   - Multiplier must NOT be 1.0 -- strength reduction turns fma(a, 1, b)
//     into an add, which uses a different port and gives wrong peak numbers.
//     Using 1.0f + 1e-7f prevents this optimisation.
//   - Accumulators are reset to distinct non-zero values at the start of each
//     outer (state) iteration so they cannot overflow to infinity across
//     repeated timing loops. Without the reset, large rep*iteration products
//     (e.g. 100000 reps * 1000 iterations) would eventually reach FP infinity.
//   - A tree reduction after the inner loop prevents dead-code elimination;
//     the compiler cannot prove the result is unused.

#ifdef BENCH_HAS_AVX2
#include <immintrin.h>
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include <benchmark/benchmark.h>

#include "bench_utils.hpp"
#include "topology.hpp"

// Number of independent accumulator chains.
// Must be >= FMA_LATENCY / FMA_RECIPROCAL_THROUGHPUT
// Skylake: 4/0.5 = 8; Zen 3: 4/0.5 = 8. 10 is safely above both.
static constexpr int UNROLL = 10;

// ---------------------------------------------------------------------------
// AVX2 / FMA3 -- single precision (SP)
// Peak = 2 FMA units x 2 FLOPs/FMA x 8 floats/register = 32 FLOPS/cycle
// ---------------------------------------------------------------------------

#ifdef BENCH_HAS_AVX2

static void BM_PeakFLOPS_AVX2_SP(benchmark::State& state) {
    const __m256 b = _mm256_set1_ps(1.0000001f);

    // Total FLOPs per inner loop rep: UNROLL FMAs * 2 FLOPs/FMA * 8 floats
    const long long flops_per_rep = 2LL * 8 * UNROLL;
    long long total_flops = 0;

    for (auto _ : state) {
        const int reps = static_cast<int>(state.range(0));

        // Reset accumulators at the start of each timed iteration.
        // Without this, values grow geometrically and reach FP infinity
        // when reps * state.iterations() is large (e.g. 100000 * 1000).
        __m256 acc[UNROLL];
        for (int i = 0; i < UNROLL; ++i)
            acc[i] = _mm256_set1_ps(static_cast<float>(i + 1));

        for (int r = 0; r < reps; ++r) {
            acc[0] = _mm256_fmadd_ps(acc[0], b, b);
            acc[1] = _mm256_fmadd_ps(acc[1], b, b);
            acc[2] = _mm256_fmadd_ps(acc[2], b, b);
            acc[3] = _mm256_fmadd_ps(acc[3], b, b);
            acc[4] = _mm256_fmadd_ps(acc[4], b, b);
            acc[5] = _mm256_fmadd_ps(acc[5], b, b);
            acc[6] = _mm256_fmadd_ps(acc[6], b, b);
            acc[7] = _mm256_fmadd_ps(acc[7], b, b);
            acc[8] = _mm256_fmadd_ps(acc[8], b, b);
            acc[9] = _mm256_fmadd_ps(acc[9], b, b);
        }

        // Tree reduction prevents dead-code elimination of the accumulator chain
        __m256 sum = _mm256_setzero_ps();
        for (int i = 0; i < UNROLL; ++i)
            sum = _mm256_add_ps(sum, acc[i]);
        benchmark::DoNotOptimize(sum);

        total_flops += static_cast<long long>(reps) * flops_per_rep;
    }

    state.counters["GFLOPS"] = benchmark::Counter(
        static_cast<double>(total_flops) / 1.0e9,
        benchmark::Counter::kIsRate);
}

// Double precision variant (DP)
// Peak = 2 FMA units x 2 FLOPs/FMA x 4 doubles = 16 FLOPS/cycle
static void BM_PeakFLOPS_AVX2_DP(benchmark::State& state) {
    const __m256d b = _mm256_set1_pd(1.0000000001);
    const long long flops_per_rep = 2LL * 4 * UNROLL;
    long long total_flops = 0;

    for (auto _ : state) {
        const int reps = static_cast<int>(state.range(0));

        __m256d acc[UNROLL];
        for (int i = 0; i < UNROLL; ++i)
            acc[i] = _mm256_set1_pd(static_cast<double>(i + 1));

        for (int r = 0; r < reps; ++r) {
            acc[0] = _mm256_fmadd_pd(acc[0], b, b);
            acc[1] = _mm256_fmadd_pd(acc[1], b, b);
            acc[2] = _mm256_fmadd_pd(acc[2], b, b);
            acc[3] = _mm256_fmadd_pd(acc[3], b, b);
            acc[4] = _mm256_fmadd_pd(acc[4], b, b);
            acc[5] = _mm256_fmadd_pd(acc[5], b, b);
            acc[6] = _mm256_fmadd_pd(acc[6], b, b);
            acc[7] = _mm256_fmadd_pd(acc[7], b, b);
            acc[8] = _mm256_fmadd_pd(acc[8], b, b);
            acc[9] = _mm256_fmadd_pd(acc[9], b, b);
        }

        __m256d sum = _mm256_setzero_pd();
        for (int i = 0; i < UNROLL; ++i)
            sum = _mm256_add_pd(sum, acc[i]);
        benchmark::DoNotOptimize(sum);

        total_flops += static_cast<long long>(reps) * flops_per_rep;
    }

    state.counters["GFLOPS"] = benchmark::Counter(
        static_cast<double>(total_flops) / 1.0e9,
        benchmark::Counter::kIsRate);
}

// Register with several rep counts to average over different loop-body counts
BENCHMARK(BM_PeakFLOPS_AVX2_SP)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(0.5);

BENCHMARK(BM_PeakFLOPS_AVX2_DP)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(0.5);

#endif // BENCH_HAS_AVX2

// ---------------------------------------------------------------------------
// AArch64 NEON -- single precision
// Peak = 2 FP/SIMD pipes x 2 FLOPs/FMA x 4 floats/register = 16 FLOPS/cycle
// (varies by core; Cortex-A72 has 1 FP pipe, so 8 FLOPS/cycle)
// ---------------------------------------------------------------------------

#ifdef __ARM_NEON

static void BM_PeakFLOPS_NEON_SP(benchmark::State& state) {
    const float32x4_t b = vdupq_n_f32(1.0000001f);
    const long long flops_per_rep = 2LL * 4 * UNROLL;
    long long total_flops = 0;

    for (auto _ : state) {
        const int reps = static_cast<int>(state.range(0));

        float32x4_t acc[UNROLL];
        for (int i = 0; i < UNROLL; ++i)
            acc[i] = vdupq_n_f32(static_cast<float>(i + 1));

        for (int r = 0; r < reps; ++r) {
            acc[0] = vfmaq_f32(b, acc[0], b);
            acc[1] = vfmaq_f32(b, acc[1], b);
            acc[2] = vfmaq_f32(b, acc[2], b);
            acc[3] = vfmaq_f32(b, acc[3], b);
            acc[4] = vfmaq_f32(b, acc[4], b);
            acc[5] = vfmaq_f32(b, acc[5], b);
            acc[6] = vfmaq_f32(b, acc[6], b);
            acc[7] = vfmaq_f32(b, acc[7], b);
            acc[8] = vfmaq_f32(b, acc[8], b);
            acc[9] = vfmaq_f32(b, acc[9], b);
        }

        float32x4_t sum = vdupq_n_f32(0.0f);
        for (int i = 0; i < UNROLL; ++i)
            sum = vaddq_f32(sum, acc[i]);
        benchmark::DoNotOptimize(sum);

        total_flops += static_cast<long long>(reps) * flops_per_rep;
    }

    state.counters["GFLOPS"] = benchmark::Counter(
        static_cast<double>(total_flops) / 1.0e9,
        benchmark::Counter::kIsRate);
}

BENCHMARK(BM_PeakFLOPS_NEON_SP)
    ->Arg(1000)->Arg(10000)->Arg(100000)
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(0.5);

#endif // __ARM_NEON
