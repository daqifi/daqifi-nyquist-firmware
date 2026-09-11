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

# #970: encoder consume-accounting. Like BENCH_BIN, streaming.c and
# JSON_Encoder.c are not host-test candidates (FreeRTOS/libscpi/the whole
# board graph), so this test re-implements the two relevant loop SHAPES
# (streaming.c's `encoded == 0` booking arm, and JSON_Encoder.c's DIO
# element commit loop) against injected mocks. The greps below pin the
# three guarantees that re-implementation depends on, and fail the build
# (not just the test) if any has drifted:
#   1. streaming.c's arm no longer increments encoderDroppedSamples --
#      that increment was the #970 defect (a zero-byte return does not
#      imply exactly one lost sample for every encoder). If this grep
#      fails, the arm has regressed back to the pre-fix shape.
#   2. streaming.h declares Streaming_ReportEncoderSampleLoss -- the entry
#      point an encoder uses to book a loss IT knows about, instead of the
#      caller guessing.
#   3. JSON_Encoder.c's DIO loop pops before committing (the exact guard
#      text), i.e. the twin of the #164 ADC-loop fix this PR gives the DIO
#      loop.
#   4. JSON_Encoder.c and NanoPB_Encoder.c actually CALL that entry point.
#      Pins 1-3 establish that the caller stopped guessing and that the
#      entry point EXISTS -- none of them establishes that any producer
#      reaches it. Deleting the production call would leave every host
#      assertion and all of pins 1-3 green while real destroyed samples
#      stopped being counted, so the test's whole subject would be
#      unguarded. Pin the CALL, per-file, for both encoders that book.
#      The pin asks the COMPILER to strip comments (`$(CC) -fpreprocessed
#      -dD -E`, which removes them without resolving any include) and then
#      flattens newlines before matching, so it owns neither a comment
#      heuristic nor a line-shape heuristic. That is deliberate: the two
#      hand-rolled versions before it were both defeated, and by opposite
#      failures.
#        v1, a plain symbol grep, was a PROXY -- the symbol also appears
#        twice in JSON_Encoder.c's own prose, so deleting the real call and
#        leaving the comments kept the guard GREEN.
#        v2 matched a call statement through its `);` and dropped lines
#        whose prefix looked like a comment. That was wrong in BOTH
#        directions: a legitimate call reformatted across two lines RED a
#        correct tree, and a call inside a STARLESS block comment still
#        passed -- the per-line prefix test cannot see comment state.
#      A third regex would have been the third patch to one mechanism, so
#      the mechanism was replaced instead. `cc` already has an exact
#      comment parser; the pin uses it rather than approximating one.
#      It counts call sites per file rather than testing existence:
#      NanoPB_Encoder.c has TWO paths that destroy a sample, and an
#      existence check stayed green when either one was deleted because the
#      other still matched. Mutation-proven in six directions -- see the
#      table in the commit.
BIN970 := run_970_tests
STREAMING_SRC := ../../firmware/src/services/streaming.c
STREAMING_HDR := ../../firmware/src/services/streaming.h
JSON_ENC_SRC  := ../../firmware/src/services/JSON_Encoder.c
NANOPB_ENC_SRC := ../../firmware/src/services/DaqifiPB/NanoPB_Encoder.c

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

$(BIN970): test_970_encoder_consume_accounting.c test_framework.h $(STREAMING_SRC) $(STREAMING_HDR) $(JSON_ENC_SRC) $(NANOPB_ENC_SRC)
	@grep -qF 'gStreamStats.encoderDroppedSamples++' $(STREAMING_SRC) \
	  && { echo "ERROR: $(STREAMING_SRC) still increments encoderDroppedSamples directly --"; \
	       echo "       the #970 fix removed this from the encoded==0 arm. If this is"; \
	       echo "       intentional, re-derive test_970_encoder_consume_accounting.c's"; \
	       echo "       old-vs-new comparison against the new shape."; \
	       exit 1; } || true
	@grep -qE 'void[[:space:]]+Streaming_ReportEncoderSampleLoss\(uint32_t samples\);' $(STREAMING_HDR) \
	  || { echo "ERROR: Streaming_ReportEncoderSampleLoss is no longer declared in $(STREAMING_HDR)."; \
	       echo "       This is the #970 entry point encoders use to book a loss they"; \
	       echo "       caused -- test_970_encoder_consume_accounting.c models it directly."; \
	       exit 1; }
	@grep -qF 'if (!DIOSampleList_PopFront(&state->DIOSamples, &data)) {' $(JSON_ENC_SRC) \
	  || { echo "ERROR: $(JSON_ENC_SRC)'s DIO loop no longer gates the commit on"; \
	       echo "       DIOSampleList_PopFront's return (the #970 fix, mirroring #164's"; \
	       echo "       ADC-loop treatment). Update the guard or re-derive the DIO half of"; \
	       echo "       test_970_encoder_consume_accounting.c."; \
	       exit 1; }
	@set -- "$(JSON_ENC_SRC)" 1 "$(NANOPB_ENC_SRC)" 2; \
	 while [ $$# -gt 0 ]; do \
	   f=$$1; want=$$2; shift 2; \
	   got=`$(CC) -fpreprocessed -dD -E "$$f" 2>/dev/null | tr '\n' ' ' \
	        | grep -oE 'Streaming_ReportEncoderSampleLoss[[:space:]]*\(' | wc -l`; \
	   [ "$$got" -ge "$$want" ] \
	     || { echo "ERROR: $$f has $$got call(s) to Streaming_ReportEncoderSampleLoss, expected >= $$want."; \
	          echo "       #970 moved loss booking from the caller's zero-return guess"; \
	          echo "       to the encoder that destroyed the sample. Each count below is"; \
	          echo "       the number of distinct paths in that file that DESTROY a"; \
	          echo "       sample: JSON_Encoder.c 1 (the oversize-sample drop arm),"; \
	          echo "       NanoPB_Encoder.c 2 (the AIN-message encode failure and the"; \
	          echo "       standalone-DIO fallback). A per-file EXISTENCE check could not"; \
	          echo "       see one of two sites vanish -- the surviving call kept it"; \
	          echo "       green -- so either PB loss path could stop reaching the"; \
	          echo "       counter silently. The host test models the reporter; it never"; \
	          echo "       executes Nanopb_EncodeStreamingFast, so nothing else here"; \
	          echo "       observes these call sites."; \
	          echo "       ADDING a path is fine and raises the count; this only reds"; \
	          echo "       when a site DISAPPEARS, which is the regression it is for."; \
	          exit 1; }; \
	 done
	$(CC) $(CFLAGS) -o $(BIN970) test_970_encoder_consume_accounting.c

# Every suite, and every one of the six binaries. Merge conflict resolved by
# keeping each side's target intact and merging only `run` and `clean` -- a
# duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(BIN970)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(BIN970)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(BIN970)

.PHONY: run clean
