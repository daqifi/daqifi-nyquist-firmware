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
FW_SVC   := ../../firmware/src/services
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

# #953: SD:BENCHmark's "file never opened" diagnosis. Same file as BENCH_BIN
# and, for the same reason, the same technique -- no UUT copy, no stubs, the
# decision cascade re-implemented against injected values.
#
# It copies no CONSTANTS, so it needs no equivalent of the two greps above.
# What it copies is the ORDER of the three arms, which is the entire substance
# of #953: recorded dir-full verdict, then live suspend reason, then the card
# diagnosis. So the guard checks THAT -- the three arms' markers must still
# appear in that order in the real source. It fails (loudly) if they are
# reordered, reworded or moved, each of which invalidates the test's premise
# and should be looked at rather than skipped.
SUSPEND_BIN := run_953_tests

# #981: SYSTem:STORage:SD:FAILNext's one-shot arm/consume contract.
# `sd_card_manager.c` is not includable on the host (FreeRTOS + Harmony's
# SYS_FS + the whole SD state machine), so this re-implements only the
# one-shot flag/consume shape against a mock, same technique as BENCH_BIN and
# SUSPEND_BIN above. Four properties of the real source make the model valid;
# the guard greps for all four and refuses to build if any has moved:
#   1. the flag `gFailNextWrite` still exists;
#   2. its consume site is a test-and-clear (`if (gFailNextWrite) { ...
#      gFailNextWrite = false ...`) INSIDE one taskENTER_CRITICAL /
#      taskEXIT_CRITICAL pair, not a bare clear outside one;
#   3. `sd_card_manager_Init()` still scrubs it to false (the #409 rail: a
#      reset must always clear an outstanding arm);
#   4. `sd_card_manager_SetFailNextWrite()`'s body is still a PLAIN store
#      with no critical section of its own (the atomicity argument for #981:
#      a store that reads nothing first is not an RMW).
FAILNEXT_BIN := run_981_tests
SD_MANAGER_SRC := ../../firmware/src/services/sd_card_services/sd_card_manager.c

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

# Every binary depends on this file as well as on its sources. Two of the
# targets below carry GUARDS in their recipes -- greps that fail the build when
# the firmware text they model has moved -- and a guard that is only re-run when
# a source changes is not re-run when the GUARD itself changes. CI is unaffected
# either way (Actions checks out clean, so `make run` always builds from
# nothing); this is for the local incremental build, where the binary outliving
# an edited recipe is exactly how a guard goes quiet. Qodo, PR #983.
RECIPES := Makefile

