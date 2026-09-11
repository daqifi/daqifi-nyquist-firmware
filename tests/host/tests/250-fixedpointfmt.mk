# #250: FixedPointFmt.h is header-only and dependency-free, so it needs no
# UUT copy and no stubs -- the test includes the real header directly. -lm
# is for round/fabs/isfinite/signbit.

TEST_250_BIN := run_fmt_tests

$(TEST_250_BIN): test_fixedpointfmt.c $(FW_UTIL)/FixedPointFmt.h $(RECIPES) $(lastword $(MAKEFILE_LIST))
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_250_BIN) test_fixedpointfmt.c -lm

TESTS += $(TEST_250_BIN)
