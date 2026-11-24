/* test_1187_sd_leaf_extension.c -- issue #1187
 *
 * extractBaseFilename() and generateFilename() (sd_card_manager.c) split a
 * log filename into base + extension. They used the last '.' of the WHOLE
 * string, so "sub.dir/exp" became base "sub", ext ".dir/exp" and rotation 1
 * targeted "D/sub-1.dir/exp" -- a directory that does not exist, so logging
 * stopped. The extension must come from the LEAF only.
 *
 * The three functions are SPLICED out of the real source by the Makefile
 * (gen_1187_leaf.h); this file only supplies the prelude. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

#define LOG_E(...) ((void)0)
static struct { char baseFilename[65]; } gSDCardData;

#include "gen_1187_leaf.h"

static void rotate(const char* name, uint32_t n, char* out, size_t len) {
    extractBaseFilename(name);
    generateFilename(out, len, n, "D", gSDCardData.baseFilename, name);
}

TEST(plain_leaf_unchanged) {
    char p[96];
    rotate("exp.csv", 1, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/exp-1.csv") == 0);
    rotate("exp.csv", 0, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/exp.csv") == 0);
}

TEST(no_extension_unchanged) {
    char p[96];
    rotate("exp", 2, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/exp-2") == 0);
}

TEST(dotted_subdir_with_extension) {
    char p[96];
    rotate("sub.dir/exp.csv", 1, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/sub.dir/exp-1.csv") == 0);
}

TEST(dotted_subdir_without_extension) {
    char p[96];
    rotate("sub.dir/exp", 1, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/sub.dir/exp-1") == 0);
    ASSERT_TRUE(strcmp(gSDCardData.baseFilename, "sub.dir/exp") == 0);
}

TEST(multi_dot_leaf_takes_last) {
    char p[96];
    rotate("a.b/c.d.csv", 3, p, sizeof p);
    ASSERT_TRUE(strcmp(p, "D/a.b/c.d-3.csv") == 0);
}

int main(void) {
    RUN(plain_leaf_unchanged);
    RUN(no_extension_unchanged);
    RUN(dotted_subdir_with_extension);
    RUN(dotted_subdir_without_extension);
    RUN(multi_dot_leaf_takes_last);
    return TEST_SUMMARY();
}
