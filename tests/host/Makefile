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

# #985: SD_SuspendReasonText() itself -- the function the cascade above calls.
# Third target on SCPIStorageSD.c and, for the same reason as the two above,
# the same technique: no UUT copy, no stubs, both decision SHAPES (pre-fix and
# post-fix) re-implemented against an injected environment and compared.
#
# It copies no constants either, but it does copy the SHAPE, and here the
# shape IS the whole fix: the four flags read ONCE each, ahead of every
# branch, with no call to the composed gate app_SDCard_SpiOwnedByWifi()
# (calling that AND separately re-reading its parts is the #985 defect). A
# drift back to two samples would leave this test passing green against a
# model the firmware no longer has -- which is exactly the failure a guard is
# for. So the guard reads the real function and requires exactly one call to
# each of the four flag accessors, zero to the composite, and the four arms
# still in the precedence order the test asserts.
#
# Comment lines are stripped before counting, because the function's own
# comments name both the accessors and the composite on purpose. A comment
# restyled so its lines do not begin with `*` or `/` therefore fails this
# guard -- loudly, which is the right outcome for a guard whose job is to
# notice edits to this function.
SUSPENDREASON_BIN := run_985_tests

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

# #913: UserSpi.c's spi_XferByte busy-spin -> spin-then-vTaskDelay yield.
# Like BENCH_BIN this needs no UUT copy and no stubs -- UserSpi.c is NOT
# includable on the host (FreeRTOS, Harmony's definitions.h/configuration.h,
# the DIO ownership registry), so the test re-implements spi_WaitStat's loop
# SHAPE against a mock tick clock and mock hardware-condition flag.
#
# That makes the fast-spin count and the timeout budget a COPY, which can
# drift. The recipe greps both out of the real source first and refuses to
# build if either has changed, so a stale copy can't pass silently -- same
# convention as BENCH_BIN above.
SPI913_BIN := run_913_tests
USER_SPI_SRC := ../../firmware/src/HAL/UserSpi/UserSpi.c
USER_UART_SRC := ../../firmware/src/HAL/UserUart/UserUart.c
USER_I2C_SRC := ../../firmware/src/HAL/UserI2c/UserI2c.c

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

# #958: SD:BENCHmark's scratch FILENAME. Third target on the same
# non-host-compilable file as BENCH_BIN and SUSPEND_BIN, same technique for the
# same reason: the name generation is re-implemented against injected tick
# values rather than included.
#
# It copies BOTH a format string and a length constant, and it also depends on
# two DEFECTS being absent, so the recipe carries eight guards. Three are about
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
# the counter's initial value), that the LOCAL copy is assigned exactly once
# outside its declaration (position alone is not enough -- a second
# assignment after the advance restores the initial value with the order pin
# still satisfied, which the pre-merge audit reproduced), that the global is
# NEVER assigned anywhere but its definition (a reset reopens the collision),
# and that the tick-only form has not come back. Each fails loudly and names
# what to re-read.
NAME_BIN := run_958_tests
SD_MGR_HDR := ../../firmware/src/services/sd_card_services/sd_card_manager.h

