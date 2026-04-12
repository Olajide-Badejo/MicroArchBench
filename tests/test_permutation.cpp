// tests/test_permutation.cpp
// Unit tests for the pointer-chase list builder.
// Verifies that:
//   1. Every node is visited exactly once per traversal.
//   2. The list is circular (last node points back to the first visited).
//   3. The traversal order is not sequential (randomness sanity check).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>
#include <algorithm>

// ---- Replicate the Node definition and builder from pointer_chase.cpp ----
// (Keeping the test self-contained avoids linking the benchmark harness.)

struct alignas(64) Node {
    Node* next;
    char  pad[56];
};

static Node* build_chase_list(std::size_t n_nodes, uint64_t seed = 0xDEADBEEF42ULL) {
    auto* nodes = static_cast<Node*>(
        aligned_alloc(64, n_nodes * sizeof(Node)));
    if (!nodes) return nullptr;
    memset(nodes, 0, n_nodes * sizeof(Node));

    try {
        std::vector<std::size_t> perm(n_nodes);
        std::iota(perm.begin(), perm.end(), 0);
        std::mt19937_64 rng(seed);
        std::shuffle(perm.begin(), perm.end(), rng);

        for (std::size_t i = 0; i < n_nodes - 1; ++i)
            nodes[perm[i]].next = &nodes[perm[i + 1]];
        nodes[perm[n_nodes - 1]].next = &nodes[perm[0]];
    } catch (...) {
        free(nodes);
        return nullptr;
    }

    return nodes;
}

// ---------------------------------------------------------------------------
// Simple test framework -- no external dependencies
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Test 1: every node visited exactly once for a small list
// ---------------------------------------------------------------------------

static void test_visit_count() {
    constexpr std::size_t N = 64;
    Node* head = build_chase_list(N);
    if (!head) { fprintf(stderr, "allocation failed\n"); exit(1); }

    std::vector<int> visit_count(N, 0);
    Node* p = head;
    for (std::size_t i = 0; i < N; ++i) {
        std::size_t idx = static_cast<std::size_t>(p - head);
        ++visit_count[idx];
        p = p->next;
    }

    bool all_one = true;
    for (int c : visit_count)
        if (c != 1) { all_one = false; break; }

    CHECK(all_one, "every node visited exactly once (N=64)");

    // After N steps we should be back at head (circular)
    CHECK(p == head, "traversal is circular (N=64)");

    free(head);
}

// ---------------------------------------------------------------------------
// Test 2: visit-count check for a larger list (1024 nodes)
// ---------------------------------------------------------------------------

static void test_visit_count_large() {
    constexpr std::size_t N = 1024;
    Node* head = build_chase_list(N);
    if (!head) { fprintf(stderr, "allocation failed\n"); exit(1); }

    std::vector<int> visit_count(N, 0);
    Node* p = head;
    for (std::size_t i = 0; i < N; ++i) {
        std::size_t idx = static_cast<std::size_t>(p - head);
        if (idx >= N) { CHECK(false, "index out of range (N=1024)"); free(head); return; }
        ++visit_count[idx];
        p = p->next;
    }

    bool all_one = true;
    for (int c : visit_count)
        if (c != 1) { all_one = false; break; }

    CHECK(all_one, "every node visited exactly once (N=1024)");
    CHECK(p == head, "traversal is circular (N=1024)");

    free(head);
}

// ---------------------------------------------------------------------------
// Test 3: order is not sequential (basic randomness check)
// If the first 8 steps are strictly sequential, the permutation is broken.
// ---------------------------------------------------------------------------

static void test_not_sequential() {
    constexpr std::size_t N = 256;
    Node* head = build_chase_list(N);
    if (!head) { fprintf(stderr, "allocation failed\n"); exit(1); }

    bool sequential = true;
    Node* p = head;
    for (std::size_t i = 0; i < 8 && i < N - 1; ++i) {
        if (p->next != p + 1) { sequential = false; break; }
        p = p->next;
    }

    CHECK(!sequential, "traversal order is not strictly sequential (randomness check)");

    free(head);
}

// ---------------------------------------------------------------------------
// Test 4: different seeds produce different permutations
// ---------------------------------------------------------------------------

static void test_seed_independence() {
    constexpr std::size_t N = 64;
    Node* a = build_chase_list(N, 111);
    if (!a) { fprintf(stderr, "allocation failed (a)\n"); exit(1); }
    Node* b = build_chase_list(N, 222);
    if (!b) { free(a); fprintf(stderr, "allocation failed (b)\n"); exit(1); }

    bool any_different = false;
    Node* pa = a;
    Node* pb = b;
    for (std::size_t i = 0; i < N; ++i) {
        if ((pa->next - a) != (pb->next - b)) { any_different = true; break; }
        pa = pa->next;
        pb = pb->next;
    }

    CHECK(any_different, "different seeds produce different permutations");

    free(a);
    free(b);
}

int main() {
    test_visit_count();
    test_visit_count_large();
    test_not_sequential();
    test_seed_independence();

    printf("\n%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
