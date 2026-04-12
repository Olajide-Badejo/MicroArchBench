// src/pointer_chase.cpp
// Pointer-chase (linked-list traversal) latency microbenchmark.
//
// Measures load-use latency for L1, L2, L3, and DRAM by sweeping the
// working-set size from 4 KiB to 512 MiB. Each node is one cache line
// (64 bytes). The traversal order is a random permutation (Fisher-Yates)
// so the hardware stride prefetcher sees no exploitable pattern.
//
// The inner loop must not be speculated across iterations -- each load
// address is derived from the VALUE returned by the previous load.
// That is the definition of a dependent load chain.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "bench_utils.hpp"
#include "numa.hpp"
#include "topology.hpp"

// ---------------------------------------------------------------------------
// Node layout
// One node per cache line (64 bytes). The `next` pointer occupies the first 8
// bytes; the remaining 56 bytes are padding to guarantee exactly one node per
// line and prevent false sharing.
// ---------------------------------------------------------------------------

struct alignas(64) Node {
    Node* next;
    char  pad[56];  // pad to fill a full 64-byte cache line
};

static_assert(sizeof(Node) == 64, "Node must be exactly one cache line");

// ---------------------------------------------------------------------------
// build_chase_list
// Allocates `n_nodes` nodes and links them in a random permutation such that
// every node is visited exactly once per traversal before the list wraps.
// The permutation is computed via std::shuffle (Fisher-Yates internally).
// ---------------------------------------------------------------------------

static Node* build_chase_list(std::size_t n_nodes) {
    if (n_nodes == 0) throw std::invalid_argument("n_nodes must be > 0");

    // Allocate NUMA-local if available, otherwise 64-byte aligned.
    // Use a raw pointer held in a local variable; on exception the catch block
    // frees it before rethrowing, preventing a leak if the perm vector throws.
    Node* nodes = static_cast<Node*>(
        numa_alloc_local_safe(n_nodes * sizeof(Node)));
    if (!nodes) throw std::bad_alloc{};

    try {
        // Build random permutation of indices (Fisher-Yates via std::shuffle)
        std::vector<std::size_t> perm(n_nodes);
        std::iota(perm.begin(), perm.end(), 0);

        // Fixed seed: reproducible results across runs.
        // Latency does not depend on which particular permutation is used,
        // only that it is uniformly random so the prefetcher sees no pattern.
        std::mt19937_64 rng(0xDEADBEEF42ULL);
        std::shuffle(perm.begin(), perm.end(), rng);

        // Link: perm[0] -> perm[1] -> ... -> perm[n-1] -> perm[0] (circular)
        for (std::size_t i = 0; i < n_nodes - 1; ++i)
            nodes[perm[i]].next = &nodes[perm[i + 1]];
        nodes[perm[n_nodes - 1]].next = &nodes[perm[0]];
    } catch (...) {
        numa_free_safe(nodes, n_nodes * sizeof(Node));
        throw;
    }

    return nodes;
}

// How many full traversals to perform per benchmark iteration.
// More repetitions amortise loop overhead; 4 is sufficient for large arrays.
static constexpr int CHASE_REPS = 4;

// ---------------------------------------------------------------------------
// BM_PointerChase
// Google Benchmark entry point. `state.range(0)` is the working-set size in
// bytes. The benchmark reports nanoseconds-per-load as a custom counter.
// ---------------------------------------------------------------------------

static void BM_PointerChase(benchmark::State& state) {
    const std::size_t ws_bytes = static_cast<std::size_t>(state.range(0));
    const std::size_t n_nodes  = ws_bytes / sizeof(Node);

    if (n_nodes < 2) {
        state.SkipWithError("working set too small for pointer chase");
        return;
    }

    Node* head = build_chase_list(n_nodes);

    // Warm-up: one full traversal to bring the working set into the TLB
    // and resolve physical page allocations before timing begins.
    {
        volatile Node* p = head;
        for (std::size_t i = 0; i < n_nodes; ++i)
            p = p->next;
        DoNotOptimize(p);
    }

    const std::size_t total_loads_per_iter =
        static_cast<std::size_t>(n_nodes) * CHASE_REPS;

    for (auto _ : state) {
        // Use volatile to prevent the compiler from treating the dependent
        // load chain as a reduction it can optimise away.
        volatile Node* p = head;
        for (std::size_t i = 0; i < total_loads_per_iter; ++i)
            p = p->next;
        benchmark::DoNotOptimize(p);
    }

    // Label with cache-level classification
    state.SetLabel(classify_working_set(ws_bytes));

    // Report latency per individual load in nanoseconds
    state.counters["ns_per_load"] = benchmark::Counter(
        static_cast<double>(state.iterations()) *
            static_cast<double>(total_loads_per_iter),
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert,
        benchmark::Counter::kIs1000);  // 1000 = ns (not GHz)

    numa_free_safe(head, n_nodes * sizeof(Node));
}

// Register with a working-set sweep: 4 KiB to 512 MiB in 2x steps.
// MinWarmUpTime ensures the TLB is warm before the measured iterations begin.
BENCHMARK(BM_PointerChase)
    ->RangeMultiplier(2)
    ->Range(4   << 10,    // 4 KiB  (should land in L1)
            512 << 20)    // 512 MiB (well into DRAM)
    ->Unit(benchmark::kNanosecond)
    ->MinWarmUpTime(0.5);