# #1000: the #942 SD-log-arm refusal's LOG_E line, measured against the ceiling
# Logger actually cuts at. Same host-testability constraint as HELP_BIN above
# -- SCPIInterface.c is not includable here -- but the subject is a LENGTH, not
# a control-flow shape, so there is nothing to re-implement. The test
# reproduces Logger's truncation for real and runs the actual strings through
# it.
#
# That only means anything if the strings and the ceiling are the REAL ones, so
# this recipe EXTRACTS them into $(GEN_1000) rather than letting the test carry
# copies: LOG_MESSAGE_SIZE from Logger.h, BOTH of Logger.c's reservations (the
# vsnprintf bound and the clamp -- the arithmetic is split across the two
# files, which is why both are prerequisites), and the LOG_E prefix plus the
# call site's NULL fallback from SCPIInterface.c. A firmware change to any of
# them re-derives the budget and fails if a line no longer fits, rather than
# asking to be updated.
#
# Extraction has a failure mode a grep guard does not: finding NOTHING. An
# empty prefix satisfies every budget assertion in the test while establishing
# nothing at all -- the vacuous pass. So each extraction is checked non-empty
# here, and the prefix is then ROUND-TRIPPED: reassembled into the full LOG_E
# call text and required to appear verbatim in the source, which a silently
# truncated extraction cannot satisfy. The build fails, by name, on any of it.
#
# The three SD_SuspendReasonText() strings are the one thing NOT extracted.
# They belong to that function rather than to this call site, and #1001 is
# building the mechanism that measures them against all of their callers; until
# then they are copied into the test and each full return statement is grepped
# here -- the whole statement including its closing quote and semicolon, not
# the reason's own words, which is how the guard tried in #983 was defeated (it
# matched the longer string it was meant to replace). A drifted copy fails the
# build instead of quietly measuring a string the firmware no longer emits.
SDLOG_BIN := run_1000_tests
GEN_1000  := gen_1000_log_budget.h
FW_LOG_H  := $(FW_UTIL)/Logger.h
FW_LOG_C  := $(FW_UTIL)/Logger.c

# Every binary depends on this file as well as on its sources. FIVE of the
# nine targets below carry GUARDS in their recipes -- greps that fail the build when
# the firmware text they model has moved -- and a guard that is only re-run when
# a source changes is not re-run when the GUARD itself changes. CI is unaffected
# either way (Actions checks out clean, so `make run` always builds from
# nothing); this is for the local incremental build, where the binary outliving
# an edited recipe is exactly how a guard goes quiet. Qodo, PR #983.
#
# The count moves every time a guarded suite is added and it has been wrong
# more than once -- it read "Two" while four targets were guarded, then
# "Four" on one branch and "Two" on another, which is how this merge found
# it. Guarded now: BENCH_BIN, SUSPEND_BIN, HELP_BIN, NAME_BIN, SDLOG_BIN.
# Naming them rather than only counting them makes the next drift visible
# instead of arithmetic.
RECIPES := Makefile

# #980: DAC7718 error-path honesty (lock ownership in DAC7718_ReadWriteReg,
# init-failure propagation + slot retention in DAC_EnsureHardwareInitialized).
# Like BENCH_BIN, neither DAC7718.c (Harmony GPIO/SPI/coretimer + FreeRTOS
# semaphores) nor SCPIDAC.c (+ libscpi + BoardData/BoardConfig) is includable
# on the host, so the test re-implements both control-flow SHAPES against
# mocks. The grep guards pin the two shape-defining identifiers and the
# one-slot allocator constant the mock's fidelity claim depends on; a rename,
# removal, or a MAX_DAC7718_CONFIG bump above 1 invalidates the test's premise
# and must fail loudly rather than silently pass a stale copy.
#
# Part D (added post-merge, PR #990's own pre-merge adversarial audit, BLOCK
# verdict): SCPI_DACVoltageGet's dacWriterPossible gate, fixing the confirmed
# regression where the getter's new command-serialization lock was taken
# UNCONDITIONALLY even though the mutex it locks is created only under
# BoardVariant == 3 -- failing every SOUR:VOLT:LEV? with "DAC command busy"
# on NQ1/NQ2 (every board on this bench) where the pre-PR getter always
# succeeded. Two more grep guards below pin the predicate and the
# lockHeld-threaded Unlock call.
#
# Part E (#1030): SCPI_DACVoltageGet must release gDacCommandMutex BEFORE
# calling SCPI_ResultVoltage, not across it -- the transport write's own
# retry budget (up to ~1s per value against a stalled reader) must not
# extend how long the whole-command lock is held. Three more grep guards
# below pin the local-copy array and the line-number ordering of the
# Unlock call against both transport-write call sites.
DAC980_BIN := run_980_tests
DAC7718_SRC := ../../firmware/src/HAL/DAC7718/DAC7718.c
SCPIDAC_SRC := ../../firmware/src/services/SCPI/SCPIDAC.c

