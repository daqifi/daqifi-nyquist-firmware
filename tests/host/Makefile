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
# #953 review: the on-device message has a HARD length limit, and the test
# has to know the real one rather than a copy of it. Logger.c formats with
# vsnprintf(buf, LOG_MESSAGE_SIZE - 2, ...), so a line longer than that is cut
# mid-word -- silently, and on the device only, where no host test that
# compares against the pre-truncation constant can see it.
LOGGER_HDR := ../../firmware/src/Util/Logger.h

# #983 review: a grep for the quarantine reason's own text was NOT a drift
# guard. 'then SYST:STOR:SD:ENAble 1' is a substring of the 94-character
# string #986 replaced, and of the comment explaining the replacement, so the
# check passed for exactly the state it was meant to catch. A CONTENT HASH of
# the whole function claims only "this text is unchanged", which is all a
# build recipe can honestly know, and no wording can slip past it.
#
# Hashed: the function from its `const char *<name>(` line to its closing
# brace, with WHOLE-LINE comments dropped and whitespace collapsed -- so
# rewording the comment block above it does not fire, and any change to a
# returned string does. Same mechanism, same reasoning, as the
# SD_ArmOrRefuseWithCleanup pin in #971.
SUSPEND_REASON_FN := SD_SuspendReasonText
SUSPEND_REASON_SHA := 799914e5de6c69bd491438343a5b02a3aeac105713c29b332c97f670d0192409

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

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

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

$(SUSPEND_BIN): test_953_bench_suspend_diagnosis.c test_framework.h $(SD_BENCH_SRC) $(LOGGER_HDR)
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
	@grep -qE '^#define[[:space:]]+LOG_MESSAGE_SIZE[[:space:]]+[0-9]+' $(LOGGER_HDR) \
	  || { echo "ERROR: no '#define LOG_MESSAGE_SIZE <n>' in $(LOGGER_HDR)."; \
	       echo "       test_953_bench_suspend_diagnosis.c checks that the arm's"; \
	       echo "       message survives Logger's vsnprintf intact, and it takes the"; \
	       echo "       limit from here rather than copying it. Re-point LOGGER_HDR."; \
	       exit 1; }
	@command -v sha256sum >/dev/null 2>&1 \
	  || { echo "ERROR: sha256sum not found -- it is what pins"; \
	       echo "       $(SUSPEND_REASON_FN)()'s text for the length assertions in"; \
	       echo "       test_953_bench_suspend_diagnosis.c. Install GNU coreutils."; \
	       echo "       (Reported separately so it is not read as a drift failure.)"; \
	       exit 1; }
	@grep -q '^const char \*$(SUSPEND_REASON_FN)(' $(SD_BENCH_SRC) \
	  || { echo "ERROR: no '^const char *$(SUSPEND_REASON_FN)(' line in"; \
	       echo "       $(SD_BENCH_SRC) -- renamed or moved, and the hash below"; \
	       echo "       extracts it from that line to its closing brace."; \
	       exit 1; }
	@got=$$(sed -n '/^const char \*$(SUSPEND_REASON_FN)(/,/^}/p' $(SD_BENCH_SRC) \
	        | sed -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\//d' \
	        | tr -s '[:space:]' ' ' | sha256sum | cut -d' ' -f1); \
	 [ "$$got" = "$(SUSPEND_REASON_SHA)" ] \
	  || { echo "ERROR: the CODE of $(SUSPEND_REASON_FN)() has changed."; \
	       echo "         expected sha256 $(SUSPEND_REASON_SHA)"; \
	       echo "         got      sha256 $$got"; \
	       echo "       test_953_bench_suspend_diagnosis.c carries COPIES of the three"; \
	       echo "       reason strings and measures them against Logger's limit (#986)."; \
	       echo "       A tripwire, not a verdict: re-read the function against those"; \
	       echo "       copies, then update them and this hash together."; \
	       exit 1; }
	@grep -qF 'LOG_E("SD:BENCH - could not complete the arm: %s' $(SD_BENCH_SRC) \
	  || { echo "ERROR: the mid-wait arm's LOG_E call changed in $(SD_BENCH_SRC)."; \
	       echo "       test_953_bench_suspend_diagnosis.c carries a copy of that"; \
	       echo "       prefix and measures the formatted line against Logger's limit."; \
	       echo "       Matched as the whole LOG_E( call, not the bare text, so a"; \
	       echo "       comment quoting the prefix cannot satisfy this check."; \
	       exit 1; }
	$(CC) $(CFLAGS) \
	  -DFW_LOG_MESSAGE_SIZE=`grep -E '^#define[[:space:]]+LOG_MESSAGE_SIZE' $(LOGGER_HDR) | awk '{print $$3}'` \
	  -o $(SUSPEND_BIN) test_953_bench_suspend_diagnosis.c

# Every suite, and every one of the six binaries. Merge conflict (fw#961
# JSONESC_BIN vs fw#983 SUSPEND_BIN, both merging around the same time)
# resolved by keeping each side's target intact and merging only `run` and
# `clean` -- a duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SUSPEND_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN)

.PHONY: run clean
