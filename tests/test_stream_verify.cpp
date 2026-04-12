// tests/test_stream_verify.cpp
// Correctness tests for the STREAM bandwidth kernels.
// Compares vectorised and AVX2-NT variants against scalar reference
// implementations to catch compiler optimisation errors or wrong strides.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef BENCH_HAS_AVX2
#include <immintrin.h>
#endif

// ---- Minimal test framework (no external dependencies) ----

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg) do { \
    ++g_tests_run; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [line %d]: %s\n", __LINE__, (msg)); \
        ++g_tests_failed; \
    } else { \
        printf("PASS: %s\n", (msg)); \
    } \
} while(0)

// ---- Reference (unambiguously scalar) implementations ----

static void ref_copy(float* c, const float* a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i];
}
static void ref_scale(float* b, const float* a, float s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b[i] = s * a[i];
}
static void ref_add(float* c, const float* a, const float* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}
static void ref_triad(float* c, const float* a, const float* b,
                       float s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + s * b[i];
}

// ---- Compiler-vectorised implementations (mirror of stream.cpp) ----

static void vec_copy(float* __restrict__ c, const float* __restrict__ a,
                     std::size_t n) {
#pragma GCC ivdep
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i];
}
static void vec_scale(float* __restrict__ b, const float* __restrict__ a,
                      float s, std::size_t n) {
#pragma GCC ivdep
    for (std::size_t i = 0; i < n; ++i) b[i] = s * a[i];
}
static void vec_add(float* __restrict__ c, const float* __restrict__ a,
                    const float* __restrict__ b, std::size_t n) {
#pragma GCC ivdep
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
}
static void vec_triad(float* __restrict__ c, const float* __restrict__ a,
                      const float* __restrict__ b, float s, std::size_t n) {
#pragma GCC ivdep
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] + s * b[i];
}

// ---- AVX2 NT variants (mirror of stream.cpp) ----

#ifdef BENCH_HAS_AVX2
static void avx2_nt_copy(float* __restrict__ c, const float* __restrict__ a,
                          std::size_t n) {
    const std::size_t vn = (n / 8) * 8;
    for (std::size_t i = 0; i < vn; i += 8)
        _mm256_stream_ps(c + i, _mm256_load_ps(a + i));
    for (std::size_t i = vn; i < n; ++i) c[i] = a[i];
    _mm_sfence();
}
static void avx2_nt_scale(float* __restrict__ b, const float* __restrict__ a,
                           float s, std::size_t n) {
    const __m256 vs = _mm256_set1_ps(s);
    const std::size_t vn = (n / 8) * 8;
    for (std::size_t i = 0; i < vn; i += 8)
        _mm256_stream_ps(b + i, _mm256_mul_ps(vs, _mm256_load_ps(a + i)));
    for (std::size_t i = vn; i < n; ++i) b[i] = s * a[i];
    _mm_sfence();
}
static void avx2_nt_add(float* __restrict__ c, const float* __restrict__ a,
                         const float* __restrict__ b, std::size_t n) {
    const std::size_t vn = (n / 8) * 8;
    for (std::size_t i = 0; i < vn; i += 8)
        _mm256_stream_ps(c + i,
            _mm256_add_ps(_mm256_load_ps(a + i), _mm256_load_ps(b + i)));
    for (std::size_t i = vn; i < n; ++i) c[i] = a[i] + b[i];
    _mm_sfence();
}
static void avx2_nt_triad(float* __restrict__ c, const float* __restrict__ a,
                           const float* __restrict__ b, float s, std::size_t n) {
    const __m256 vs = _mm256_set1_ps(s);
    const std::size_t vn = (n / 8) * 8;
    for (std::size_t i = 0; i < vn; i += 8)
        _mm256_stream_ps(c + i,
            _mm256_fmadd_ps(vs, _mm256_load_ps(b + i), _mm256_load_ps(a + i)));
    for (std::size_t i = vn; i < n; ++i) c[i] = a[i] + s * b[i];
    _mm_sfence();
}
#endif // BENCH_HAS_AVX2

// ---- Test buffer management ----

struct Bufs {
    float* a   = nullptr;
    float* b   = nullptr;
    float* c   = nullptr;
    float* ref = nullptr;
    std::size_t n = 0;

    explicit Bufs(std::size_t n_elems) : n(n_elems) {
        if (posix_memalign(reinterpret_cast<void**>(&a),   64, n * sizeof(float)) ||
            posix_memalign(reinterpret_cast<void**>(&b),   64, n * sizeof(float)) ||
            posix_memalign(reinterpret_cast<void**>(&c),   64, n * sizeof(float)) ||
            posix_memalign(reinterpret_cast<void**>(&ref), 64, n * sizeof(float))) {
            fprintf(stderr, "FATAL: posix_memalign failed (n=%zu)\n", n_elems);
            free(a); free(b); free(c); free(ref);
            a = b = c = ref = nullptr;
            return;
        }
        for (std::size_t i = 0; i < n; ++i) {
            a[i]   = static_cast<float>(i) * 0.5f + 1.0f;
            b[i]   = static_cast<float>(i) * 0.3f + 0.5f;
            c[i]   = 0.0f;
            ref[i] = 0.0f;
        }
    }

