/* ==========================================================================
 * test_framework.h — tiny dependency-free C unit-test harness
 *
 * No Unity, no CMocka, no downloads. Header-only. Include it in exactly one
 * translation unit (the test .c) that also defines main() and calls the RUN()
 * / TEST_SUMMARY() macros.
 *
 * Usage:
 *   TEST(my_case) { ASSERT_EQ(2 + 2, 4); }
 *   int main(void) { RUN(my_case); return TEST_SUMMARY(); }
 *
 * A test "passes" if none of its assertions failed. main() returns non-zero
 * if any test failed, so `make run` fails the build on a regression.
 * ========================================================================== */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int g_tests_run     = 0;
static int g_tests_failed  = 0;
static int g_asserts       = 0;
static int g_current_failed = 0;

/* Define a test case. Body is a plain function taking no args. */
#define TEST(name) static void name(void)

#define ASSERT_TRUE(cond) do {                                              \
    g_asserts++;                                                            \
    if (!(cond)) {                                                          \
        g_current_failed++;                                                 \
        printf("    ASSERT FAILED [%s:%d]: %s\n",                          \
               __func__, __LINE__, #cond);                                  \
    }                                                                       \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

/* Compares as signed 64-bit; safe for uint32_t values (they widen exactly). */
#define ASSERT_EQ(a, b) do {                                                \
    g_asserts++;                                                            \
    long long _va = (long long)(a);                                         \
    long long _vb = (long long)(b);                                         \
    if (_va != _vb) {                                                       \
        g_current_failed++;                                                 \
        printf("    ASSERT FAILED [%s:%d]: %s == %s  (%lld != %lld)\n",    \
               __func__, __LINE__, #a, #b, _va, _vb);                       \
    }                                                                       \
} while (0)

/* Byte-array equality (memcmp). */
#define ASSERT_BYTES(actual, expected, n) do {                              \
    g_asserts++;                                                            \
    if (memcmp((actual), (expected), (n)) != 0) {                           \
        g_current_failed++;                                                 \
        printf("    ASSERT FAILED [%s:%d]: bytes differ (%s vs %s, %u B)\n",\
               __func__, __LINE__, #actual, #expected, (unsigned)(n));      \
    }                                                                       \
} while (0)

#define RUN(test) do {                                                      \
    g_tests_run++;                                                          \
    g_current_failed = 0;                                                   \
    test();                                                                 \
    if (g_current_failed) {                                                 \
        g_tests_failed++;                                                   \
        printf("[FAIL] %s\n", #test);                                       \
    } else {                                                                \
        printf("[ OK ] %s\n", #test);                                       \
    }                                                                       \
} while (0)

/* Prints a summary and yields the process exit code (0 = all passed). */
static int test_summary(void)
{
    printf("\n---------------------------------------------\n");
    printf("%d tests run, %d failed, %d assertions checked\n",
           g_tests_run, g_tests_failed, g_asserts);
    return (g_tests_failed == 0) ? 0 : 1;
}
#define TEST_SUMMARY() test_summary()

#endif /* TEST_FRAMEWORK_H */
