cmake_minimum_required(VERSION 3.25)
project(cpu_microbench VERSION 1.0.0 LANGUAGES CXX)

# Export compile_commands.json so VS Code IntelliSense and clangd can
# resolve all include paths and compiler flags without manual configuration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- User-facing options ----
# Tri-state options (AUTO/ON/OFF): AUTO probes the host compiler at configure
# time and enables the feature if the compiler supports it.
# Boolean options use standard ON/OFF.
set(BENCH_ENABLE_AVX2   "AUTO" CACHE STRING "Enable -mavx2   (AUTO=detect, ON, OFF)")
set(BENCH_ENABLE_FMA    "AUTO" CACHE STRING "Enable -mfma    (AUTO=detect, ON, OFF)")
set(BENCH_ENABLE_NEON   "AUTO" CACHE STRING "Enable AArch64 NEON (AUTO=detect, ON, OFF)")
set(BENCH_WITH_NUMA     "AUTO" CACHE STRING "Link libnuma    (AUTO=detect, ON, OFF)")

option(BENCH_ENABLE_AVX512 "Enable -mavx512f (Skylake-X+; causes freq throttling)" OFF)
option(BENCH_ENABLE_PERF   "Compile perf_event_open wrappers"                      ON)
option(BENCH_SANITIZE      "Enable AddressSanitizer + UBSan (tests only)"          OFF)
option(BENCH_LTO           "Enable link-time optimisation (intentionally discouraged)" OFF)
option(BENCHMARK_DOWNLOAD_DEPENDENCIES "Fetch Google Benchmark via FetchContent"   ON)

# ---- CMake modules ----
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(DetectArch)
include(PerfCounters)
include(Sanitizers)

# ---- LTO guard ----
if(BENCH_LTO)
    message(WARNING
        "BENCH_LTO=ON: the linker may constant-fold or eliminate benchmark loops. "
        "This flag is intentionally discouraged for benchmarks.")
endif()

# ---- Google Benchmark ----
if(BENCHMARK_DOWNLOAD_DEPENDENCIES)
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.0
    )
    # Build benchmark without its own tests
    set(BENCHMARK_ENABLE_TESTING   OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
else()
    find_package(benchmark REQUIRED)
endif()

# ---- OpenMP (for multi-threaded STREAM) ----
find_package(OpenMP)

# ---- Shared compile flags ----
# Applied to every target via an interface library so they are not leaked
# into FetchContent dependencies.
add_library(bench_compile_options INTERFACE)

target_compile_options(bench_compile_options INTERFACE
    -O3
    -Wall
    -Wextra
    -Wpedantic
)

# Add architecture-specific flags detected by DetectArch.cmake
if(HAVE_AVX2)
    target_compile_options(bench_compile_options INTERFACE -mavx2)
endif()
if(HAVE_FMA)
    target_compile_options(bench_compile_options INTERFACE -mfma)
endif()
if(BENCH_ENABLE_AVX512 AND HAVE_AVX512)
    target_compile_options(bench_compile_options INTERFACE -mavx512f)
endif()

# ---- Helper function: create a benchmark executable ----
function(add_bench name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${name} PRIVATE
        bench_compile_options
        benchmark::benchmark
        benchmark::benchmark_main
        $<$<BOOL:${OpenMP_FOUND}>:OpenMP::OpenMP_CXX>
        $<$<BOOL:${PERF_EVENT_AVAILABLE}>:${CMAKE_DL_LIBS}>
    )
    if(BENCH_WITH_NUMA AND NUMA_FOUND)
        target_link_libraries(${name} PRIVATE numa)
        target_compile_definitions(${name} PRIVATE BENCH_HAS_NUMA=1)
    endif()
    if(PERF_EVENT_AVAILABLE AND BENCH_ENABLE_PERF)
        target_compile_definitions(${name} PRIVATE BENCH_HAS_PERF=1)
    endif()
    if(HAVE_AVX2)
        target_compile_definitions(${name} PRIVATE BENCH_HAS_AVX2=1)
    endif()
endfunction()

# ---- Benchmark targets ----
add_bench(pointer_chase   src/pointer_chase.cpp)
add_bench(stream_bench    src/stream.cpp)
add_bench(peak_flops      src/peak_flops.cpp)
add_bench(gemm_tile_bench src/gemm_tile_predict.cpp)

# Scalar baseline for pointer_chase does not want auto-vectorisation
target_compile_options(pointer_chase PRIVATE -fno-tree-vectorize)

# STREAM: aggressive unrolling for the vectorised variants; also disable
# auto-vectorisation at the translation-unit level so that the scalar_*
# functions (which use BENCH_NO_VECTORIZE) are never accidentally vectorised
# on Clang (which ignores __attribute__((optimize("no-tree-vectorize")))).
# The vectorised variants opt back in explicitly via #pragma GCC ivdep.
target_compile_options(stream_bench PRIVATE -funroll-loops -fno-tree-vectorize)

# ---- Tests ----
enable_testing()

add_executable(test_permutation   tests/test_permutation.cpp)
target_include_directories(test_permutation   PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_permutation        PRIVATE bench_compile_options)
add_test(NAME permutation   COMMAND test_permutation)

add_executable(test_stream_verify tests/test_stream_verify.cpp)
target_include_directories(test_stream_verify PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_stream_verify
    PRIVATE bench_compile_options
    $<$<BOOL:${OpenMP_FOUND}>:OpenMP::OpenMP_CXX>
)
if(HAVE_AVX2)
    target_compile_options(test_stream_verify PRIVATE -mavx2 -mfma -funroll-loops)
    target_compile_definitions(test_stream_verify PRIVATE BENCH_HAS_AVX2=1)
endif()
add_test(NAME stream_verify COMMAND test_stream_verify)
