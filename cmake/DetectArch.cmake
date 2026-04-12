# cmake/DetectArch.cmake
# Probe the host compiler and CPU for ISA support.
# Sets HAVE_AVX2, HAVE_FMA, HAVE_AVX512, HAVE_NEON in parent scope.

include(CheckCXXSourceRuns)
include(CheckCXXSourceCompiles)

# ---- x86: AVX2 + FMA3 ----
if(BENCH_ENABLE_AVX2 OR BENCH_ENABLE_AVX2 STREQUAL "AUTO")
    set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma")
    check_cxx_source_runs("
#include <immintrin.h>
int main() {
    __m256  a = _mm256_setzero_ps();
    __m256  b = _mm256_fmadd_ps(a, a, a);
    (void)b;
    return 0;
}
" HAVE_AVX2_FMA_RUN)
    unset(CMAKE_REQUIRED_FLAGS)

    if(HAVE_AVX2_FMA_RUN)
        set(HAVE_AVX2 TRUE CACHE INTERNAL "AVX2 support detected")
        set(HAVE_FMA  TRUE CACHE INTERNAL "FMA3 support detected")
        message(STATUS "DetectArch: AVX2 + FMA3 -- yes")
    else()
        set(HAVE_AVX2 FALSE CACHE INTERNAL "")
        set(HAVE_FMA  FALSE CACHE INTERNAL "")
        message(STATUS "DetectArch: AVX2 + FMA3 -- no (host CPU or cross-compile)")
    endif()
endif()

# ---- x86: AVX-512 ----
if(BENCH_ENABLE_AVX512)
    set(CMAKE_REQUIRED_FLAGS "-mavx512f")
    check_cxx_source_compiles("
#include <immintrin.h>
int main() {
    __m512 a = _mm512_setzero_ps();
    (void)a;
    return 0;
}
" HAVE_AVX512)
    unset(CMAKE_REQUIRED_FLAGS)
    if(HAVE_AVX512)
        message(STATUS "DetectArch: AVX-512 -- yes (WARNING: frequency throttling may apply)")
    else()
        message(STATUS "DetectArch: AVX-512 -- no")
    endif()
endif()

# ---- AArch64: NEON ----
if(BENCH_ENABLE_NEON OR BENCH_ENABLE_NEON STREQUAL "AUTO")
    check_cxx_source_compiles("
#include <arm_neon.h>
int main() {
    float32x4_t a = vdupq_n_f32(1.0f);
    float32x4_t b = vfmaq_f32(a, a, a);
    (void)b;
    return 0;
}
" HAVE_NEON)
    if(HAVE_NEON)
        message(STATUS "DetectArch: NEON -- yes")
    else()
        set(HAVE_NEON FALSE CACHE INTERNAL "")
        message(STATUS "DetectArch: NEON -- no")
    endif()
endif()
