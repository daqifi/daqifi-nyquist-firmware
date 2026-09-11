# Host unit tests for firmware/src/Util/CircularBuffer.c (issue #124)
#
# Plain gcc, no external deps. `make run` builds and runs the suite; a
# non-zero exit (any failing test) fails the build.
#
# CircularBuffer.c uses quoted includes ("Logger.h", "osal/osal.h") that GCC
# resolves relative to the *source file's own directory* first. To let our
# stubs win, we compile a build-time copy of the source placed in this
# directory (CircularBuffer_uut.c) — its quoted includes then fall through to
# -Istubs (Logger + osal stubs) and -I$(FW_UTIL) (the real CircularBuffer.h).
# The copy is regenerated on every build, so it always tracks the real source.

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O0 -g
FW_UTIL  := ../../firmware/src/Util
FW_ADC   := ../../firmware/src/HAL/ADC
INCLUDES := -Istubs -I$(FW_UTIL)

BIN := run_tests
UUT := CircularBuffer_uut.c

# #250: FixedPointFmt.h is header-only and dependency-free, so it needs no UUT
# copy and no stubs -- the test includes the real header directly. -lm is for
# round/fabs/isfinite/signbit.
FMT_BIN := run_fmt_tests

# #889: AD7609Scale.h is header-only and dependency-free for exactly the same
# reason -- the pure code->volts arithmetic was split out of AD7609.h, which
# is unusable on a host (it pulls in Harmony's configuration.h/definitions.h
# via state/board/AInConfig.h). So: no UUT copy, no stubs, and NOT $(INCLUDES)
# -- this test needs no stub on the include path at all, which is the property
# that makes it worth having. test_framework.h resolves relative to the test
# source's own directory. No -lm: the header calls no math functions.
AD7609_BIN := run_ad7609_tests

# #943: SD:BENCHmark's per-chunk stall bound. Like FMT_BIN this needs no UUT
# copy and no stubs -- but for a different reason. FixedPointFmt.h is a real
# firmware header included directly; SCPIStorageSD.c is NOT includable on the
# host (libscpi + FreeRTOS + the whole SD manager), so the test re-implements
# the two loop shapes against a mock clock and compares their verdicts.
#
# That makes the two timeout constants a COPY, which can drift. The recipe
# therefore greps them out of the real source first and refuses to build if
# either has changed, so a stale copy can't pass silently. The greps also fail
# (loudly, same message) if the loop is moved to another file -- which likewise
# invalidates the test's premise and should be looked at rather than skipped.
BENCH_BIN := run_943_tests
SD_BENCH_SRC := ../../firmware/src/services/SCPI/SCPIStorageSD.c

# #971: the under-claim settle order of a REFUSED SD arm. Same file, same
# reason as #943 -- SCPIStorageSD.c is not host-includable -- so this is also
# a model rather than the real code, and it also needs a way to fail loudly
# when what it models moves underneath it.
#
# The guard here is a CONTENT HASH of the modelled function, not a grep for
# the property. That is deliberate. PR #976 spent three review rounds finding
# fifteen ways an honest refactor slips past a textual assertion about this
# exact function, and `tools/lint/scpi_sd_arm_path.py` is being narrowed for
# over-claiming on the strength of it; a second, cruder copy of that
# assertion here would give false comfort. A hash claims only "this text is
# unchanged", which is all a build recipe can honestly know, and no refactor
# can slip past it. When it fires, re-read ~8 lines of C against the model in
# test_971_sd_arm_refusal_order.c and update the hash -- it is a tripwire
# asking for a review, not a verdict on whether the code is right.
#
# Hashed: the function from its `static bool <name>(` line to its closing
# brace, with WHOLE-LINE comments dropped and whitespace collapsed. So
# rewording the (long, load-bearing) comment block above it does not fire,
# and any change to the code does. A trailing comment on a line of code is
# hashed with it -- conservative in the safe direction.
ARM_BIN := run_971_tests
SD_ARM_HELPER := SD_ArmOrRefuseWithCleanup
SD_ARM_HELPER_SHA := 5fc520aee8b158e4742efde6e8b7632b4d300dc48799a90e21b964f27456d5e0

