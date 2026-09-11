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

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

# #947: SCPI_SysInfoTextGet's shared-response-buffer write-abort bound. Same
# situation as #943 -- SCPIInterface.c is NOT includable on the host (libscpi
# + FreeRTOS + the whole board/driver graph), so the test re-implements
# SCPI_WriteWithRetry's loop and SysInfoText_Write's two guards against a mock
# clock and mock transport, and compares pre-#947/guard-1-only/post-#947
# verdicts.
#
# That makes three firmware constants a COPY (the two SCPI_WriteWithRetry
# retry constants, shared with every other SCPI write, plus the new
# SCPI_SYSINFO_WRITE_BUDGET_MS this issue adds), PLUS the same 1000 Hz /
# 32-bit-TickType_t assumption #943 already makes (this test's MS_TO_TICKS is
# the identity only because of that). The recipe greps all of them out of the
# real source first and refuses to build if any has changed -- including the
# two FreeRTOS tick-config values, which #943 documented as an assumption but
# never pinned (found by Qodo /agentic_review on this PR: a change to either
# would silently invalidate this test's, and #943's, millisecond arithmetic
# without the build failing). Deliberately grepped here rather than adding a
# 4th grep to $(BENCH_BIN) -- this is the newer, still-open PR; #943 is
# shipped and untouched by this change.
SYSINFO_BIN := run_947_tests
SCPI_INTERFACE_SRC := ../../firmware/src/services/SCPI/SCPIInterface.c
FREERTOS_CONFIG_SRC := ../../firmware/src/config/default/FreeRTOSConfig.h

$(BIN): test_circularbuffer.c test_framework.h stubs/Logger.h stubs/osal/osal.h $(FW_UTIL)/CircularBuffer.c $(FW_UTIL)/CircularBuffer.h
	cp $(FW_UTIL)/CircularBuffer.c $(UUT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BIN) test_circularbuffer.c $(UUT)

$(FMT_BIN): test_fixedpointfmt.c $(FW_UTIL)/FixedPointFmt.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $(FMT_BIN) test_fixedpointfmt.c -lm

$(AD7609_BIN): test_ad7609_scale.c test_framework.h $(FW_ADC)/AD7609Scale.h
	$(CC) $(CFLAGS) -I$(FW_ADC) -o $(AD7609_BIN) test_ad7609_scale.c

$(JSONESC_BIN): test_json_string_escape.c test_framework.h $(FW_SVC)/JSON_StringEscape.h
	$(CC) $(CFLAGS) -I$(FW_SVC) -o $(JSONESC_BIN) test_json_string_escape.c

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

$(SYSINFO_BIN): test_947_sysinfo_write_abort.c test_framework.h $(SCPI_INTERFACE_SRC) $(FREERTOS_CONFIG_SRC)
	@grep -qE '^#define[[:space:]]+SCPI_WRITE_MAX_RETRIES[[:space:]]+200[[:space:]]*$$' $(SCPI_INTERFACE_SRC) \
	  || { echo "ERROR: SCPI_WRITE_MAX_RETRIES is no longer '200' in $(SCPI_INTERFACE_SRC)."; \
	       echo "       Update FW_WRITE_MAX_RETRIES in test_947_sysinfo_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_WRITE_RETRY_DELAY_MS[[:space:]]+5[[:space:]]*$$' $(SCPI_INTERFACE_SRC) \
	  || { echo "ERROR: SCPI_WRITE_RETRY_DELAY_MS is no longer '5' in $(SCPI_INTERFACE_SRC)."; \
	       echo "       Update FW_WRITE_RETRY_DELAY_MS in test_947_sysinfo_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_SYSINFO_WRITE_BUDGET_MS[[:space:]]+2000U[[:space:]]*$$' $(SCPI_INTERFACE_SRC) \
	  || { echo "ERROR: SCPI_SYSINFO_WRITE_BUDGET_MS is no longer '2000U' in $(SCPI_INTERFACE_SRC)."; \
	       echo "       Update FW_SYSINFO_BUDGET_MS in test_947_sysinfo_write_abort.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+configTICK_RATE_HZ[[:space:]]+\( \( TickType_t \) 1000 \)[[:space:]]*$$' $(FREERTOS_CONFIG_SRC) \
	  || { echo "ERROR: configTICK_RATE_HZ is no longer '1000' in $(FREERTOS_CONFIG_SRC)."; \
	       echo "       This test's MS_TO_TICKS(ms) == ms identity (and #943's) assumes a 1000 Hz"; \
	       echo "       tick. Re-derive the timing model in test_947_sysinfo_write_abort.c (and"; \
	       echo "       test_943_bench_stall_bound.c) before trusting either against new firmware."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+configTICK_TYPE_WIDTH_IN_BITS[[:space:]]+TICK_TYPE_WIDTH_32_BITS[[:space:]]*$$' $(FREERTOS_CONFIG_SRC) \
	  || { echo "ERROR: configTICK_TYPE_WIDTH_IN_BITS is no longer TICK_TYPE_WIDTH_32_BITS in $(FREERTOS_CONFIG_SRC)."; \
	       echo "       This test's uint32_t elapsed-tick arithmetic (and #943's) assumes a 32-bit"; \
	       echo "       TickType_t. Re-derive the timing model before trusting either against new firmware."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(SYSINFO_BIN) test_947_sysinfo_write_abort.c

# Every suite, and every one of the six binaries. Merge conflict resolved by
# keeping each side's target intact and merging only `run` and `clean` -- a
# duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SYSINFO_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN)

.PHONY: run clean
