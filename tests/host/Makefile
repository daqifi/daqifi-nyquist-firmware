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

# #958: SD:BENCHmark's scratch FILENAME. Third target on the same
# non-host-compilable file as BENCH_BIN and SUSPEND_BIN, same technique for the
# same reason: the name generation is re-implemented against injected tick
# values rather than included.
#
# It copies BOTH a format string and a length constant, and it also depends on
# two DEFECTS being absent, so the recipe carries seven guards. Three are about
# the text being right: the post-fix format expression must be present (checked
# with newlines squashed, because it wraps), the field length must still be 40,
# and the masked 16-bit form -- the original #958 defect -- must NOT be back.
#
# The other three are about the SEQUENCE field, added after Qodo found that the
# whole 32-bit tick still repeats after 49.7 days of uptime. A format pin alone
# would prove the field is in the name and nothing more, so the recipe also
# requires that the counter is ADVANCED once per run, that the advance happens
# BEFORE the format (the pre-merge audit reproduced a swap of those two steps
# passing every other guard and the whole host suite, while every name carried
# the counter's initial value), that it is NEVER assigned anywhere but its
# definition (a reset reopens the collision), and that the tick-only form has
# not come back. Each fails loudly and names what to re-read.
NAME_BIN := run_958_tests
SD_MGR_HDR := ../../firmware/src/services/sd_card_services/sd_card_manager.h

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

# #1004: SCPI_Help's shared-buffer write-abort bound. Same shape as
# SUSPEND_BIN's sibling SD_BENCH_SRC-style guard and #995's own host test --
# SCPIInterface.c is not includable on the host (libscpi + FreeRTOS + the
# whole board/driver graph), so this re-implements the pre-fix and post-fix
# write shapes against a mock clock and compares their verdicts. The three
# firmware constants it depends on are a COPY; the recipe greps them (plus
# the tick-rate/width assumption, same as #943/#995's grep) and refuses to
# build on drift.
HELP_BIN := run_1004_tests
SCPI_IFACE_SRC := ../../firmware/src/services/SCPI/SCPIInterface.c
FW_RTOS_CFG := ../../firmware/src/config/default/FreeRTOSConfig.h

# Every binary depends on this file as well as on its sources. Four of the
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

