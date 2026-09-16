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

# #999: CreateSCPIContext() used to hand both the USB and TCP SCPI transport
# the SAME file-scope input-buffer and error-queue arrays. SCPIInterface.c
# itself is NOT includable on the host (same reason as SCPIStorageSD.c
# above), but the vendored libscpi sources that implement the actual
# clobber mechanism (parser.c/fifo.c/error.c + their internal dependents)
# are plain portable C with zero FreeRTOS/Harmony dependency and build
# standalone -- confirmed by libscpi's OWN Makefile, which lists exactly
# these 9 files as its whole static-library SRCS. This target links the
# real production libscpi against a minimal harness mirroring
# CreateSCPIContext's own SCPI_Init() call.
#
# USE_DEVICE_DEPENDENT_ERROR_INFORMATION is forced to 0 to match the
# firmware target (bare-metal SYSTEM_TYPE) rather than the host's
# auto-detected SYSTEM_FULL_BLOWN, which would otherwise change
# sizeof(scpi_error_t) and pull in a malloc/free path the firmware never
# takes.
#
# The test's Part B/D assume CreateSCPIContext's fixed shape (a
# ScpiContextStorage struct, one instance per transport) without being able
# to include the real declaration -- so the grep guards below pin that the
# real sources still have that shape, the same technique BENCH_BIN and
# SUSPEND_BIN use for their own non-includable UUTs. A guard failure means
# the fix's shape moved and this test's premise needs re-checking, not that
# the guard should be relaxed.
#
# An adversarial audit of an earlier version of this target (round 2) proved
# those declaration-only guards insufficient: CreateSCPIContext()'s BODY is
# never actually compiled or linked here (the harness reimplements its SHAPE
# with file-local statics, it never calls the real function), and the
# original guards only grepped for declarations and signatures -- so a
# mutation inside CreateSCPIContext()'s body ("static ScpiContextStorage
# shared; storage = &shared;", reintroducing #999's own aliasing bug)
# changed no declaration, no signature and no caller, and passed every guard
# and the whole suite.
#
# Round 3 (this state) replaced that body-text guard after an INDEPENDENT
# adversarial counter-audit defeated round 2's own three line-based greps
# three separate ways (an aliased pointer whose literal `storage->` text
# survived only in a nearby comment; re-seating `.buffer.data` and calling
# `SCPI_ErrorInit()` a second time AFTER an otherwise-correct SCPI_Init()
# call; splitting the reassignment across two physical lines to dodge a
# single-line regex) and found three false positives against legitimate
# code (a defensive NULL check; a prose comment naming
# "ScpiContextStorage instance"; writing THROUGH the pointer via `*storage =
# ...` rather than reassigning it). See
# check_999_createscpicontext_wiring.sh's own header comment for the full
# account of each and how this version closes them: the function body is
# now bounded to its own closing brace (not "signature to EOF"), comments
# are stripped and the body flattened before any keyword search, the
# wiring check is scoped to the SCPI_Init() call's own argument text rather
# than "anywhere in the body", and two new checks forbid the body from
# touching `.buffer.data` / `error_queue` / calling `SCPI_ErrorInit()`
# directly at all (#999's fix relies solely on SCPI_Init() to own that
# wiring). Mutation-proven (2026-09-11) against all of the above plus two
# more probes (a zero-whitespace reassignment, a direct `error_queue` poke)
# -- 11 cases total, each verified to fail or pass as intended; see the PR
# description for the full matrix. What this script does NOT establish is
# stated in its own header (a `union`- or pointer-arithmetic-based aliasing
# scheme is out of scope for a textual guard) -- read that before trusting
# a green run more than it proves.
#
# Actually linking SCPIInterface.c itself (the real fix) was tried first and
# rejected as impractical, not skipped by default: it has 42 direct
# #includes spanning Harmony PLIB (peripheral/gpio, the USB HS driver, the
# WINC WiFi driver), FreeRTOS (whose portmacro.h is PIC32MZ/MIPS assembly,
# not host-portable), nanopb-generated protobuf headers, and most of
# firmware/src/{HAL,state,services} -- confirmed empirically: a bare `gcc -c`
# of the file fails immediately on a missing FreeRTOS.h, and FreeRTOS.h's own
# portable/MPLAB/PIC32MZ/portmacro.h is not something a host stub can
# meaningfully fake. Stubbing that whole graph would be a second HAL, not a
# test. The textual guard is the documented second-best (per the #1016
# review brief) until a seam makes a real link tractable -- this repo has
# done exactly that seam twice before for a similarly non-includable UUT
# (AD7609Scale.h split out of AD7609.h for #889, JSON_StringEscape.h split
# out of JSON_Encoder.c for #164), which is the natural next step if this
# textual approach needs replacing rather than patching again.
SCPI999_BIN := run_999_tests
LIBSCPI_DIR := ../../firmware/src/libraries/scpi/libscpi
LIBSCPI_SRCS := $(addprefix $(LIBSCPI_DIR)/src/, \
	error.c fifo.c ieee488.c minimal.c parser.c units.c utils.c lexer.c expression.c)
