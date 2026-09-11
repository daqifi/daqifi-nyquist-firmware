# #250: FixedPointFmt.h is header-only and dependency-free, so it needs no
# UUT copy and no stubs -- the test includes the real header directly. -lm
# is for round/fabs/isfinite/signbit.

TEST_250_BIN := run_fmt_tests

$(TEST_250_BIN): test_fixedpointfmt.c $(FW_UTIL)/FixedPointFmt.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_250_BIN) test_fixedpointfmt.c -lm

TESTS += $(TEST_250_BIN)
# Identity, not just a count: the guard in ../Makefile compares the SET of
# claimed sources against the set on disk, so a fragment that is deleted,
# duplicated, or pointed at the wrong source is caught instead of balancing
# out. Keep this naming the source THIS fragment compiles.
TEST_SOURCES_CLAIMED += test_fixedpointfmt.c
