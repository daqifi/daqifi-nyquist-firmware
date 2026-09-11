# #1000: the #942 SD-log-arm refusal's LOG_E line, measured against the
# ceiling Logger actually cuts at. Same host-testability constraint as
# #1004's fragment -- SCPIInterface.c is not includable here -- but the
# subject is a LENGTH, not a control-flow shape, so there is nothing to
# re-implement. The test reproduces Logger's truncation for real and runs
# the actual strings through it.
#
# That only means anything if the strings and the ceiling are the REAL
# ones, so this recipe EXTRACTS them into $(GEN_1000) rather than letting
# the test carry copies: LOG_MESSAGE_SIZE from Logger.h, BOTH of Logger.c's
# reservations (the vsnprintf bound and the clamp -- the arithmetic is split
# across the two files, which is why both are prerequisites), and the LOG_E
# prefix plus the call site's NULL fallback from SCPIInterface.c. A firmware
# change to any of them re-derives the budget and fails if a line no longer
# fits, rather than asking to be updated.
#
# Extraction has a failure mode a grep guard does not: finding NOTHING. An
# empty prefix satisfies every budget assertion in the test while
# establishing nothing at all -- the vacuous pass. So each extraction is
# checked non-empty here, and the prefix is then ROUND-TRIPPED: reassembled
# into the full LOG_E call text and required to appear verbatim in the
# source, which a silently truncated extraction cannot satisfy. The build
# fails, by name, on any of it.
#
# The three SD_SuspendReasonText() strings are the one thing NOT extracted.
# They belong to that function rather than to this call site, and #1001 is
# building the mechanism that measures them against all of their callers;
# until then they are copied into the test and each full return statement
# is grepped here -- the whole statement including its closing quote and
# semicolon, not the reason's own words, which is how the guard tried in
# #983 was defeated (it matched the longer string it was meant to replace).
# A drifted copy fails the build instead of quietly measuring a string the
# firmware no longer emits.

TEST_1000_BIN := run_1000_tests
GEN_1000      := gen_1000_log_budget.h