# Wildcarded, not enumerated: the sources above #include both the public
# headers under inc/scpi and the private ones beside them in src, and a
# header-only edit to either set must invalidate this binary for `make`'s
# incremental build to be trustworthy (Qodo /agentic_review, citing #983's
# same-shaped gap). A wildcard means a header ADDED to either directory is
# automatically a prerequisite too, with no separate list to keep in sync.
LIBSCPI_HDRS := $(wildcard $(LIBSCPI_DIR)/inc/scpi/*.h) $(wildcard $(LIBSCPI_DIR)/src/*.h)
SCPI_INTERFACE_H := ../../firmware/src/services/SCPI/SCPIInterface.h
SCPI_INTERFACE_C := ../../firmware/src/services/SCPI/SCPIInterface.c
USBCDC_SRC := ../../firmware/src/services/UsbCdc/UsbCdc.c
WIFI_TCP_SRC := ../../firmware/src/services/wifi_services/wifi_tcp_server.c
# #1056 (was #913): the spin-then-vTaskDelay(1) wait loop UserSpi.c,
# UserUart.c and UserI2c.c all use. It now exists ONCE, in
# firmware/src/HAL/WaitLoop.h, and this test compiles that header directly --
# same category as AD7609_BIN and JSONESC_BIN, not the re-implementing kind.
# So: no UUT copy, no stubs, no grep guards, and NOT $(INCLUDES) -- just
# -I$(FW_HAL) for the real header. There is no copy left to pin.
#
# It replaces run_913_tests / test_913_spi_wait_stat.c, which could only
# re-implement spi_WaitStat's loop line-for-line (none of the three drivers is
# includable on a host: FreeRTOS, Harmony's definitions.h/configuration.h, the
# DIO ownership registry) and pinned a handful of per-file constants with the
# five greps this target used to carry. See the block above $(WAITLOOP_BIN)'s
# rule for what that could and could not establish, and why #1056 ended it.
WAITLOOP_BIN := run_1056_tests
FW_HAL := ../../firmware/src/HAL
WAITLOOP_H := $(FW_HAL)/WaitLoop.h
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

# #1039: four more LOG_E messages cut by the SAME Util/Logger.c message frame
# $(SDLOG_BIN) above measures #1000's site against -- in app_freertos.c,
# SCPIADC.c and wifi_manager.c. Same host-testability situation (none of those
# three is includable here, and neither is Logger.c, which pulls in FreeRTOS),
# so this test also mirrors LogMessageFormatImpl and runs the REAL texts
# through it. Independent of $(SDLOG_BIN)/$(GEN_1000): different sites,
# separate extraction, its own mirror -- a mistake in one does not become the
# other's baseline.
#
# The extraction is a Python generator rather than the sed/grep pipeline
# $(SDLOG_BIN) uses, because three of these four format strings are
# concatenated across physical lines and a shell one-liner cannot reassemble
# one without risking a FRAGMENT -- and a fragment understates a message's
# length, which passes every assertion in the test while checking nothing.
# gen_1039_log_budget.py anchors each site on the CODE that gates it (never on
# the message text: a re-word must re-MEASURE, not fail the build), requires
# the anchor to be unique, refuses a literal run that is not followed by ','
# or ')', and pins each site's conversion signature so a changed substitution
# count re-derives the worst case instead of being measured with stale
# arguments. It exits non-zero with a named diagnostic on any of that.
#
# It deliberately does NOT reuse tools/lint/log_budget.py's parser: that tool
# is the static gate this same PR adds, and it MODELS the truncation. The
# value of this test is that it does not share the model, so the model's
# extractor must not sit on both sides of the check either.
LOG1039_BIN := run_1039_tests
LOG1039_GEN := gen_1039_log_budget.h
FW_APP_SRC  := ../../firmware/src/app_freertos.c
FW_ADC_SCPI := ../../firmware/src/services/SCPI/SCPIADC.c
FW_WIFI_SRC := ../../firmware/src/services/wifi_services/wifi_manager.c
PYTHON      ?= python3

# Every binary depends on this file as well as on its sources. Two of the
# targets below carry GUARDS in their recipes -- greps that fail the build when
# the firmware text they model has moved -- and a guard that is only re-run when
# a source changes is not re-run when the GUARD itself changes. CI is unaffected
# either way (Actions checks out clean, so `make run` always builds from
# nothing); this is for the local incremental build, where the binary outliving
# an edited recipe is exactly how a guard goes quiet. Qodo, PR #983.
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
# the way the deleted test_913_spi_wait_stat.c did (DAC7718 has only the one
# wait shape, shared by all three of its call sites, unlike UserSpi/UserUart/
# UserI2c's three separate shapes). The two firmware constants this depends
# on are a COPY; the recipe greps them and refuses to build on drift.
#
# THIS IS NOW THE LAST COPY OF THAT LOOP. #1056 moved the other three onto the
# shared firmware/src/HAL/WaitLoop.h that $(WAITLOOP_BIN) compiles for real, so
# this target is the only place left where a grep guard stands in for the loop
# itself -- with the limits that block spells out. Folding dac7718_WaitStat
# onto WaitLoop.h is the obvious follow-up and was deliberately left out of
# #1056's scope (it was filed before #1057 existed).
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

# THIS TARGET USED TO CARRY FIVE GREP GUARDS. THEY ARE GONE, AND SO IS WHAT
# THEY GUARDED. They pinned CONSTANTS, and that was all they ever claimed:
# review of #1009 was right that constants alone cannot catch the defect this
# suite exists for -- delete the fresh register read at expiry, move the
# deadline test after the yield, or drop the vTaskDelay(1), and every grep
# still passed while the modelled test stayed green.
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
# the checker was deleted rather than given a sixth generation, and #1056 took
# #889's route instead: make the wait loop host-compilable so the test
# exercises the REAL code, the way AD7609Scale.h was split out so
# test_ad7609_scale.c could include the shipped header directly.
#
# It now does. The loop lives once in $(WAITLOOP_H), the three drivers are thin
# wrappers injecting their own register read / clock / yield, and
# test_1056_wait_loop.c compiles that header -- so the mutations above turn
# this suite RED instead of leaving it green, and there is no copied constant
# left for a grep to pin. Each driver's own spin bound and budget are now
# ordinary tuning values, checked by nothing here, which is the correct amount
# of claim to make about them.
#
# The three drivers stay as PREREQUISITES even though nothing reads them: they
# are the only consumers of the header under test, so an edit to one must
# invalidate this binary for `make`'s incremental build to be trustworthy.
$(WAITLOOP_BIN): test_1056_wait_loop.c test_framework.h $(WAITLOOP_H) \
                 $(USER_SPI_SRC) $(USER_UART_SRC) $(USER_I2C_SRC) $(RECIPES)
	$(CC) $(CFLAGS) -I$(FW_HAL) -o $(WAITLOOP_BIN) test_1056_wait_loop.c

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

SCPI999_WIRING_GUARD := check_999_createscpicontext_wiring.sh
SCPI999_ARG_EXTRACTOR := extract_call_args.pl
SCPI999_ARG_EXTRACTOR_TEST := test_extract_call_args.sh

$(SCPI999_BIN): test_999_scpi_context_storage_isolation.c test_framework.h \
                $(LIBSCPI_SRCS) $(LIBSCPI_HDRS) $(SCPI_INTERFACE_H) $(SCPI_INTERFACE_C) \
                $(USBCDC_SRC) $(WIFI_TCP_SRC) $(SCPI999_WIRING_GUARD) \
                $(SCPI999_ARG_EXTRACTOR) $(SCPI999_ARG_EXTRACTOR_TEST) $(RECIPES)
	@# extract_call_args.pl decides whether the caller checks below and
	@# check_999_createscpicontext_wiring.sh's check (c) can fail at all: they
	@# ask it for a call's own arguments and then require EQUALITY of one whole
	@# argument. A parser that returned the wrong list -- or the whole call as
	@# one argument -- would leave both guards running, green, and vacuous. So
	@# its own self-test runs FIRST, before anything it decides.
	@bash $(SCPI999_ARG_EXTRACTOR_TEST) > /dev/null \
	  || { echo "ERROR: $(SCPI999_ARG_EXTRACTOR_TEST) failed --"; \
	       echo "       extract_call_args.pl is the engine of the CreateSCPIContext()"; \
	       echo "       caller checks and of check_999_createscpicontext_wiring.sh's"; \
	       echo "       check (c). Until it is correct, neither guard can be trusted"; \
	       echo "       to fail. Run 'bash $(SCPI999_ARG_EXTRACTOR_TEST)' for detail."; \
	       exit 1; }
	@grep -qE 'typedef struct \{' $(SCPI_INTERFACE_H) \
	  && grep -qE 'char inputBuffer\[SCPI_INPUT_BUFFER_LENGTH\];' $(SCPI_INTERFACE_H) \
	  && grep -qE 'scpi_error_t errorQueue\[SCPI_ERROR_QUEUE_SIZE\];' $(SCPI_INTERFACE_H) \
	  && grep -qE '\} ScpiContextStorage;' $(SCPI_INTERFACE_H) \
	  || { echo "ERROR: ScpiContextStorage no longer has the {inputBuffer, errorQueue}"; \
	       echo "       shape this test assumes in $(SCPI_INTERFACE_H)."; \
	       echo "       Re-check #999's fix shape before updating this guard."; \
	       exit 1; }
	@grep -qE 'scpi_t CreateSCPIContext\(scpi_interface_t\* interface, void\* user_context,' $(SCPI_INTERFACE_H) \
	  || { echo "ERROR: CreateSCPIContext() in $(SCPI_INTERFACE_H) no longer takes a"; \
	       echo "       storage parameter -- #999's per-transport isolation may have"; \
	       echo "       been reverted."; \
	       exit 1; }
	@grep -qE '^\s*char scpi_input_buffer\[' $(SCPI_INTERFACE_C) \
	  && { echo "ERROR: $(SCPI_INTERFACE_C) still declares the OLD file-scope"; \
	       echo "       scpi_input_buffer[] singleton -- #999's fix removes this."; \
	       exit 1; } || true
	@grep -qE '^\s*scpi_error_t scpi_error_queue_data\[' $(SCPI_INTERFACE_C) \
	  && { echo "ERROR: $(SCPI_INTERFACE_C) still declares the OLD file-scope"; \
	       echo "       scpi_error_queue_data[] singleton -- #999's fix removes this."; \
	       exit 1; } || true
	@grep -qE 'static ScpiContextStorage gUsbScpiStorage;' $(USBCDC_SRC) \
	  || { echo "ERROR: $(USBCDC_SRC) no longer declares its own ScpiContextStorage --"; \
	       echo "       has #999's per-transport storage been reverted to sharing?"; \
	       exit 1; }
	@grep -qE 'static ScpiContextStorage gTcpScpiStorage;' $(WIFI_TCP_SRC) \
	  || { echo "ERROR: $(WIFI_TCP_SRC) no longer declares its own ScpiContextStorage --"; \
	       echo "       has #999's per-transport storage been reverted to sharing?"; \
	       exit 1; }
	@# The two checks below scope to the CreateSCPIContext() CALL SITE's own
	@# argument text, not "does &gUsbScpiStorage/&gTcpScpiStorage appear
	@# anywhere in the file" (that was this guard's shape until an opus
	@# adversarial pass on #1016 found the whole-file version defeatable: a
	@# new shared ScpiContextStorage instance both USB and TCP callers point
	@# their real CreateSCPIContext() call at, while the original
	@# gUsbScpiStorage/gTcpScpiStorage declaration is left dead in the file
	@# with a one-line `(void)&gUsbScpiStorage;` keep-alive -- that keep-alive
	@# satisfies a whole-file grep for the literal substring while the actual
	@# call wires up the shared instance instead, reintroducing #999 exactly.
	@# Requiring exactly one CreateSCPIContext() call per caller (same
	@# technique check_999_createscpicontext_wiring.sh's check (c) uses for
	@# SCPI_Init()) and then checking only that call's own extracted argument
	@# text closes it.
	@#
	@# Round 5 (this state) hardens that call-site scoping twice more, after
	@# a THIRD adversarial audit of #1016 found the round-4 version above
	@# still bypassable two ways:
	@#   F0: `usb_flat`/`tcp_flat` never stripped comments -- just \r removal
	@#       and newline flattening -- so
	@#       `CreateSCPIContext(&iface, ctx, /* &gUsbScpiStorage */ &gSharedScpiStorage);`
	@#       has the real (wrong) storage argument `&gSharedScpiStorage`, but
	@#       the surviving comment text made `grep -q '&gUsbScpiStorage'`
	@#       against the WHOLE call pass anyway. Comments are now stripped
	@#       (both `//` and `/* */`, the same technique
	@#       check_999_createscpicontext_wiring.sh already used for the
	@#       function BODY) before anything is extracted -- and a comma
	@#       inside an unstripped comment would also have corrupted argument
	@#       counting below, so this is load-bearing for extraction
	@#       correctness, not just this one example.
	@#   F1: `grep -q '&gUsbScpiStorage'` against the whole call's text is a
	@#       CONTAINMENT test, not an equality test -- a ternary argument
	@#       (`cond ? &gSharedScpiStorage : &gUsbScpiStorage`) contains the
	@#       literal substring while a true `cond` hands out shared storage
	@#       at runtime. `extract_call_args.pl` now extracts each argument's
	@#       own text by real paren-depth counting, and the checks below
	@#       require EQUALITY of one whole argument to the expected token
	@#       (a ternary's own concatenated text can never equal either of
	@#       its own branches) plus an explicit reject if any argument
	@#       contains `?` at all -- production has no legitimate use for a
	@#       conditional in this call's argument list.
	@# See extract_call_args.pl's own header for the full mechanism.
	@usb_lf=`tr -d '\r' < $(USBCDC_SRC)`; \
	 usb_nolinecomment=`printf '%s\n' "$$usb_lf" | sed -E 's@//.*$$@@'`; \
	 usb_nocomment=`printf '%s' "$$usb_nolinecomment" | perl -0pe 's{/\*.*?\*/}{}gs'`; \
	 usb_flat=`printf '%s' "$$usb_nocomment" | tr '\n' ' '`; \
	 usb_call_count=`printf '%s' "$$usb_flat" | grep -oE 'CreateSCPIContext[[:space:]]*\(' | wc -l`; \
	 test "$$usb_call_count" -eq 1 \
	  || { echo "ERROR: $(USBCDC_SRC) calls CreateSCPIContext() $$usb_call_count times"; \
	       echo "       (expected exactly 1). Re-check #999's fix shape before relaxing"; \
	       echo "       this guard."; \
	       exit 1; }; \
	 usb_args=`printf '%s' "$$usb_flat" | perl extract_call_args.pl CreateSCPIContext`; \
	 test -n "$$usb_args" \
	  || { echo "ERROR: $(USBCDC_SRC)'s CreateSCPIContext() call's arguments could not"; \
	       echo "       be extracted (unbalanced parens/brackets?). Re-check before"; \
	       echo "       relaxing this guard."; \
	       exit 1; }; \
	 printf '%s\n' "$$usb_args" | grep -qF '?' \
	  && { echo "ERROR: $(USBCDC_SRC)'s CreateSCPIContext() call has a"; \
	       echo "       conditional/ternary expression in one of its arguments. A"; \
	       echo "       ternary can make the storage argument resolve to different"; \
	       echo "       (e.g. shared) storage at runtime while still containing the"; \
	       echo "       expected text in an untaken branch -- forbidden outright."; \
	       exit 1; } || true; \
	 printf '%s\n' "$$usb_args" | grep -qxF '&gUsbScpiStorage' \
	  || { echo "ERROR: $(USBCDC_SRC)'s own CreateSCPIContext() call no longer passes"; \
	       echo "       &gUsbScpiStorage as one of its OWN arguments, exactly. A"; \
	       echo "       reference surviving elsewhere in the file (e.g. a dead"; \
	       echo "       keep-alive or a stale comment), or the text merely appearing"; \
	       echo "       inside a larger expression such as a ternary, does not"; \
	       echo "       satisfy this -- one whole argument must equal it."; \
	       exit 1; }
	@tcp_lf=`tr -d '\r' < $(WIFI_TCP_SRC)`; \
	 tcp_nolinecomment=`printf '%s\n' "$$tcp_lf" | sed -E 's@//.*$$@@'`; \
	 tcp_nocomment=`printf '%s' "$$tcp_nolinecomment" | perl -0pe 's{/\*.*?\*/}{}gs'`; \
	 tcp_flat=`printf '%s' "$$tcp_nocomment" | tr '\n' ' '`; \
	 tcp_call_count=`printf '%s' "$$tcp_flat" | grep -oE 'CreateSCPIContext[[:space:]]*\(' | wc -l`; \
	 test "$$tcp_call_count" -eq 1 \
	  || { echo "ERROR: $(WIFI_TCP_SRC) calls CreateSCPIContext() $$tcp_call_count times"; \
	       echo "       (expected exactly 1). Re-check #999's fix shape before relaxing"; \
	       echo "       this guard."; \
	       exit 1; }; \
	 tcp_args=`printf '%s' "$$tcp_flat" | perl extract_call_args.pl CreateSCPIContext`; \
	 test -n "$$tcp_args" \
	  || { echo "ERROR: $(WIFI_TCP_SRC)'s CreateSCPIContext() call's arguments could not"; \
	       echo "       be extracted (unbalanced parens/brackets?). Re-check before"; \
	       echo "       relaxing this guard."; \
	       exit 1; }; \
	 printf '%s\n' "$$tcp_args" | grep -qF '?' \
	  && { echo "ERROR: $(WIFI_TCP_SRC)'s CreateSCPIContext() call has a"; \
	       echo "       conditional/ternary expression in one of its arguments. A"; \
	       echo "       ternary can make the storage argument resolve to different"; \
	       echo "       (e.g. shared) storage at runtime while still containing the"; \
	       echo "       expected text in an untaken branch -- forbidden outright."; \
	       exit 1; } || true; \
	 printf '%s\n' "$$tcp_args" | grep -qxF '&gTcpScpiStorage' \
	  || { echo "ERROR: $(WIFI_TCP_SRC)'s own CreateSCPIContext() call no longer passes"; \
	       echo "       &gTcpScpiStorage as one of its OWN arguments, exactly. A"; \
	       echo "       reference surviving elsewhere in the file (e.g. a dead"; \
	       echo "       keep-alive or a stale comment), or the text merely appearing"; \
	       echo "       inside a larger expression such as a ternary, does not"; \
	       echo "       satisfy this -- one whole argument must equal it."; \
	       exit 1; }
	@bash check_999_createscpicontext_wiring.sh $(SCPI_INTERFACE_C)
	$(CC) $(CFLAGS) -DUSE_DEVICE_DEPENDENT_ERROR_INFORMATION=0 -I$(LIBSCPI_DIR)/inc \
	    -o $(SCPI999_BIN) test_999_scpi_context_storage_isolation.c $(LIBSCPI_SRCS) -lm

