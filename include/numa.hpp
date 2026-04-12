#pragma once
// include/numa.hpp
// Thin wrappers around libnuma for NUMA-local allocation and thread pinning.
// All functions degrade gracefully when libnuma is not available or when
// BENCH_HAS_NUMA is not defined by CMake.

#include <cstddef>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <sched.h>    // cpu_set_t, CPU_ZERO, CPU_SET, sched_setaffinity
#include <unistd.h>   // sysconf
#endif

#ifdef BENCH_HAS_NUMA
#include <numa.h>
#include <numaif.h>
#endif

// ---------------------------------------------------------------------------
// numa_available_safe
// Returns true if the system has more than one NUMA node AND libnuma
// was compiled in. Single-socket systems always return false.
// ---------------------------------------------------------------------------

inline bool numa_available_safe() {
#ifdef BENCH_HAS_NUMA
    return numa_available() >= 0 && numa_max_node() > 0;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// numa_alloc_local_safe
// Allocates `size` bytes from the NUMA node local to the calling thread.
// Falls back to posix_memalign when libnuma is not present or not available.
//
// IMPORTANT: the allocator used is tracked implicitly:
//   - When BENCH_HAS_NUMA and numa_available() >= 0: uses numa_alloc_local.
//     Returns nullptr on failure rather than falling back to posix_memalign,
//     so that numa_free_safe can unconditionally call numa_free in that case.
//   - Otherwise: uses posix_memalign. Free with regular free().
// Use numa_free_safe to release memory regardless of which path was taken.
// ---------------------------------------------------------------------------

inline void* numa_alloc_local_safe(std::size_t size) {
#ifdef BENCH_HAS_NUMA
    if (numa_available() >= 0) {
        void* p = numa_alloc_local(size);
        if (!p) return nullptr;  // do NOT fall through to posix_memalign
        // Force physical pages onto the local node via first-touch.
        const std::size_t page = 4096;
        for (std::size_t off = 0; off < size; off += page)
            reinterpret_cast<volatile char*>(p)[off] = 0;
        return p;
    }
#endif
    // Fallback: 64-byte aligned allocation via POSIX
    void* p = nullptr;
    if (posix_memalign(&p, 64, size) != 0) return nullptr;
    memset(p, 0, size);
    return p;
}

// ---------------------------------------------------------------------------
// numa_free_safe
// Releases memory allocated by numa_alloc_local_safe.
// Calls numa_free when BENCH_HAS_NUMA and libnuma is available (matching the
// allocator used), otherwise calls free().
// ---------------------------------------------------------------------------

inline void numa_free_safe(void* ptr, std::size_t size) {
    if (!ptr) return;
#ifdef BENCH_HAS_NUMA
    if (numa_available() >= 0) {
        numa_free(ptr, size);
        return;
    }
#endif
    free(ptr);
    (void)size;
}

// ---------------------------------------------------------------------------
// pin_thread_to_core
// Pins the calling thread to `core_id` using sched_setaffinity.
// Useful for eliminating NUMA remote access in single-threaded benchmarks.
// Returns true on success, false if the OS call fails or is unavailable.
// ---------------------------------------------------------------------------

inline bool pin_thread_to_core(int core_id) {
#if defined(__linux__) && !defined(__ANDROID__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)core_id;
    return false;
#endif
}


// ---------------------------------------------------------------------------
// numa_available_safe
// Returns true if the system has more than one NUMA node AND libnuma
// was compiled in. Single-socket systems always return false.
// ---------------------------------------------------------------------------

inline bool numa_available_safe() {
#ifdef BENCH_HAS_NUMA
    return numa_available() >= 0 && numa_max_node() > 0;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// numa_alloc_local_safe
// Allocates `size` bytes from the NUMA node local to the calling thread.
// Falls back to aligned_alloc when libnuma is not present or the system
// is not NUMA-aware.
// Caller must free with numa_free_safe (not delete or free).
// ---------------------------------------------------------------------------

inline void* numa_alloc_local_safe(std::size_t size) {
#ifdef BENCH_HAS_NUMA
    if (numa_available() >= 0) {
        void* p = numa_alloc_local(size);
        if (p) {
            // Force physical pages onto the local node by touching first byte
            // of each page (first-touch policy).
            const std::size_t page = 4096;
            for (std::size_t off = 0; off < size; off += page)
                reinterpret_cast<volatile char*>(p)[off] = 0;
            return p;
        }
    }
#endif
    // Fallback: 64-byte aligned allocation
    void* p = nullptr;
    if (posix_memalign(&p, 64, size) != 0) return nullptr;
    memset(p, 0, size);
    return p;
}

// ---------------------------------------------------------------------------
// numa_free_safe
// Pair of numa_alloc_local_safe. Calls numa_free if libnuma is available,
// otherwise calls free (posix_memalign blocks are free()-able).
// ---------------------------------------------------------------------------

inline void numa_free_safe(void* ptr, std::size_t size) {
    if (!ptr) return;
#ifdef BENCH_HAS_NUMA
    if (numa_available() >= 0) {
        numa_free(ptr, size);
        return;
    }
#endif
    free(ptr);
    (void)size;
}

// ---------------------------------------------------------------------------
// pin_thread_to_core
// Pins the calling thread to `core_id` using sched_setaffinity.
// Useful for eliminating NUMA remote access in single-threaded benchmarks.
// Returns true on success.
// ---------------------------------------------------------------------------

inline bool pin_thread_to_core(int core_id) {
#ifdef __linux__
#ifndef __ANDROID__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#endif
#endif
    (void)core_id;
    return false;
}
