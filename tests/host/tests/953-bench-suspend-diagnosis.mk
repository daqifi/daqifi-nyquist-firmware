# #953: SD:BENCHmark's "file never opened" diagnosis. Same file as #943's
# fragment and, for the same reason, the same technique -- no UUT copy, no
# stubs, the decision cascade re-implemented against injected values.
#
# It copies no CONSTANTS, so it needs no equivalent of #943's two greps.
# What it copies is the ORDER of the three arms, which is the entire
# substance of #953: recorded dir-full verdict, then live suspend reason,
# then the card diagnosis. So the guard checks THAT -- the three arms'
# markers must still appear in that order in the real source. It fails
# (loudly) if they are reordered, reworded or moved, each of which
# invalidates the test's premise and should be looked at rather than
# skipped.

TEST_953_BIN := run_953_tests

$(TEST_953_BIN): test_953_bench_suspend_diagnosis.c test_framework.h $(SD_BENCH_SRC) $(RECIPES) $(lastword $(MAKEFILE_LIST))
	@a=`grep -nF 'SD:BENCH refused' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 b=`grep -nF '} else if (why != NULL) {' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 c=`grep -nF 'SD:BENCH - File not ready after timeout' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 test -n "$$a" && test -n "$$b" && test -n "$$c" \
	   && test "$$a" -lt "$$b" && test "$$b" -lt "$$c" \
	   || { echo "ERROR: the SD:BENCHmark file-not-ready cascade in $(SD_BENCH_SRC)"; \
	        echo "       is no longer dirFull -> suspend -> card, or an arm was"; \
	        echo "       reworded, moved or duplicated (markers found at a='$$a'"; \
	        echo "       b='$$b' c='$$c')."; \
	        echo "       test_953_bench_suspend_diagnosis.c models THAT ordering and"; \
	        echo "       its whole point is that the order is the fix -- re-read both"; \
	        echo "       before changing either."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(TEST_953_BIN) test_953_bench_suspend_diagnosis.c

TESTS += $(TEST_953_BIN)