$(NAME_BIN): test_958_sd_benchmark_filename_collision.c test_framework.h $(SD_BENCH_SRC) $(SD_MGR_HDR) $(RECIPES)
	@tr -d '\r' < $(SD_BENCH_SRC) | tr '\n' ' ' \
	  | grep -qE '"benchmark_%lu_%lu\.dat",[[:space:]]*\(unsigned long\)xTaskGetTickCount\(\),[[:space:]]*\(unsigned long\)benchNameSeq' \
	  || { echo "ERROR: the SD:BENCHmark scratch name in $(SD_BENCH_SRC) is no"; \
	       echo "       longer built from the tick AND the per-boot sequence as"; \
	       echo "       '\"benchmark_%lu_%lu.dat\", tick, benchNameSeq'."; \
	       echo "       test_958_sd_benchmark_filename_collision.c re-implements THAT"; \
	       echo "       format and asserts the names it produces -- update"; \
	       echo "       new_bench_name() and re-read the header's uniqueness argument"; \
	       echo "       before changing either. Newlines are squashed first, so the"; \
	       echo "       expression may wrap however it likes -- only the text matters."; \
	       exit 1; }
	@grep -qE 'benchNameSeq[[:space:]]*=[[:space:]]*\+\+gBenchNameSeq[[:space:]]*;' $(SD_BENCH_SRC) \
	  || { echo "ERROR: $(SD_BENCH_SRC) no longer advances gBenchNameSeq per run."; \
	       echo "       The format pin above only proves the FIELD is in the name;"; \
	       echo "       this proves the field changes. A sequence left at its initial"; \
	       echo "       value prints the same digits every run, which is the 65.5 s"; \
	       echo "       collision again with a longer name."; \
	       exit 1; }
	@if grep -vE '^static uint32_t gBenchNameSeq' $(SD_BENCH_SRC) \
	     | grep -qE 'gBenchNameSeq[[:space:]]*=[^=]'; then \
	       echo "ERROR: gBenchNameSeq is assigned somewhere other than its"; \
	       echo "       definition in $(SD_BENCH_SRC). Its whole contract is that it"; \
	       echo "       is NEVER reset while the board is up -- that is what makes the"; \
	       echo "       scratch name unique within a boot (test_958 CASE 5). A reset,"; \
	       echo "       anywhere, reopens the collision it closes."; \
	       exit 1; \
	 fi
	@inc=`grep -n 'benchNameSeq = ++gBenchNameSeq;' $(SD_BENCH_SRC) | wc -l`; \
	 fmt=`grep -n '"benchmark_%lu_%lu.dat"' $(SD_BENCH_SRC) | wc -l`; \
	 [ "$$inc" -eq 1 ] && [ "$$fmt" -eq 1 ] \
	   || { echo "ERROR: cannot locate the #958 naming steps uniquely in"; \
	        echo "       $(SD_BENCH_SRC) (advance=$$inc, format=$$fmt; want 1 each)."; \
	        echo "       The order pin below compares their positions and needs"; \
	        echo "       exactly one of each to mean anything."; \
	        exit 1; }; \
	 i=`grep -n 'benchNameSeq = ++gBenchNameSeq;' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 f=`grep -n '"benchmark_%lu_%lu.dat"' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 [ "$$i" -lt "$$f" ] \
	   || { echo "ERROR: $(SD_BENCH_SRC) formats the scratch name at line $$f BEFORE"; \
	        echo "       advancing gBenchNameSeq at line $$i."; \
	        echo "       Every name then carries the counter's INITIAL value, so two"; \
	        echo "       runs at one tick collide exactly as they did before #958 --"; \
	        echo "       the field is present and does nothing. A format pin cannot"; \
	        echo "       see this; the pre-merge audit reproduced it passing all six"; \
	        echo "       of the other guards and the whole host suite."; \
	        exit 1; }
	@if grep -qF '"benchmark_%lu.dat"' $(SD_BENCH_SRC); then \
	       echo "ERROR: the tick-ONLY benchmark filename is back in $(SD_BENCH_SRC)."; \
	       echo "       TickType_t is 32-bit here, so that name repeats after 49.7"; \
	       echo "       days of uptime and the truncating open destroys the earlier"; \
	       echo "       run's file -- the 16-bit defect again, on a longer period."; \
	       echo "       test_958 CASE 5 is that scenario. Keep the sequence field."; \
	       exit 1; \
	 fi
	@if grep -qE 'benchmark_%d\.dat|xTaskGetTickCount\(\)[[:space:]]*&[[:space:]]*0xFFFF' $(SD_BENCH_SRC); then \
	       echo "ERROR: the 16-bit MASKED benchmark filename is back in $(SD_BENCH_SRC)."; \
	       echo "       That is the #958 defect itself: the name then repeats every"; \
	       echo "       65536 ticks (65.5 s) and the truncating open destroys the"; \
	       echo "       earlier run's file silently. test_958's whole premise is that"; \
	       echo "       the mask is gone -- fix the firmware, do not relax this guard."; \
	       exit 1; \
	 fi
	@grep -qE '^#define[[:space:]]+SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX[[:space:]]+40[[:space:]]*$$' $(SD_MGR_HDR) \
	  || { echo "ERROR: SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX is no longer '40' in"; \
	       echo "       $(SD_MGR_HDR). test_958 copies it as FW_FILE_NAME_LEN_MAX and its"; \
	       echo "       no-truncation case is arithmetic on that number -- update both."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(NAME_BIN) test_958_sd_benchmark_filename_collision.c

$(HELP_BIN): test_1004_help_write_abort.c test_framework.h $(SCPI_IFACE_SRC) $(FW_RTOS_CFG) $(RECIPES)
	@grep -qE '^#define[[:space:]]+SCPI_WRITE_MAX_RETRIES[[:space:]]+200[[:space:]]*$$' $(SCPI_IFACE_SRC) \
	  || { echo "ERROR: SCPI_WRITE_MAX_RETRIES is no longer '200' in $(SCPI_IFACE_SRC)."; \
	       echo "       Update FW_WRITE_MAX_RETRIES in test_1004_help_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_WRITE_RETRY_DELAY_MS[[:space:]]+5[[:space:]]*$$' $(SCPI_IFACE_SRC) \
	  || { echo "ERROR: SCPI_WRITE_RETRY_DELAY_MS is no longer '5' in $(SCPI_IFACE_SRC)."; \
	       echo "       Update FW_WRITE_RETRY_DELAY_MS in test_1004_help_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_HELP_WRITE_BUDGET_MS[[:space:]]+2000U[[:space:]]*$$' $(SCPI_IFACE_SRC) \
	  || { echo "ERROR: SCPI_HELP_WRITE_BUDGET_MS is no longer '2000U' in $(SCPI_IFACE_SRC)."; \
	       echo "       Update FW_HELP_BUDGET_MS in test_1004_help_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+configTICK_RATE_HZ[[:space:]]+\( \( TickType_t \) 1000 \)[[:space:]]*$$' $(FW_RTOS_CFG) \
	  || { echo "ERROR: configTICK_RATE_HZ is no longer 1000 Hz in $(FW_RTOS_CFG) -- re-derive test_1004's ms<->tick assumption."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+configTICK_TYPE_WIDTH_IN_BITS[[:space:]]+TICK_TYPE_WIDTH_32_BITS[[:space:]]*$$' $(FW_RTOS_CFG) \
	  || { echo "ERROR: configTICK_TYPE_WIDTH_IN_BITS is no longer 32-bit in $(FW_RTOS_CFG) -- re-derive test_1004's wrap tests."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(HELP_BIN) test_1004_help_write_abort.c

# Every suite, and every one of the eight binaries. This spot has now collided
# twice for the same reason -- fw#961 JSONESC_BIN vs fw#983 SUSPEND_BIN, then
# fw#1012 HELP_BIN vs fw#958 NAME_BIN -- because two branches that each add a
# target both add a line here. Resolved the same way both times: keep each
# side's target rule intact and merge ONLY `run` and `clean`. Dropping a rule
# loses a suite silently, and duplicating one silently shadows the other.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(HELP_BIN) $(NAME_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SUSPEND_BIN)
	./$(HELP_BIN)
	./$(NAME_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(HELP_BIN) $(NAME_BIN)

.PHONY: run clean