# Every suite, and every one of the seven binaries. Third merge around this
# same block (fw#961 JSONESC_BIN, fw#983 SUSPEND_BIN, fw#913 SPI913_BIN --
# since renamed to WAITLOOP_BIN by fw#1056), resolved the same way every time:
# keep each side's target INTACT and merge only `run` and `clean` -- a
# duplicated rule here would silently shadow one of them. $(RECIPES) is main's
# (#983) fix, applied to every target that carries grep guards: a guard re-run
# only when its SOURCE changes is not re-run when the GUARD changes. (#1056
# deleted WAITLOOP_BIN's five guards along with the copy they pinned, so for
# that one $(RECIPES) is now just ordinary recipe-change hygiene.)

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

$(LOG1039_GEN): gen_1039_log_budget.py $(FW_APP_SRC) $(FW_ADC_SCPI) \
                $(FW_WIFI_SRC) $(FW_LOG_H) $(FW_LOG_C) $(RECIPES)
	$(PYTHON) gen_1039_log_budget.py --out $(LOG1039_GEN)

$(LOG1039_BIN): test_1039_log_message_budget.c test_framework.h \
                $(LOG1039_GEN) $(RECIPES)
	$(CC) $(CFLAGS) -o $(LOG1039_BIN) test_1039_log_message_budget.c

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

