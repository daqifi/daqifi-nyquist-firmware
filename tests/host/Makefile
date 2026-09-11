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

# #998: the claim/arm/refusal/poll order of SCPI_StartStreamingClaimed()'s
# SD-logging arm. Same wall as #943 and #971, one file over -- SCPIInterface.c
# is 8,705 lines and pulls in FreeRTOS, Harmony PLIBs, the WINC driver, nanopb
# and libscpi, so it is not host-includable and this is a MODEL rather than the
# real code. It therefore needs the same way to fail loudly when what it models
# moves underneath it.
#
# The guard is a CONTENT HASH, for the reason #971's is: PR #976 removed a
# branch-gated positional check of this exact shape, for this exact site, after
# three review rounds catalogued how an honest refactor defeats reasoning about
# which BRANCH a write sits in with regexes. #998 exists because that removal
# left the site with no coverage; re-adding that check is explicitly out of its
# scope. A hash claims only "this text is unchanged", which is all a build
# recipe can honestly know.
#
# WHAT THIS PIN DOES NOT COVER, stated because an earlier revision of this line
# claimed "no refactor slips past it" and that is false. This is a pin on the
# CONTENT OF AN INTERIOR SLICE. It is not a pin on the behaviour of the
# function. The sed range begins AT the BEGIN anchor line, so anything inserted
# ABOVE that line is outside the hash while still changing what the slice does.
# Demonstrated: inserting
#
#     pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE;
#
# immediately above the anchor publishes the operand before TryClaim, so the
# claim is refused, START returns an error, and because the refusal branch does
# not restore `mode` the manager reads busy until reboot -- the exact
# publish-then-check shape property C exists to pin. Both anchors stay unique
# and ordered, the selected bytes are unchanged, and the gate stays green.
#
# That gap is INTRINSIC to pinning an interior region, not an anchor-placement
# bug: every slice has a boundary, and code above any boundary you choose can
# change what follows it. Moving the anchor moves the gap. Hashing the whole
# ~1250-line function would close it and is rejected for the reason in the next
# paragraph -- it would fire on nearly every unrelated edit, and a tripwire
# people learn to ignore protects nothing.
#
# So read a green pin as "the slice's bytes are unchanged", which is what it
# checks, and never as "the ordering this models is still correct", which it
# cannot check. The model in test_998_start_streaming_claimed_order.c is what
# describes the ordering; the pin only tells you when to go and re-read it.
#
# WHAT IS HASHED, and why it is a SLICE and not a function. Unlike #971's
# ~8-line helper, SCPI_StartStreamingClaimed() is ~1250 lines with more than
# twenty return sites, so the whole body is impractical to pin: nearly every
# unrelated edit to START would fire it and the tripwire would be trained away.
# Pinned instead is the region between the two anchors below -- the TryClaim,
# the `mode = MODE_WRITE` publish, the arm, the refusal branch's clear+release,
# the success release, and the head of the readiness poll. That is exactly the
# text the three modelled orderings live in, and nothing else.
#
# THE HASH COVERS THE SLICE'S RAW BYTES. Nothing is stripped, nothing is
# normalised. Any byte that changes between the anchors changes the hash --
# including a reworded comment and including reindentation.
#
# That is the fourth rule this pin has had, and the previous three all failed
# the same way: each tried to make some class of "harmless" edit not fire, and
# each turned out to erase a harmful edit too.
#
#   1. line-anchored comment strip  -- erased a leading-dereference statement,
#                                      and the code half of /* note */ stmt();
#   2. awk comment-state machine    -- erased a statement spliced into a //
#                                      comment by a backslash-newline, and one
#                                      hidden by comment markers inside string
#                                      literals
#   3. whitespace-run collapse      -- erased whitespace edits INSIDE string
#                                      literals, which change what the firmware
#                                      logs: padding a message past Logger's
#                                      128-byte line truncates away its reason
#
# Rules 1 and 2 were about comments and rule 3 was not, which is the point --
# the defect was never "our comment parser is wrong", it was "we are deciding
# which edits do not matter, and we keep being wrong about that". A tripwire
# does not get to make that judgement. So it no longer makes any.
#
# COST, and it is real: reindenting the slice fires, and rewording a comment in
# it fires. 59 of the slice's 89 lines are comment, so this will fire on edits
# that turn out to be harmless. Each such fire costs one re-read and a
# re-derived SHA. The alternative, three times running, was a silent pass over
# an edit that mattered. Re-derive with:
#
#   sed -n '/^[[:space:]]*<BEGIN anchor>/,/^[[:space:]]*<END anchor>/p' \
#       firmware/src/services/SCPI/SCPIInterface.c | sha256sum
#
# The END anchor carries the poll's `readyWait < 500` bound, which the model
# copies as MODEL_POLL_MAX. A change to that number fails the anchor check by
# name rather than merely shifting the hash, which is the more useful message.
START_BIN := run_998_tests
START_CLAIMED_SRC := ../../firmware/src/services/SCPI/SCPIInterface.c
START_CLAIMED_BEGIN := if (!sd_card_manager_TryClaim()) {
START_CLAIMED_END := while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
START_CLAIMED_SHA := 188a8d9e5cc6a03e1998cefb9e506dac97a2b0f73f3c035138095187dc682942

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

# Four separate failures with four separate messages, because they send the
# reader to four different places: the slice is GONE or DUPLICATED (the model
# has lost, or can no longer name, its subject); the two anchors are in the
# wrong order (the extraction is ill-formed, so the hash below would report a
# misleading "code changed"); sha256sum is missing (a toolchain problem, not
# drift); or the slice is there and its CODE CHANGED (the model may have gone
# stale). Collapsing any of them into "hash mismatch" would send the reader to
# diff a region that is not there, or not the one they think.
$(START_BIN): test_998_start_streaming_claimed_order.c test_framework.h $(START_CLAIMED_SRC) Makefile
	@n=$$(grep -cF '$(START_CLAIMED_BEGIN)' $(START_CLAIMED_SRC) || true); \
	 [ "$$n" = "1" ] \
	  || { echo "ERROR: the slice's OPENING anchor occurs $$n times (want exactly 1)"; \
	       echo "       in $(START_CLAIMED_SRC). The anchor is:"; \
	       echo "         $(START_CLAIMED_BEGIN)"; \
	       echo "       0 means SCPI_StartStreamingClaimed()'s SD arm was moved,"; \
	       echo "       renamed or reshaped, and the model in"; \
	       echo "       test_998_start_streaming_claimed_order.c has lost its subject."; \
	       echo "       More than 1 means the extraction below can no longer name ONE"; \
	       echo "       region: re-anchor it on a line that is unique and re-derive"; \
	       echo "       START_CLAIMED_SHA. Either way, read the code against the model."; \
	       exit 1; }
	@n=$$(grep -cF '$(START_CLAIMED_END)' $(START_CLAIMED_SRC) || true); \
	 [ "$$n" = "1" ] \
	  || { echo "ERROR: the slice's CLOSING anchor occurs $$n times (want exactly 1)"; \
	       echo "       in $(START_CLAIMED_SRC). The anchor is:"; \
	       echo "         $(START_CLAIMED_END)"; \
	       echo "       If the readiness poll's 500-iteration bound changed, update"; \
	       echo "       BOTH this anchor and MODEL_POLL_MAX in"; \
	       echo "       test_998_start_streaming_claimed_order.c, then re-derive"; \
	       echo "       START_CLAIMED_SHA."; \
	       exit 1; }
	@b=$$(grep -nF '$(START_CLAIMED_BEGIN)' $(START_CLAIMED_SRC) | cut -d: -f1); \
	 e=$$(grep -nF '$(START_CLAIMED_END)' $(START_CLAIMED_SRC) | cut -d: -f1); \
	 [ "$$b" -lt "$$e" ] \
	  || { echo "ERROR: the closing anchor (line $$e) is not below the opening one"; \
	       echo "       (line $$b) in $(START_CLAIMED_SRC), so the sed range below"; \
	       echo "       would run to end-of-file and the hash would report a"; \
	       echo "       misleading 'the code changed'."; \
	       echo "       This is a WELL-FORMEDNESS check on the extraction, NOT an"; \
	       echo "       assertion about the ordering -- do not read a pass here as"; \
	       echo "       the poll being in the right place. Re-read the code against"; \
	       echo "       the model."; \
	       exit 1; }
	@command -v sha256sum >/dev/null 2>&1 \
	  || { echo "ERROR: sha256sum not found -- it is what pins the modelled slice of"; \
	       echo "       SCPI_StartStreamingClaimed() for"; \
	       echo "       test_998_start_streaming_claimed_order.c. Install GNU coreutils."; \
	       echo "       (Reported separately so it is not read as a drift failure.)"; \
	       exit 1; }
	@got=$$(sed -n '/^[[:space:]]*$(START_CLAIMED_BEGIN)/,/^[[:space:]]*$(START_CLAIMED_END)/p' $(START_CLAIMED_SRC) \
	        | sha256sum | cut -d' ' -f1); \
	 [ "$$got" = "$(START_CLAIMED_SHA)" ] \
	  || { echo "ERROR: the CODE of SCPI_StartStreamingClaimed()'s SD arm has changed"; \
	       echo "       in $(START_CLAIMED_SRC)."; \
	       echo "         expected sha256 $(START_CLAIMED_SHA)"; \
	       echo "         got      sha256 $$got"; \
	       echo "       This is a tripwire, not a verdict: the change may be perfectly"; \
	       echo "       correct. Re-read the slice between the two anchors against the"; \
	       echo "       model in test_998_start_streaming_claimed_order.c -- if the"; \
	       echo "       model still describes it (refusal before the poll, clear before"; \
	       echo "       release, claim once before the arm), update START_CLAIMED_SHA;"; \
	       echo "       if it does not, fix the model."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(START_BIN) test_998_start_streaming_claimed_order.c

# Every suite, and every one of the SIX binaries. Merge conflict resolved by
# keeping each side's target intact and merging only `run` and `clean` -- a
# duplicated rule here would silently shadow one of them. Six now, not five:
# #998's start-arm-order suite landed on top of the five. #971's suite
# (run_971_tests) is a seventh landing independently on another branch; when
# the two meet, keep both targets and merge only `run` and `clean`.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(START_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(START_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(START_BIN)

.PHONY: run clean