    bool ok() const { return a && b && c && ref; }
    ~Bufs() { free(a); free(b); free(c); free(ref); }
};

static bool arrays_equal(const float* x, const float* y, std::size_t n,
                          float tol = 1e-5f) {
    for (std::size_t i = 0; i < n; ++i) {
        if (fabsf(x[i] - y[i]) > tol * (fabsf(x[i]) + 1.0f)) {
            fprintf(stderr, "  mismatch at i=%zu: got %.8f  expected %.8f\n",
                    i, static_cast<double>(x[i]), static_cast<double>(y[i]));
            return false;
        }
    }
    return true;
}

// ---- Vectorised variant tests (all 4 kernels) ----

static void test_vec_copy() {
    Bufs b(4096); if (!b.ok()) return;
    ref_copy(b.ref, b.a, b.n);
    vec_copy(b.c,   b.a, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n), "vec_copy matches reference");
}
static void test_vec_scale() {
    Bufs b(4096); if (!b.ok()) return;
    ref_scale(b.ref, b.a, 3.14f, b.n);
    vec_scale(b.c,   b.a, 3.14f, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n), "vec_scale matches reference");
}
static void test_vec_add() {
    Bufs b(4096); if (!b.ok()) return;
    ref_add(b.ref, b.a, b.b, b.n);
    vec_add(b.c,   b.a, b.b, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n), "vec_add matches reference");
}
static void test_vec_triad() {
    Bufs b(4096); if (!b.ok()) return;
    ref_triad(b.ref, b.a, b.b, 2.5f, b.n);
    vec_triad(b.c,   b.a, b.b, 2.5f, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n), "vec_triad matches reference");
}

// ---- AVX2 NT variant tests (all 4 kernels) ----

#ifdef BENCH_HAS_AVX2
static void test_avx2_nt_copy() {
    Bufs b(4096); if (!b.ok()) return;
    ref_copy(b.ref, b.a, b.n);
    avx2_nt_copy(b.c, b.a, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n, 1e-6f), "avx2_nt_copy matches reference");
}
static void test_avx2_nt_scale() {
    Bufs b(4096); if (!b.ok()) return;
    ref_scale(b.ref, b.a, 1.5f, b.n);
    avx2_nt_scale(b.c, b.a, 1.5f, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n, 1e-6f), "avx2_nt_scale matches reference");
}
static void test_avx2_nt_add() {
    Bufs b(4096); if (!b.ok()) return;
    ref_add(b.ref, b.a, b.b, b.n);
    avx2_nt_add(b.c, b.a, b.b, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n, 1e-6f), "avx2_nt_add matches reference");
}
static void test_avx2_nt_triad() {
    // FMA vs separate mul+add may differ in the last ULP; slightly wider tol.
    Bufs b(4096); if (!b.ok()) return;
    ref_triad(b.ref, b.a, b.b, 1.7f, b.n);
    avx2_nt_triad(b.c, b.a, b.b, 1.7f, b.n);
    CHECK(arrays_equal(b.c, b.ref, b.n, 2e-5f), "avx2_nt_triad matches reference");
}
#endif // BENCH_HAS_AVX2

// ---- Edge case: zero-length inputs must not crash or write output ----

static void test_zero_length() {
    float x = 0.0f, y = 1.0f, z = 2.0f;
    vec_copy(&z,  &x,          0); CHECK(z == 2.0f, "vec_copy  n=0 no-op");
    vec_scale(&z, &x, 3.0f,   0); CHECK(z == 2.0f, "vec_scale n=0 no-op");
    vec_add(&z,   &x, &y,     0); CHECK(z == 2.0f, "vec_add   n=0 no-op");
    vec_triad(&z, &x, &y, 3.0f, 0); CHECK(z == 2.0f, "vec_triad n=0 no-op");
}

// ---- Large buffer: verify at L3/DRAM working-set sizes ----

static void test_large_buffer() {
    // 16 MiB per array -- stresses L3 without exhausting RAM in CI
    const std::size_t n = 4 * 1024 * 1024;
    Bufs b(n); if (!b.ok()) return;
    ref_triad(b.ref, b.a, b.b, 1.234f, n);
    vec_triad(b.c,   b.a, b.b, 1.234f, n);
    CHECK(arrays_equal(b.c, b.ref, n), "vec_triad large buffer matches reference");
}

// ---- Entry point ----

int main() {
    printf("=== STREAM kernel correctness tests ===\n\n");

    test_vec_copy();
    test_vec_scale();
    test_vec_add();
    test_vec_triad();

#ifdef BENCH_HAS_AVX2
    test_avx2_nt_copy();
    test_avx2_nt_scale();
    test_avx2_nt_add();
    test_avx2_nt_triad();
#endif

    test_zero_length();
    test_large_buffer();

    printf("\n%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