# #1057 (#913's twin): DAC7718_ReadWriteReg's six SPI2 busy-spins ->
# dac7718_WaitStat, the same bounded-spin-then-vTaskDelay(1) shape #913 gave
# UserSpi.c's spi_XferByte. Same host-testability constraint as DAC980_BIN
# above -- DAC7718.c is not includable on the host -- so this re-implements
# the loop SHAPE against a mock tick clock and mock hardware-condition flag,
# identically to test_913_spi_wait_stat.c (DAC7718 has only the one wait
# shape, shared by all three of its call sites, unlike UserSpi/UserUart/
# UserI2c's three separate shapes). The two firmware constants this depends
# on are a COPY; the recipe greps them and refuses to build on drift, same
# convention as SPI913_BIN.
DAC1057_BIN := run_1057_tests

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

# THE GREPS BELOW PIN CONSTANTS, AND THAT IS ALL THEY CLAIM. They do not pin
# the loop's SHAPE. Review of #1009 was right that constants alone cannot catch
# the defect this suite exists for -- delete the fresh register read at expiry,
# move the deadline test after the yield, or drop the vTaskDelay(1), and every
# grep here still passes while the modelled test stays green.
#
# A SHAPE CHECKER WAS BUILT FOR EXACTLY THAT AND HAS BEEN DELETED. It went
# through five generations, each defeated by the next review round:
# constants-only, then file-scoped presence, then presence anchored to a
# signature within a 40-line window, then a self-test that asked only whether
# ANY problem came back, then one that named the expected problem text. The
# fifth round found the two holes that ended it. The window anchors on a
# signature and has no notion of where the function ENDS, so gutting the real
# wait function and leaving its loop in a same-shaped sibling ten lines below
# reported zero problems. And the self-test only ever exercised the spi shape,
# so the uart and i2c checks could both be deleted while it printed 9/9 passed.
#
# The first of those cannot be closed without finding function boundaries, i.e.
# PARSING C -- which is the mechanism #1032 exists to get this repo OUT of. So
# the checker was deleted rather than given a sixth generation, and the durable
# fix is tracked as #1056: make the wait loop host-compilable so the test
# exercises the REAL code instead of a copy, the way #889 split AD7609Scale.h
# out so test_ad7609_scale.c could include the shipped header directly.
#
# What is left is honest about its reach. Same convention as every other suite
# here, with the constants pinned PER FILE because the modelled shape is shared
# by all three wait loops and uart and i2c had no pin of their own at all. The
# host test still proves the shape's BEHAVIOUR; nothing here claims any more to
# prove the firmware still has that shape.
$(SPI913_BIN): test_913_spi_wait_stat.c test_framework.h $(USER_SPI_SRC) \
               $(USER_UART_SRC) $(USER_I2C_SRC) $(RECIPES)
	@grep -qE '^#define[[:space:]]+USER_SPI_BYTE_TIMEOUT_MS[[:space:]]+20u[[:space:]]*$$' $(USER_SPI_SRC) \
	  || { echo "ERROR: USER_SPI_BYTE_TIMEOUT_MS is no longer '20u' in $(USER_SPI_SRC)."; \
	       echo "       Update FW_BYTE_TIMEOUT_MS in test_913_spi_wait_stat.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*for \(uint32_t s = 0; s < 8000u; \+\+s\)' $(USER_SPI_SRC) \
	  || { echo "ERROR: spi_WaitStat's fast-spin bound is no longer 's < 8000u' in $(USER_SPI_SRC)."; \
	       echo "       Update FW_SPIN_COUNT in test_913_spi_wait_stat.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*for \(uint32_t s = 0; s < 4000u; \+\+s\)' $(USER_UART_SRC) \
	  || { echo "ERROR: uart_WaitSta's fast-spin bound is no longer 's < 4000u' in $(USER_UART_SRC)."; \
	       echo "       The modelled shape is SHARED by all three wait loops, so each"; \
	       echo "       file's bound is pinned separately -- re-derive against the new one."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*for \(uint32_t s = 0; s < 8000u; \+\+s\)' $(USER_I2C_SRC) \
	  || { echo "ERROR: i2c_WaitMif's fast-spin bound is no longer 's < 8000u' in $(USER_I2C_SRC)."; \
	       echo "       Same shared shape, pinned per file -- re-derive."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+I2C_OP_TIMEOUT_CP0[[:space:]]+700000u' $(USER_I2C_SRC) \
	  || { echo "ERROR: I2C_OP_TIMEOUT_CP0 is no longer '700000u' in $(USER_I2C_SRC)."; \
	       echo "       i2c is the most reachable of the three -- a false timeout there"; \
	       echo "       fires i2c_BusRecover() on a healthy bus -- and the test's budget"; \
	       echo "       arithmetic is derived from this number. Re-derive it."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(SPI913_BIN) test_913_spi_wait_stat.c

$(DAC980_BIN): test_980_dac7718_error_paths.c test_framework.h $(DAC7718_SRC) $(SCPIDAC_SRC) $(RECIPES)
	@grep -qE '^[[:space:]]*bool lockHeld = false;' $(DAC7718_SRC) \
	  || { echo "ERROR: DAC7718_ReadWriteReg's 'lockHeld' local is gone from $(DAC7718_SRC)."; \
	       echo "       test_980_dac7718_error_paths.c Part A models exactly this variable -- re-derive it."; \
	       exit 1; }
	@grep -qE 'DAC7718_Unlock\(config, csAsserted, lockHeld\);' $(DAC7718_SRC) \
	  || { echo "ERROR: DAC7718_Unlock's call site no longer passes (config, csAsserted, lockHeld) in $(DAC7718_SRC)."; \
	       echo "       Part A's mock signature mirrors this exact call -- re-derive it."; \
	       exit 1; }
	@test "$$(grep -c 'goto cleanup;' $(DAC7718_SRC))" = "10" \
	  || { echo "ERROR: DAC7718.c's 'goto cleanup;' count in DAC7718_ReadWriteReg changed from 10."; \
	       echo "       Part A's read_write_reg_shape() models exactly 3 PRE-lock validations"; \
	       echo "       (RW, Reg, config) plus 7 post-lock SPI-timeout branches, all reaching the"; \
	       echo "       same cleanup label -- a branch added or removed changes what the mock's"; \
	       echo "       shape claims to cover. Re-derive the test against the new branch count."; \
	       exit 1; }
	@grep -qE '^#define MAX_DAC7718_CONFIG 1[[:space:]]*$$' $(DAC7718_SRC) \
	  || { echo "ERROR: MAX_DAC7718_CONFIG is no longer '1' in $(DAC7718_SRC)."; \
	       echo "       Part B's mock_new_config() models a ONE-slot allocator; a different capacity"; \
	       echo "       changes what 'the slot is retained on retry' even means -- re-derive the test."; \
	       exit 1; }
	@grep -qE 'static volatile bool dacInitInProgress = false;' $(SCPIDAC_SRC) \
	  || { echo "ERROR: dacInitInProgress is gone from $(SCPIDAC_SRC)."; \
	       echo "       test_980_dac7718_error_paths.c Part B models exactly this claim flag -- re-derive it."; \
	       exit 1; }
	@test "$$(grep -c 'BoardData_Get(BOARDDATA_POWER_DATA, 0)' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: BoardData_Get(BOARDDATA_POWER_DATA, 0) call count in $(SCPIDAC_SRC) changed from 2."; \
	       echo "       Part B's ensure_hardware_initialized_ex(..., powerStillUpAtPublish) models the"; \
	       echo "       #980-pass-3 fix for the Qodo 'Power cycles leave the DAC marked ready' bug,"; \
	       echo "       which needs BOTH the top-of-function power check AND a second, FRESH read"; \
	       echo "       immediately before publishing dacHardwareInitialized=true. If this count is"; \
	       echo "       no longer 2, the pre-publish re-check may have been removed -- re-derive."; \
	       exit 1; }
	@test "$$(grep -c 'failedMask |= (1UL << i)' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: failedMask |= (1UL << i) count in $(SCPIDAC_SRC) changed from 2."; \
	       echo "       Part C's all_channel_set_new_shape() models SCPI_DACVoltageSet's all-channel"; \
	       echo "       branch: EVERY per-channel failure (invalid hwChannel, failed register write)"; \
	       echo "       must record into failedMask and fall through to the NEXT channel, never abort"; \
	       echo "       the loop early. If this count changed, a failure path may now return before"; \
	       echo "       the unconditional latch call -- re-derive Part C against the new shape."; \
	       exit 1; }
	@test "$$(grep -c 'staged\[i\] = true;' $(SCPIDAC_SRC))" = "1" \
	  || { echo "ERROR: 'staged[i] = true;' count in $(SCPIDAC_SRC) changed from 1."; \
	       echo "       Part C's fidelity claim is that a channel is only ever marked staged AFTER"; \
	       echo "       both the hwChannel-validity check and the register write succeed, and that"; \
	       echo "       BoardData is published only for staged channels post-latch. Re-derive Part C"; \
	       echo "       if this shape moved."; \
	       exit 1; }
	@grep -qE 'const bool dacWriterPossible = \(pCfg != NULL\) && \(pCfg->BoardVariant == 3\);' $(SCPIDAC_SRC) \
	  || { echo "ERROR: SCPI_DACVoltageGet's dacWriterPossible gate is gone from $(SCPIDAC_SRC)."; \
	       echo "       Part D's getter_gated_shape() models exactly this predicate -- the fix for"; \
	       echo "       the PR #990 BLOCK-audit regression (unconditional lock failing every"; \
	       echo "       SOUR:VOLT:LEV? on NQ1/NQ2). Re-derive Part D if this moved."; \
	       exit 1; }
	@test "$$(grep -c 'SCPIDAC_UnlockCommand(lockHeld);' $(SCPIDAC_SRC))" = "1" \
	  || { echo "ERROR: 'SCPIDAC_UnlockCommand(lockHeld);' count in $(SCPIDAC_SRC) changed from 1."; \
	       echo "       SCPI_DACVoltageGet must thread lockHeld (not an unconditional true) through"; \
	       echo "       to Unlock, since dacWriterPossible == false deliberately skips the lock."; \
	       echo "       Part D's getter_gated_shape() models this -- re-derive it if this moved."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*double allVoltages\[MAX_AOUT_CHANNEL\]' $(SCPIDAC_SRC) \
	  || { echo "ERROR: SCPI_DACVoltageGet's 'allVoltages[MAX_AOUT_CHANNEL]' local copy is gone"; \
	       echo "       from $(SCPIDAC_SRC). Part E (#1030) models copying every BoardData read"; \
	       echo "       into a local BEFORE releasing gDacCommandMutex -- re-derive it if this moved."; \
	       exit 1; }
	@a=`grep -nF 'SCPIDAC_UnlockCommand(lockHeld);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 b=`grep -nF 'SCPI_ResultVoltage(context, singleVoltage, precision);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 c=`grep -nF 'SCPI_ResultVoltage(context, allVoltages[i], precision);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 test -n "$$a" && test -n "$$b" && test -n "$$c" \
	   && test "$$a" -lt "$$b" && test "$$a" -lt "$$c" \
	   || { echo "ERROR: SCPI_DACVoltageGet's Unlock->write ordering (#1030) is gone or reordered"; \
	        echo "       in $(SCPIDAC_SRC) (markers found at a='$$a' b='$$b' c='$$c')."; \
	        echo "       gDacCommandMutex must be released (line a) strictly BEFORE either transport"; \
	        echo "       write (lines b, c) -- Part E's differential test models exactly this ordering."; \
	        exit 1; }
# Part E2 (#1030 audit finding): the ERROR push must also land after the
# unlock. SCPI_ErrorPush reaches the transport (SCPI_ErrorEmit ->
# interface->error -> SCPI_WriteWithRetry), so pushing it under the lock is
# the same defect as writing a value under the lock. The original Part E
# guard pinned only the two SCPI_ResultVoltage sites, which is exactly why
# the out-of-range arm slipped past it.
# WHAT THIS DOES NOT COVER: it pins the ONE deferred push by its unique
# variable name. It cannot prove some future arm has not added a fresh
# SCPI_ErrorPush under the lock -- grep has no notion of the lock region.
	@d=`grep -nF 'SCPI_ErrorPush(context, deferredError);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 a=`grep -nF 'SCPIDAC_UnlockCommand(lockHeld);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 test -n "$$d" && test -n "$$a" && test "$$a" -lt "$$d" \
	   || { echo "ERROR: SCPI_DACVoltageGet's deferred error push (#1030) is gone or moved"; \
	        echo "       back under the lock in $(SCPIDAC_SRC) (unlock a='$$a', push d='$$d')."; \
	        echo "       SCPI_ErrorPush writes to the transport; it must run after the unlock."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(DAC980_BIN) test_980_dac7718_error_paths.c

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

# Every suite, and every one of the seven binaries. Third merge around this
# same block (fw#961 JSONESC_BIN, fw#983 SUSPEND_BIN, fw#913 SPI913_BIN),
# resolved the same way every time: keep each side's target INTACT and merge
# only `run` and `clean` -- a duplicated rule here would silently shadow one
# of them. $(RECIPES) is main's (#983) fix and applies to SPI913_BIN too,
# because it carries grep guards: a guard re-run only when its SOURCE changes
# is not re-run when the GUARD changes.

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
	@n=`grep -vE '^[[:space:]]*uint32_t[[:space:]]+benchNameSeq' $(SD_BENCH_SRC) \
	      | grep -cE 'benchNameSeq[[:space:]]*=[^=]'`; \
	 [ "$$n" -eq 1 ] \
	   || { echo "ERROR: benchNameSeq is assigned $$n times in $(SD_BENCH_SRC)"; \
	        echo "       outside its declaration; exactly 1 is required."; \
	        echo "       The order pin above compares the POSITIONS of the advance"; \
	        echo "       and the format, which a SECOND assignment defeats: put"; \
	        echo "       'benchNameSeq = 0;' after the advance and every name"; \
	        echo "       carries 0 again, with the order pin still satisfied. The"; \
	        echo "       pre-merge audit reproduced exactly that, passing all seven"; \
	        echo "       other guards and the whole host suite."; \
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

$(SDLOG_BIN): test_1000_sd_log_arm_budget.c test_framework.h $(SCPI_IFACE_SRC) $(SD_BENCH_SRC) $(FW_LOG_H) $(FW_LOG_C) $(RECIPES)
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
	   echo "       in $(GEN_1000). Re-derive the extraction in tests/host/Makefile."; \
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
	 printf '/* GENERATED at build time by tests/host/Makefile ($(SDLOG_BIN)).\n' > $(GEN_1000); \
	 printf ' * Do not edit, do not commit -- see tests/host/.gitignore. */\n' >> $(GEN_1000); \
	 printf '#ifndef GEN_1000_LOG_BUDGET_H\n#define GEN_1000_LOG_BUDGET_H\n' >> $(GEN_1000); \
	 printf '#define FW_LOG_MESSAGE_SIZE      %s\n' "$$msgsz"    >> $(GEN_1000); \
	 printf '#define FW_LOG_VSNPRINTF_RESERVE %s\n' "$$vsnres"   >> $(GEN_1000); \
	 printf '#define FW_LOG_CLAMP_RESERVE     %s\n' "$$clampres" >> $(GEN_1000); \
	 printf '#define FW_1000_PREFIX           "%s"\n' "$$prefix" >> $(GEN_1000); \
	 printf '#define FW_1000_NULL_FALLBACK    "%s"\n' "$$fallback" >> $(GEN_1000); \
	 printf '#endif\n' >> $(GEN_1000)
	$(CC) $(CFLAGS) -o $(SDLOG_BIN) test_1000_sd_log_arm_budget.c

# #985: build-time guard over SD_SuspendReasonText() -- see the merge-history
# comment above `run:` below for how this target's registration in run/clean
# was resolved.
$(SUSPENDREASON_BIN): test_985_suspend_reason_consistency.c test_framework.h $(SD_BENCH_SRC) $(RECIPES)
	@body=`sed -n '/^const char \*SD_SuspendReasonText(void)[[:space:]]*$$/,/^}/p' $(SD_BENCH_SRC) | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$body" \
	   || { echo "ERROR: SD_SuspendReasonText(void) was not found in $(SD_BENCH_SRC),"; \
	        echo "       or no longer opens at column 0 -- the guard below can see"; \
	        echo "       nothing, and a guard that examines nothing must fail."; \
	        exit 1; }; \
	 nq=`printf '%s\n' "$$body" | grep -o 'SpiBusHealth_IsSdQuarantined()' | wc -l | tr -d ' '`; \
	 nf=`printf '%s\n' "$$body" | grep -o 'wifi_manager_IsWifiFirmwareUpdateActive()' | wc -l | tr -d ' '`; \
	 nw=`printf '%s\n' "$$body" | grep -o 'app_SDCard_WifiStreamActive()' | wc -l | tr -d ' '`; \
	 ns=`printf '%s\n' "$$body" | grep -o 'SpiBusHealth_IsSdSuspended()' | wc -l | tr -d ' '`; \
	 nc=`printf '%s\n' "$$body" | grep -o 'app_SDCard_SpiOwnedByWifi()' | wc -l | tr -d ' '`; \
	 test "$$nq" -eq 1 && test "$$nf" -eq 1 && test "$$nw" -eq 1 \
	   && test "$$ns" -eq 1 && test "$$nc" -eq 0 \
	   || { echo "ERROR: SD_SuspendReasonText() in $(SD_BENCH_SRC) no longer reads"; \
	        echo "       each flag exactly once with no composed-gate call"; \
	        echo "       (quarantine=$$nq fwUpdate=$$nf wifiStream=$$nw"; \
	        echo "        suspended=$$ns SpiOwnedByWifi=$$nc; want 1 1 1 1 0)."; \
	        echo "       A second read of any of them, or a call to the composite"; \
	        echo "       that already ORs three of them, reintroduces #985 -- and"; \
	        echo "       test_985_suspend_reason_consistency.c models the"; \
	        echo "       single-snapshot shape, so it would pass regardless."; \
	        exit 1; }; \
	 q=`printf '%s\n' "$$body" | grep -n 'if (quarantined)' | cut -d: -f1`; \
	 f=`printf '%s\n' "$$body" | grep -n 'if (fwUpdate)' | cut -d: -f1`; \
	 w=`printf '%s\n' "$$body" | grep -n 'if (wifiStream)' | cut -d: -f1`; \
	 s=`printf '%s\n' "$$body" | grep -n 'if (suspended)' | cut -d: -f1`; \
	 test -n "$$q" && test -n "$$f" && test -n "$$w" && test -n "$$s" \
	   && test "$$q" -lt "$$f" && test "$$f" -lt "$$w" && test "$$w" -lt "$$s" \
	   || { echo "ERROR: the four arms of SD_SuspendReasonText() in $(SD_BENCH_SRC)"; \
	        echo "       are no longer quarantine -> fwUpdate -> wifiStream ->"; \
	        echo "       suspended, or an arm was renamed, moved or duplicated"; \
	        echo "       (markers at q='$$q' f='$$f' w='$$w' s='$$s')."; \
	        echo "       That order is the precedence test_985 asserts, and the"; \
	        echo "       quarantine-first part of it is load-bearing: it is the"; \
	        echo "       one cause that does not clear on its own."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(SUSPENDREASON_BIN) test_985_suspend_reason_consistency.c

$(DAC1057_BIN): test_1057_dac7718_wait_stat.c test_framework.h $(DAC7718_SRC) $(RECIPES)
	@grep -qE '^#define[[:space:]]+DAC7718_SPI_FAST_SPIN_COUNT[[:space:]]+8000u[[:space:]]*$$' $(DAC7718_SRC) \
	  || { echo "ERROR: DAC7718_SPI_FAST_SPIN_COUNT is no longer '8000u' in $(DAC7718_SRC)."; \
	       echo "       Update FW_SPIN_COUNT in test_1057_dac7718_wait_stat.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+DAC7718_SPI_BYTE_TIMEOUT_MS[[:space:]]+20u[[:space:]]*$$' $(DAC7718_SRC) \
	  || { echo "ERROR: DAC7718_SPI_BYTE_TIMEOUT_MS is no longer '20u' in $(DAC7718_SRC)."; \
	       echo "       Update FW_BYTE_TIMEOUT_MS in test_1057_dac7718_wait_stat.c and re-derive its numbers."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*for \(uint32_t s = 0; s < DAC7718_SPI_FAST_SPIN_COUNT; \+\+s\)' $(DAC7718_SRC) \
	  || { echo "ERROR: dac7718_WaitStat's fast-spin loop is gone or reshaped in $(DAC7718_SRC)."; \
	       echo "       The two constant greps above cannot see this -- they pin VALUES, not the"; \
	       echo "       shape that consumes them. test_1057_dac7718_wait_stat.c models this loop;"; \
	       echo "       re-read both before changing either."; \
	       exit 1; }
	@grep -qF 'return (((SPI2STAT & mask) != 0u) == want);' $(DAC7718_SRC) \
	  || { echo "ERROR: dac7718_WaitStat's deadline branch no longer takes a FRESH SPI2STAT"; \
	       echo "       read at expiry in $(DAC7718_SRC) -- that fresh-read is the exact #913"; \
	       echo "       opus-review defect (a bare 'return false;' trusting the pre-check above"; \
	       echo "       it) this suite exists to keep out. Neither constant grep above, nor the"; \
	       echo "       modelled host test, can see this on its own -- see the file header of"; \
	       echo "       test_1057_dac7718_wait_stat.c."; \
	       exit 1; }
	@test "$$(grep -cF 'dac7718_WaitStat(' $(DAC7718_SRC))" = "7" \
	  || { echo "ERROR: expected exactly 7 occurrences of 'dac7718_WaitStat(' in $(DAC7718_SRC)"; \
	       echo "       (1 definition + 6 call sites -- 3 in the write frame, 3 in the RW=1 NOP"; \
	       echo "       readback), found $$(grep -cF 'dac7718_WaitStat(' $(DAC7718_SRC)). A call"; \
	       echo "       site reverting to a bare register-poll spin would not otherwise be caught"; \
	       echo "       by any guard here."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(DAC1057_BIN) test_1057_dac7718_wait_stat.c
# Every suite, and every one of the THIRTEEN binaries below. A new suite
# always touches this same `run:`/`clean:` pair, so any two branches that add
# a suite around the same time collide here. History, each resolved the same
# way (keep each side's target rule INTACT, union only `run` and `clean` --
# dropping a rule loses a suite silently, duplicating one silently shadows
# the other): fw#961 JSONESC_BIN vs fw#983 SUSPEND_BIN; fw#1012 vs fw#958;
# fw#913 vs fw#1004 HELP_BIN; fw#958 vs fw#1000 SDLOG_BIN; fw#980 DAC980_BIN
# landing on main alongside fw#1004 HELP_BIN and fw#1000 SDLOG_BIN; fw#958's
# NAME_BIN vs main's DAC980_BIN; fw#913 vs fw#1004/fw#1000/fw#980/fw#985
# (SPI913_BIN, SUSPENDREASON_BIN) landing together on main; and now this
# merge, uniting fw#958's NAME_BIN with SPI913_BIN, SUSPENDREASON_BIN (#985)
# and DAC1057_BIN (#1057), all landed on main since the previous resolution
# -- the NINTH occurrence of this exact conflict.
#
# #1026 is the ticket to stop this and PR #1040 implements it, moving each
# target into tests/host/tests/<issue>-<slug>.mk so a new suite appends to an
# accumulator instead of editing a shared line. Whichever lands first, the
# rest re-express their target as a fragment.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
     $(NAME_BIN) $(SPI913_BIN) $(SDLOG_BIN) $(SUSPENDREASON_BIN) $(DAC1057_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SUSPEND_BIN)
	./$(DAC980_BIN)
	./$(HELP_BIN)
	./$(NAME_BIN)
	./$(SPI913_BIN)
	./$(SDLOG_BIN)
	./$(SUSPENDREASON_BIN)
	./$(DAC1057_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
	      $(NAME_BIN) $(SPI913_BIN) $(SDLOG_BIN) $(SUSPENDREASON_BIN) $(DAC1057_BIN) $(GEN_1000)

.PHONY: run clean