$(TEST_1000_BIN): test_1000_sd_log_arm_budget.c test_framework.h $(SCPI_IFACE_SRC) $(SD_BENCH_SRC) $(FW_LOG_H) $(FW_LOG_C) $(RECIPES) $(lastword $(MAKEFILE_LIST))
	@grep -qF 'return "SD quarantined after a bus jam - reseat the card, "' $(SD_BENCH_SRC) \
	  && grep -qF '"then SYST:STOR:SD:ENAble 1";' $(SD_BENCH_SRC) \
	  && grep -qF 'return "a WiFi firmware update owns SPI4 - retry when it completes";' $(SD_BENCH_SRC) \
	  && grep -qF 'return "WiFi streaming owns SPI4 - SYST:STR:STOP first";' $(SD_BENCH_SRC) \
	  || { echo "ERROR: an SD_SuspendReasonText() return statement in $(SD_BENCH_SRC)"; \
	       echo "       no longer matches the copy in test_1000_sd_log_arm_budget.c."; \
	       echo "       That test measures those strings against the #942 refusal's"; \
	       echo "       LOG_E prefix, so a drifted copy measures a string the device"; \
	       echo "       does not emit. Update the REASON_ macros there, re-derive the"; \
	       echo "       budget, and check the new length still fits."; \
	       exit 1; }
	@set -e; \
	 msgsz=$$(sed -n 's/^#define[[:space:]]\{1,\}LOG_MESSAGE_SIZE[[:space:]]\{1,\}\([0-9]\{1,\}\)[[:space:]]*$$/\1/p' $(FW_LOG_H)); \
	 vsnres=$$(sed -n 's/^[[:space:]]*size = vsnprintf(buffer, LOG_MESSAGE_SIZE - \([0-9]\{1,\}\), format, args);[[:space:]]*$$/\1/p' $(FW_LOG_C)); \
	 clampres=$$(sed -n 's/^[[:space:]]*size = min((LOG_MESSAGE_SIZE - \([0-9]\{1,\}\)), size);[[:space:]]*$$/\1/p' $(FW_LOG_C)); \
	 if [ -z "$$msgsz" ] || [ -z "$$vsnres" ] || [ -z "$$clampres" ]; then \
	   echo "ERROR: could not read Logger's message ceiling out of $(FW_LOG_H) / $(FW_LOG_C)"; \
	   echo "       (LOG_MESSAGE_SIZE='$$msgsz' vsnprintf reserve='$$vsnres' clamp reserve='$$clampres')."; \
	   echo "       test_1000_sd_log_arm_budget.c derives its entire budget from those"; \
	   echo "       three, and mirrors LogMessageFormatImpl's truncation. If that"; \
	   echo "       function was reshaped, re-derive the mirror before re-enabling."; \
	   exit 1; \
	 fi; \
	 nfb=$$(grep -cF '"the SD task is not accepting work");' $(SCPI_IFACE_SRC) || true); \
	 if [ "$$nfb" != "1" ]; then \
	   echo "ERROR: the #942 SD-log-arm refusal's NULL fallback string is not uniquely"; \
	   echo "       present in $(SCPI_IFACE_SRC) (found $$nfb occurrences)."; \
	   echo "       test_1000_sd_log_arm_budget.c anchors its prefix extraction on that"; \
	   echo "       line -- re-read both before changing either."; \
	   exit 1; \
	 fi; \
	 prefix=$$(grep -B1 -F '"the SD task is not accepting work");' $(SCPI_IFACE_SRC) \
	          | sed -n 's/^[[:space:]]*LOG_E("\(.*\)%s\\r\\n",[[:space:]]*$$/\1/p'); \
	 fallback=$$(grep -F '"the SD task is not accepting work");' $(SCPI_IFACE_SRC) \
	          | sed -n 's/^[[:space:]]*why ? why : "\(.*\)");[[:space:]]*$$/\1/p'); \
	 if [ -z "$$prefix" ] || [ -z "$$fallback" ]; then \
	   echo "ERROR: could not extract the #942 refusal's LOG_E prefix or its NULL"; \
	   echo "       fallback from $(SCPI_IFACE_SRC)."; \
	   echo "       prefix=[$$prefix] fallback=[$$fallback]"; \
	   echo "       The LOG_E format string must sit on ONE line, immediately above"; \
	   echo "       the fallback, ending in the %s and CRLF escapes. An empty"; \
	   echo "       extraction would pass every length assertion in"; \
	   echo "       test_1000_sd_log_arm_budget.c while checking nothing, so this"; \
	   echo "       refuses to build instead."; \
	   exit 1; \
	 fi; \
	 if printf '%s%s' "$$prefix" "$$fallback" | grep -q '["\]'; then \
	   echo "ERROR: the extracted prefix or fallback contains a quote or a backslash,"; \
	   echo "       which this recipe cannot faithfully re-emit as a C string literal"; \
	   echo "       in $(GEN_1000). Re-derive the extraction in tests/host/tests/1000-sd-log-arm-budget.mk."; \
	   exit 1; \
	 fi; \
	 want="LOG_E(\"$$prefix%s\\r\\n\","; \
	 if ! grep -qF "$$want" $(SCPI_IFACE_SRC); then \
	   echo "ERROR: the extracted prefix does not round-trip into $(SCPI_IFACE_SRC)."; \
	   echo "       Reassembled as: $$want"; \
	   echo "       A partial extraction understates the prefix length, which would"; \
	   echo "       let test_1000_sd_log_arm_budget.c pass on a line the device cuts."; \
	   exit 1; \
	 fi; \
	 printf '/* GENERATED at build time by tests/host/tests/1000-sd-log-arm-budget.mk ($(TEST_1000_BIN)).\n' > $(GEN_1000); \
	 printf ' * Do not edit, do not commit -- see tests/host/.gitignore. */\n' >> $(GEN_1000); \
	 printf '#ifndef GEN_1000_LOG_BUDGET_H\n#define GEN_1000_LOG_BUDGET_H\n' >> $(GEN_1000); \
	 printf '#define FW_LOG_MESSAGE_SIZE      %s\n' "$$msgsz"    >> $(GEN_1000); \
	 printf '#define FW_LOG_VSNPRINTF_RESERVE %s\n' "$$vsnres"   >> $(GEN_1000); \
	 printf '#define FW_LOG_CLAMP_RESERVE     %s\n' "$$clampres" >> $(GEN_1000); \
	 printf '#define FW_1000_PREFIX           "%s"\n' "$$prefix" >> $(GEN_1000); \
	 printf '#define FW_1000_NULL_FALLBACK    "%s"\n' "$$fallback" >> $(GEN_1000); \
	 printf '#endif\n' >> $(GEN_1000)
	$(CC) $(CFLAGS) -o $(TEST_1000_BIN) test_1000_sd_log_arm_budget.c

TESTS       += $(TEST_1000_BIN)
# Identity, not just a count: the guard in ../Makefile compares the SET of
# claimed sources against the set on disk, so a fragment that is deleted,
# duplicated, or pointed at the wrong source is caught instead of balancing
# out. Keep this naming the source THIS fragment compiles.
TEST_SOURCES_CLAIMED += test_1000_sd_log_arm_budget.c
CLEAN_EXTRA += $(GEN_1000)