# Every suite, and every one of the twelve binaries. EIGHTH additive
# collision in this block: fw#961 vs fw#983, fw#913 vs fw#1004, fw#913 vs
# fw#1000, fw#913 vs fw#980, fw#913 vs fw#985, and now the fw#913-accumulated
# head vs fw#947 -- every one of them two branches appending a line to these
# same two lists. Resolved the same way every time: keep each side's target
# rule INTACT and union only `run` and `clean`. Dropping a rule loses a suite
# silently; duplicating one silently shadows the other.
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

# Every suite, and every one of the eleven binaries. SEVENTH additive
# collision in this block: fw#961 vs fw#983, fw#913 vs fw#1004, fw#913 vs
# fw#1000, fw#913 vs fw#980, and now fw#913 vs fw#985 -- every one of them
# two branches appending a line to these same two lists. Resolved the same
# way every time: keep each side's target rule INTACT and union only `run`
# and `clean`. Dropping a rule loses a suite silently; duplicating one
# silently shadows the other.

# Every suite, and every one of the FOURTEEN binaries. TENTH additive collision
# in this block: fw#961 vs fw#983, fw#913 vs fw#1004, fw#913 vs fw#1000,
# fw#913 vs fw#980, fw#913 vs fw#985, fw#999 vs fw#985, and now fw#999 vs
# fw#1057 -- every one of them two branches appending a line to these same two
# lists. Resolved the same way every time: keep each side's target rule INTACT
# and union only `run` and `clean`. Dropping a rule loses a suite silently;
# duplicating one silently shadows the other.
#
# #1026 is the ticket to stop this, and PR #1040 implements it by moving each
# target into tests/host/tests/<issue>-<slug>.mk so a new suite appends to an
# accumulator instead of editing a shared line. Whichever lands first, the rest
# re-express their target as a fragment -- a mechanical move of one recipe,
# which is strictly cheaper than this resolution has been nine times running.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
     $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(DAC1057_BIN) $(LOG1039_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SYSINFO_BIN)
	./$(SUSPEND_BIN)
	./$(DAC980_BIN)
	./$(HELP_BIN)
	./$(WAITLOOP_BIN)
	./$(SDLOG_BIN)
	./$(SCPI999_BIN)
	./$(SUSPENDREASON_BIN)
	./$(DAC1057_BIN)
	./$(LOG1039_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
	      $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(DAC1057_BIN) $(GEN_1000) \
	      $(LOG1039_BIN) $(LOG1039_GEN)

.PHONY: run clean