$(BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h $(RECIPES)
	cp $(FW_UTIL)/CircularBuffer.c $(UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BIN) test_circularbuffer.c $(UUT)

$(FMT_BIN): test_fixedpointfmt.c $(FW_UTIL)/FixedPointFmt.h $(RECIPES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(FMT_BIN) test_fixedpointfmt.c -lm

$(AD7609_BIN): test_ad7609_scale.c test_framework.h $(FW_ADC)/AD7609Scale.h $(RECIPES)
	$(CC) $(CFLAGS) -I$(FW_ADC) -o $(AD7609_BIN) test_ad7609_scale.c

$(JSONESC_BIN): test_json_string_escape.c test_framework.h $(FW_SVC)/JSON_StringEscape.h $(RECIPES)
	$(CC) $(CFLAGS) -I$(FW_SVC) -o $(JSONESC_BIN) test_json_string_escape.c

$(BENCH_BIN): test_943_bench_stall_bound.c test_framework.h $(SD_BENCH_SRC) $(RECIPES)
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_TIMEOUT_MS[[:space:]]+10000U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_TIMEOUT_MS is no longer '10000U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_TIMEOUT_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_POLL_MS[[:space:]]+5U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_POLL_MS is no longer '5U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_POLL_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(BENCH_BIN) test_943_bench_stall_bound.c

$(SUSPEND_BIN): test_953_bench_suspend_diagnosis.c test_framework.h $(SD_BENCH_SRC) $(RECIPES)
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
	$(CC) $(CFLAGS) -o $(SUSPEND_BIN) test_953_bench_suspend_diagnosis.c

$(FAILNEXT_BIN): test_981_sd_failnext_hook.c test_framework.h $(SD_MANAGER_SRC) $(RECIPES)
	@grep -qF 'static volatile bool gFailNextWrite = false;' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: gFailNextWrite is no longer declared in $(SD_MANAGER_SRC)."; \
	       echo "       test_981_sd_failnext_hook.c models its one-shot semantics; re-read both."; \
	       exit 1; }
	@sed -n '/bool injectWriteFailure = false;/,/if (injectWriteFailure) {/p' $(SD_MANAGER_SRC) > /tmp/.fw981_block.$$$$; \
	 ok=1; \
	 grep -qF 'taskENTER_CRITICAL();' /tmp/.fw981_block.$$$$ || ok=0; \
	 grep -qF 'if (gFailNextWrite) {' /tmp/.fw981_block.$$$$ || ok=0; \
	 grep -qF 'gFailNextWrite = false;' /tmp/.fw981_block.$$$$ || ok=0; \
	 grep -qF 'taskEXIT_CRITICAL();' /tmp/.fw981_block.$$$$ || ok=0; \
	 rm -f /tmp/.fw981_block.$$$$; \
	 test "$$ok" = 1 \
	  || { echo "ERROR: the FAILNext consume site in $(SD_MANAGER_SRC) is no"; \
	       echo "       longer a taskENTER_CRITICAL / if(gFailNextWrite){clear} /"; \
	       echo "       taskEXIT_CRITICAL test-and-clear, or its shape moved."; \
	       echo "       test_981_sd_failnext_hook.c models exactly this; re-read both."; \
	       exit 1; }
	@a=`grep -nF 'gFailNextWrite = false;' $(SD_MANAGER_SRC) | tail -1 | cut -d: -f1`; \
	 b=`grep -nF 'static bool isInitDone = false;' $(SD_MANAGER_SRC) | cut -d: -f1`; \
	 test -n "$$a" && test -n "$$b" && test "$$a" -lt "$$b" && test $$(($$b - $$a)) -lt 10 \
	  || { echo "ERROR: the #409 reset-scrub of gFailNextWrite in"; \
	       echo "       sd_card_manager_Init() is no longer immediately before the"; \
	       echo "       isInitDone guard in $(SD_MANAGER_SRC) (a='$$a' b='$$b') --"; \
	       echo "       a reboot may no longer clear an outstanding arm."; \
	       exit 1; }
	@sed -n '/void sd_card_manager_SetFailNextWrite(bool arm) {/,/bool sd_card_manager_FailNextWriteArmed(void) {/p' $(SD_MANAGER_SRC) > /tmp/.fw981_setter.$$$$; \
	 ok=1; \
	 grep -qF 'gFailNextWrite = arm;' /tmp/.fw981_setter.$$$$ || ok=0; \
	 grep -qF 'taskENTER_CRITICAL();' /tmp/.fw981_setter.$$$$ && ok=0; \
	 rm -f /tmp/.fw981_setter.$$$$; \
	 test "$$ok" = 1 \
	  || { echo "ERROR: sd_card_manager_SetFailNextWrite() in $(SD_MANAGER_SRC) is"; \
	       echo "       no longer a plain store (either the store moved, or a critical"; \
	       echo "       section was added) -- the #981 atomicity argument depends on"; \
	       echo "       this being a plain store; re-verify it before changing either."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(FAILNEXT_BIN) test_981_sd_failnext_hook.c

# Every suite, and every one of the seven binaries. Merge conflict (fw#961
# JSONESC_BIN vs fw#983 SUSPEND_BIN, both merging around the same time)
# resolved by keeping each side's target intact and merging only `run` and
# `clean` -- a duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(FAILNEXT_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SUSPEND_BIN)
	./$(FAILNEXT_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(FAILNEXT_BIN)

.PHONY: run clean
