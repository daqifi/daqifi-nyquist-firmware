# #943: SD:BENCHmark's per-chunk stall bound. Like the #250/#889 fragments
# this needs no UUT copy and no stubs -- but for a different reason.
# SCPIStorageSD.c is NOT includable on the host (libscpi + FreeRTOS + the
# whole SD manager), so the test re-implements the two loop shapes against a
# mock clock and compares their verdicts.
#
# That makes the two timeout constants a COPY, which can drift. The recipe
# therefore greps them out of the real source first and refuses to build if
# either has changed, so a stale copy can't pass silently. The greps also
# fail (loudly, same message) if the loop is moved to another file -- which
# likewise invalidates the test's premise and should be looked at rather
# than skipped.

TEST_943_BIN := run_943_tests

$(TEST_943_BIN): test_943_bench_stall_bound.c test_framework.h $(SD_BENCH_SRC) $(RECIPES) $(lastword $(MAKEFILE_LIST))
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_TIMEOUT_MS[[:space:]]+10000U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_TIMEOUT_MS is no longer '10000U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_TIMEOUT_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_POLL_MS[[:space:]]+5U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_POLL_MS is no longer '5U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_POLL_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(TEST_943_BIN) test_943_bench_stall_bound.c

TESTS += $(TEST_943_BIN)