$(BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h
	cp $(FW_UTIL)/CircularBuffer.c $(UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BIN) test_circularbuffer.c $(UUT)

$(FMT_BIN): test_fixedpointfmt.c $(FW_UTIL)/FixedPointFmt.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(FMT_BIN) test_fixedpointfmt.c -lm

$(AD7609_BIN): test_ad7609_scale.c test_framework.h $(FW_ADC)/AD7609Scale.h
	$(CC) $(CFLAGS) -I$(FW_ADC) -o $(AD7609_BIN) test_ad7609_scale.c

$(BENCH_BIN): test_943_bench_stall_bound.c test_framework.h $(SD_BENCH_SRC)
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_TIMEOUT_MS[[:space:]]+10000U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_TIMEOUT_MS is no longer '10000U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_TIMEOUT_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_POLL_MS[[:space:]]+5U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_POLL_MS is no longer '5U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_POLL_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(BENCH_BIN) test_943_bench_stall_bound.c

# Two separate failures with two separate messages: the function is GONE
# (renamed/moved -- the model has lost its subject) vs the function is there
# and its CODE CHANGED (the model may have gone stale). Collapsing them would
# report "hash mismatch" for a rename, sending the reader to diff a function
# that is no longer in the file.
$(ARM_BIN): test_971_sd_arm_refusal_order.c test_framework.h $(SD_BENCH_SRC)
	@grep -q '^static bool $(SD_ARM_HELPER)(' $(SD_BENCH_SRC) \
	  || { echo "ERROR: no '^static bool $(SD_ARM_HELPER)(' line in $(SD_BENCH_SRC)."; \
	       echo "       test_971_sd_arm_refusal_order.c models that function, and the"; \
	       echo "       hash below extracts it from that line to its closing brace. If"; \
	       echo "       it was renamed, moved, or its signature reformatted, re-read it"; \
	       echo "       against the model and update SD_ARM_HELPER / SD_ARM_HELPER_SHA."; \
	       exit 1; }
	@command -v sha256sum >/dev/null 2>&1 \
	  || { echo "ERROR: sha256sum not found -- it is what pins $(SD_ARM_HELPER)()'s"; \
	       echo "       code for test_971_sd_arm_refusal_order.c. Install GNU coreutils."; \
	       echo "       (Reported separately so it is not read as a drift failure.)"; \
	       exit 1; }
	@got=$$(sed -n '/^static bool $(SD_ARM_HELPER)(/,/^}/p' $(SD_BENCH_SRC) \
	        | sed -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\//d' \
	        | tr -s '[:space:]' ' ' | sha256sum | cut -d' ' -f1); \
	 [ "$$got" = "$(SD_ARM_HELPER_SHA)" ] \
	  || { echo "ERROR: the CODE of $(SD_ARM_HELPER)() in $(SD_BENCH_SRC) has changed."; \
	       echo "         expected sha256 $(SD_ARM_HELPER_SHA)"; \
	       echo "         got      sha256 $$got"; \
	       echo "       This is a tripwire, not a verdict: the change may be perfectly"; \
	       echo "       correct. Re-read that function against the model in"; \
	       echo "       test_971_sd_arm_refusal_order.c -- if the model still describes"; \
	       echo "       it, update SD_ARM_HELPER_SHA; if it does not, fix the model."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(ARM_BIN) test_971_sd_arm_refusal_order.c

# BOTH suites, and every one of the five binaries. Merge conflict resolved by
# keeping each side's target intact and merging only `run` and `clean` -- a
# duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(ARM_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(ARM_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(ARM_BIN)

.PHONY: run clean
