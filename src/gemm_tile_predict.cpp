// src/gemm_tile_predict.cpp
// Blocked GEMM tile-size validation benchmark.
//
// Implements a naive (non-BLAS) blocked FP32 GEMM C += A * B and sweeps
// over several (Mc, Kc, Nc) tile configurations. Results are compared to
// the tile sizes predicted by scripts/tile_predict.py using measured hardware
// parameters.
//
// This benchmark is NOT a performance-competitive GEMM -- it is a
// cross-validation tool for the analytical tile-size model.
//
// Matrix dimensions: N x N where N = state.range(0)
// Thread count: single-threaded (tile prediction targets one core)

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <benchmark/benchmark.h>

#include "bench_utils.hpp"

// ---------------------------------------------------------------------------
// Blocked GEMM C += A * B  (all FP32, row-major)
// Tile sizes: (Mc, Kc, Nc)
//   - Mc x Kc submatrix of A fits in L1 (ideally)
//   - Kc x Nc submatrix of B fits in L2 (ideally)
//   - Mc x Nc submatrix of C fits in registers (ideally)
// ---------------------------------------------------------------------------

static void blocked_sgemm(
        int N,
        const float* __restrict__ A,  // N x N
        const float* __restrict__ B,  // N x N
        float*       __restrict__ C,  // N x N
        int Mc, int Kc, int Nc)
{
    for (int mc = 0; mc < N; mc += Mc) {
        const int mc_end = std::min(mc + Mc, N);
        for (int kc = 0; kc < N; kc += Kc) {
            const int kc_end = std::min(kc + Kc, N);
            for (int nc = 0; nc < N; nc += Nc) {
                const int nc_end = std::min(nc + Nc, N);
                // Micro-kernel: C[mc:mc_end, nc:nc_end] += A[mc:mc_end, kc:kc_end]
                //                                          * B[kc:kc_end, nc:nc_end]
                for (int i = mc; i < mc_end; ++i) {
                    for (int k = kc; k < kc_end; ++k) {
                        const float a_ik = A[i * N + k];
#pragma GCC ivdep
                        for (int j = nc; j < nc_end; ++j)
                            C[i * N + j] += a_ik * B[k * N + j];
                    }
                }
            }
        }
    }
    ClobberMemory();
}

// ---------------------------------------------------------------------------
// Helper: allocate and zero-fill an N x N matrix (64-byte aligned)
// ---------------------------------------------------------------------------

static std::unique_ptr<float, decltype(&free)>
make_matrix(int N) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, static_cast<std::size_t>(N) * N * sizeof(float)))
        throw std::bad_alloc{};
    memset(ptr, 0, static_cast<std::size_t>(N) * N * sizeof(float));
    return {static_cast<float*>(ptr), free};
}

// ---------------------------------------------------------------------------
// Benchmark: sweep over (Mc, Kc, Nc) configurations
// state.range(0) = N (matrix dimension)
// state.range(1) = Mc
// state.range(2) = Kc
// state.range(3) = Nc
// ---------------------------------------------------------------------------

static void BM_BlockedSGEMM(benchmark::State& state) {
    const int N  = static_cast<int>(state.range(0));
    const int Mc = static_cast<int>(state.range(1));
    const int Kc = static_cast<int>(state.range(2));
    const int Nc = static_cast<int>(state.range(3));

    auto A = make_matrix(N);
    auto B = make_matrix(N);
    auto C = make_matrix(N);

    // Initialise A and B with simple values
    for (int i = 0; i < N * N; ++i) {
        A.get()[i] = static_cast<float>(i % 17) * 0.01f;
        B.get()[i] = static_cast<float>(i % 13) * 0.01f;
    }

    // Warm-up: one full GEMM to bring working set into cache and stabilise
    blocked_sgemm(N, A.get(), B.get(), C.get(), Mc, Kc, Nc);
    memset(C.get(), 0, static_cast<std::size_t>(N) * N * sizeof(float));

    for (auto _ : state) {
        blocked_sgemm(N, A.get(), B.get(), C.get(), Mc, Kc, Nc);
    }

    // Compute achieved GFLOPS: 2*N^3 FLOPs (multiply-add pairs)
    const double flops = 2.0 * N * N * N;
    state.counters["GFLOPS"] = benchmark::Counter(
        flops * static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate,
        benchmark::Counter::kIs1000000000);

    // Embed tile sizes in the label for easy parsing
    state.SetLabel("Mc=" + std::to_string(Mc) +
                   " Kc=" + std::to_string(Kc) +
                   " Nc=" + std::to_string(Nc));
}

// Matrix size N=512 -- large enough to see tiling effects, fast enough to sweep
// Tile configurations:
//   (48, 256, 2040) -- analytically predicted for i7-12700K
//   (16, 128, 1024) -- smaller tiles
//   (64, 512, 4096) -- larger tiles (expected to thrash L2)
//   (32, 256, 512)  -- medium tiles
BENCHMARK(BM_BlockedSGEMM)
    ->Args({512,  48, 256, 2040})
    ->Args({512,  16, 128, 1024})
    ->Args({512,  64, 512, 4096})
    ->Args({512,  32, 256,  512})
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(0.5)
    ->Iterations(3);
