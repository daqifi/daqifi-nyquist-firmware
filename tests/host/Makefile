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

# #1121: SYST:STR:START's SD write-arm ready-wait diagnosis -- the SIBLING of
# SUSPEND_BIN's cascade, in the other file. `grep readyWait firmware/src`
# returns exactly two functions: SCPI_StorageSDBenchmark (SCPIStorageSD.c,
# fixed by #953/#988) and SCPI_StartStreamingClaimed (SCPIInterface.c), which
# was left uncovered. #1121 ports both arms onto it.
#
# Same technique and same reason as SUSPEND_BIN -- SCPIInterface.c is not
# includable on the host (established by SCPI999_BIN, which tried: 42 direct
# includes spanning Harmony PLIB, the USB HS and WINC drivers and FreeRTOS's
# MIPS port layer) -- so the cascade, the poll loop and the reconciliation read
# are re-implemented against injected values and the pre-/post-fix shapes
# compared.
#
# It copies no CONSTANTS, so it needs no equivalent of BENCH_BIN's greps. What
# it copies is the ORDER of the FIVE arms, which is the substance: this site
# has a #498/#851 disk-full arm the SD:BENCHmark twin does not, and both new
# arms go BELOW it for the same reason they go below dir-full -- the
# CHECK_DISK_FULL rejection (sd_card_manager.c:1860-1868) raises the flag AND
# clears `mode`, so `diskFull && tornDown` is the NORMAL shape of that path,
# exactly as `dirFull && tornDown` is of the #690 one. So the guards check
# THAT, in three parts mirroring SUSPEND_BIN's three.
STARTARM_BIN := run_1121_tests

# #1098: the shared-SCPI-response-buffer Take/Give pairing of the four reply
# callbacks that used to format into a stack-local array --
# SCPI_SysLogLevelSet / SysLogLevelGet / SysLogLevelAllSet (SCPIInterface.c)
# and SCPI_StorageSDInfo (SCPIStorageSD.c).
#
# Same technique and same reason as SYSINFO_BIN and STARTARM_BIN: neither file
# is includable on the host (SCPI999_BIN established that for SCPIInterface.c,
# BENCH_BIN for SCPIStorageSD.c), so the control-flow shape of each callback is
# re-implemented against a mock Take/Give/write and the pre-/post-fix shapes
# compared.
#
# It copies ONE constant, SCPI_RESPONSE_BUF_SIZE, pinned by guard 1 below. It
# deliberately does NOT copy LOG_MODULE_COUNT -- every loop assertion is swept
# over counts 0..12 instead, so adding a log module cannot stale this file and
# no grep guard is needed for it.
REPLYBUF_BIN := run_1098_tests

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

# #1039 (PR #1116, split from the parked #1110): the four LOG_E messages
# #1039 shortened, measured against the SAME ceiling $(SDLOG_BIN) measures --
# Logger's real truncation, not an arithmetic estimate of it. Deliberately
# independent of PR #1110's parked tests/host/gen_1039_log_budget.py /
# tools/lint/log_budget.py (13 confirmed defects across two audit rounds per
# PR #1116's own description, while the four message edits themselves drew
# zero findings in either round -- which is why #1116 exists as the
# split-out PR). So unlike $(SDLOG_BIN) above, this target extracts NOTHING
# at build time and generates no header: it hardcodes copies of the four
# shipped format strings (and, as a frozen historical control proving the
# harness has teeth, the four pre-#1039 originals) and reproduces
# LogMessageFormatImpl's truncation against them directly -- the same
# technique $(SDLOG_BIN) uses for its own strings, just without the
# extraction step, which is proportionate to a 62-line message change.
#
# What COULD silently drift is therefore the four hardcoded copies, not a
# generated ceiling -- so the recipe greps each shipped format string's
# literal source lines back out of the three files #1039 touched and fails
# the build if any no longer matches exactly, the same technique
# $(SDLOG_BIN)'s own REASON_* guard above uses for SD_SuspendReasonText()'s
# three return strings. The three Logger.h/.c ceiling lines get the same
# plain grep treatment (restated here rather than shared with $(SDLOG_BIN)'s
# sed extraction, since nothing downstream needs the numbers as a generated
# header).
LOGCEIL_BIN  := run_1039_log_ceiling_tests
LOGCEIL_SRC1 := ../../firmware/src/app_freertos.c
LOGCEIL_SRC2 := ../../firmware/src/services/SCPI/SCPIADC.c
LOGCEIL_SRC3 := ../../firmware/src/services/wifi_services/wifi_manager.c

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
AOUTSAMPLE_SRC := ../../firmware/src/state/data/AOutSample.h

# #1034: DAC_EnsureHardwareInitialized's reinit path never reconciled
# BOARDDATA_AOUT_LATEST, so SOUR:VOLT:LEV? kept confidently reporting a
# pre-power-loss voltage after a power cycle reset every physical output.
DACREINIT_BIN := run_1034_tests

# #1057 (#913's twin) folded dac7718_WaitStat's SPI2 busy-spins onto the same
# bounded-spin-then-vTaskDelay(1) shape #913 gave UserSpi.c's spi_XferByte;
# #1108 then finished what #1056 started and moved dac7718_WaitStat itself
# onto the shared firmware/src/HAL/WaitLoop.h that $(WAITLOOP_BIN) compiles
# for real -- the same move #1056 made for spi_WaitStat/uart_WaitSta/
# i2c_WaitMif. There is no fourth grep-guarded copy left: DAC7718_SRC is now a
# prerequisite of $(WAITLOOP_BIN) below (see the comment on that target), the
# same way USER_SPI_SRC/USER_UART_SRC/USER_I2C_SRC already were. This
# target -- run_1057_tests and the DAC1057_BIN it built -- and
# test_1057_dac7718_wait_stat.c, which re-implemented the loop shape against a
# mock rather than compiling the real one, retired with it.

# PR firmware#1112 round 3 (issue #267): the scan-list enable-flag race.
# MC12b_ComputeScanList() used to read pRt->Data[i].IsEnabled inline, one entry
# at a time, holding nothing, while ADCChanEnableSetClaimed()'s mask form wrote
# up to 16 of those flags one store at a time -- so CONF:CAP:JSON? could report
# a scan list, and per-channel scan_offset_ticks derived from it, for a channel
# combination that was never commanded.
#
# Neither source is host-compilable (MC12bADC.c pulls in Harmony's
# configuration.h/definitions.h and FreeRTOS; SCPIADC.c adds libscpi and the
# whole board graph), so test_1112_scan_list_snapshot_race.c does what #943,
# #953 and #985 do for their own non-includable UUTs: it models the two READ
# shapes and the two WRITE shapes against an injected writer-progress schedule
# and compares their masks.
#
# It copies no constants. What it copies is the SHAPE, and here the shape IS
# the whole fix, on both sides:
#
#   READER -- every IsEnabled read happens inside ONE taskENTER_CRITICAL() /
#   taskEXIT_CRITICAL() pair, and the mask is then built from the resulting
#   local snapshot. The decisive check is that pRt->Data[...] is touched
#   EXACTLY ONCE in the whole function: one live read left behind in the
#   mask-building loop is the defect back, and the modelled test -- which
#   asserts the snapshot shape -- would stay green through it.
#
#   WRITER -- the board-variant switch STAGES into maskTargets[]/maskValues[]
#   outside the section (four staging sites across the three arms: NQ1, NQ3,
#   and the NQ2/legacy default, whose MC12b-public and non-MC12b paths are
#   separate) and one section applies them together. A store written straight
#   through
#   `channelRuntimeConfig->IsEnabled = value;` is the torn writer back, and the
#   test's SNAPSHOT x TORN row proves the reader's section alone does not cover
#   for it.
#
# Also pinned: the FOUR single-channel stores stay OUTSIDE the section. Their
# being unprotected is a claim this fix makes -- one aligned bool store is
# already atomic on PIC32MZ -- not an oversight, so moving them means the claim
# was re-thought and this test's premise needs re-checking.
#
# Comment lines are stripped before counting, because both functions' own
# comments name these identifiers on purpose. A comment restyled so its lines
# do not begin with `*` or `/` therefore fails this guard -- loudly, which is
# the right outcome for a guard whose job is to notice edits to these two
# functions. CRs are stripped too: every firmware source in this repo is CRLF,
# and this Makefile is not.
SCAN1112_BIN := run_1112_tests
MC12BADC_SRC := $(FW_ADC)/MC12bADC.c
SCPIADC_SRC  := ../../firmware/src/services/SCPI/SCPIADC.c

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
# it fires. 90 of the slice's 120 lines are comment, so this will fire on edits
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
START_CLAIMED_SHA := 404e01fdd3b7c9c8eb26cfca959b498ebe9b3e921b77daa0ddd3bca72938d490

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
# The four drivers stay as PREREQUISITES so an edit to one must invalidate
# this binary for `make`'s incremental build to be trustworthy. DAC7718_SRC
# joined in #1108, when dac7718_WaitStat folded onto this same shared header
# instead of keeping its own grep-guarded copy (see the retired comment above
# DAC7718_SRC's definition, above) -- and gets two guards below, because they
# are the two things the retired target checked that compiling WaitLoop.h for
# real cannot. #1114 added a THIRD guarded fact, on the i2c/spi/uart trio:
# that i2c_WaitBudgetSpent calls the shared WaitLoop_BudgetSpent (one grep),
# and that spi_WaitBudgetSpent/uart_WaitBudgetSpent still carry their OWN
# inline '>=' rather than drifting to '>' unnoticed (one grep each) --
# test_1056_wait_loop.c's budget_spent_boundary_is_ge proves the shared
# function's own boundary and nothing about which driver's text still says
# what (worker STEP 3a review, PR #1119).
#
# GUARD 1 (unchanged by #1108's correction): that DAC7718.c itself still
# ROUTES every wait through dac7718_WaitStat rather than a call site reverting
# to bare polling. test_1056_wait_loop.c has no visibility into DAC7718.c's
# text at all -- it only proves the header's shapes -- so without this grep a
# reverted call site would rebuild and pass silently (Qodo /agentic_review,
# PR #1109).
#
# GUARD 2 (added correcting #1109's own regression): that dac7718_WaitStat
# calls WaitLoop_HoistedSpinThenYield specifically, not its RE-spinning
# sibling WaitLoop_SpinThenYield. #1108's first attempt folded DAC7718 onto
# the shape the other three drivers use, silently turning its one register
# read per 1 ms fault-path tick into up to DAC7718_SPI_FAST_SPIN_COUNT reads
# per tick -- a duty-cycle regression #913 exists to prevent (measured +316 B
# vs main; #1109 comment 5697480428). Both functions compile clean, both
# satisfy GUARD 1's call-count, and test_1056_wait_loop.c proves each shape's
# OWN correctness without knowing which one any driver actually calls -- so
# only this grep, naming the specific function, catches a call site silently
# swapped back to the wrong shape.
$(WAITLOOP_BIN): test_1056_wait_loop.c test_framework.h $(WAITLOOP_H) \
                 $(USER_SPI_SRC) $(USER_UART_SRC) $(USER_I2C_SRC) $(DAC7718_SRC) $(RECIPES)
	@test "$$(grep -cF 'dac7718_WaitStat(' $(DAC7718_SRC))" = "7" \
	  || { echo "ERROR: expected exactly 7 occurrences of 'dac7718_WaitStat(' in $(DAC7718_SRC)"; \
	       echo "       (1 definition + 6 call sites -- 3 in the write frame, 3 in the RW=1 NOP"; \
	       echo "       readback), found $$(grep -cF 'dac7718_WaitStat(' $(DAC7718_SRC)). A call"; \
	       echo "       site reverting to a bare register-poll spin would not otherwise be caught"; \
	       echo "       by anything here -- test_1056_wait_loop.c proves WaitLoop.h's shape, not"; \
	       echo "       that DAC7718.c still calls into it."; \
	       exit 1; }
	@grep -qF 'return WaitLoop_HoistedSpinThenYield(dac7718_WaitBitMet' $(DAC7718_SRC) \
	  || { echo "ERROR: dac7718_WaitStat no longer calls WaitLoop_HoistedSpinThenYield in $(DAC7718_SRC)."; \
	       echo "       PR #1109 folded it onto the RE-spinning WaitLoop_SpinThenYield instead -- the"; \
	       echo "       shape the other three drivers use -- which silently turned DAC7718's one"; \
	       echo "       register read per 1 ms fault-path tick into up to DAC7718_SPI_FAST_SPIN_COUNT"; \
	       echo "       reads per tick (measured +316 B vs main; #1109 comment 5697480428). Neither"; \
	       echo "       test_1056_wait_loop.c (proves WaitLoop.h's two shapes, not which one DAC7718.c"; \
	       echo "       calls) nor the guard above (matches either shape's call syntax identically)"; \
	       echo "       can see this coupling -- only this grep does."; \
	       exit 1; }
	@grep -qF 'return WaitLoop_BudgetSpent((uint32_t)(_CP0_GET_COUNT() - w->start),' $(USER_I2C_SRC) \
	  || { echo "ERROR: i2c_WaitBudgetSpent no longer returns WaitLoop_BudgetSpent(...) in $(USER_I2C_SRC)."; \
	       echo "       #1114 decided the I2C budget boundary should be '>=', shared with"; \
	       echo "       spi_WaitStat/uart_WaitSta via that function instead of an inline"; \
	       echo "       comparison of its own. test_1056_wait_loop.c's"; \
	       echo "       budget_spent_boundary_is_ge test proves the shared function's"; \
	       echo "       boundary directly, which says nothing about whether"; \
	       echo "       i2c_WaitBudgetSpent still calls it -- same blind spot as the DAC7718"; \
	       echo "       guards above, same fix: only this grep sees the call site. The pattern"; \
	       echo "       is the exact return statement (function + its first argument"; \
	       echo "       expression), not a bare file-wide occurrence of the function name --"; \
	       echo "       an unrelated comment or a stray call elsewhere in this file cannot"; \
	       echo "       satisfy it (Qodo /agentic_review, PR #1119 round 1)."; \
	       exit 1; }
	@grep -qF 'return (TickType_t)(xTaskGetTickCount() - w->start) >= w->timeoutTicks;' $(USER_SPI_SRC) \
	  || { echo "ERROR: spi_WaitBudgetSpent's inline '>=' comparison is no longer present in $(USER_SPI_SRC)."; \
	       echo "       #1114 decided the I2C budget boundary should align to spi/uart's existing"; \
	       echo "       '>=', NOT that spi/uart migrate onto WaitLoop_BudgetSpent -- WaitLoop.h says"; \
	       echo "       why (a TickType_t-vs-uint32_t width mismatch, latent not live). So this repo"; \
	       echo "       still has THREE copies of the boundary comparison, and"; \
	       echo "       test_1056_wait_loop.c's budget_spent_boundary_is_ge test exercises only"; \
	       echo "       WaitLoop_BudgetSpent -- it cannot see spi_WaitBudgetSpent's own inline text and"; \
	       echo "       would not notice it changing to '>' (worker STEP 3a review, PR #1119). Only"; \
	       echo "       this grep pins spi's operator; it is the exact return statement, not a bare"; \
	       echo "       substring, so an unrelated '>=' elsewhere in the file cannot satisfy it."; \
	       exit 1; }
	@grep -qF 'return (TickType_t)(xTaskGetTickCount() - w->start) >= w->timeoutTicks;' $(USER_UART_SRC) \
	  || { echo "ERROR: uart_WaitBudgetSpent's inline '>=' comparison is no longer present in $(USER_UART_SRC)."; \
	       echo "       Same guard, same reason, as the spi one immediately above -- see it for the"; \
	       echo "       full explanation. uart_WaitSta's copy is textually identical to spi_WaitStat's,"; \
	       echo "       which is why the two greps use the same pattern against different files."; \
	       exit 1; }
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

$(DACREINIT_BIN): test_1034_dac_aout_invalidated_on_reinit.c test_framework.h $(SCPIDAC_SRC) $(AOUTSAMPLE_SRC) $(RECIPES)
	@grep -qE '^[[:space:]]*uint32_t Timestamp;' $(AOUTSAMPLE_SRC) \
	  || { echo "ERROR: AOutSample no longer has a 'uint32_t Timestamp;' field in $(AOUTSAMPLE_SRC)."; \
	       echo "       test_1034's MockAOutSample mirrors this exact field -- re-derive the test."; \
	       exit 1; }
	@test "$$(grep -c '\.Timestamp = SCPIDAC_ValidTimestamp()' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: '.Timestamp = SCPIDAC_ValidTimestamp()' count in $(SCPIDAC_SRC) changed from 2."; \
	       echo "       SCPI_DACVoltageSet's single-channel AND all-channel branches must both stamp"; \
	       echo "       Timestamp on every successful publish -- test_1034's set_shape() models both."; \
	       echo "       If this count changed, one branch may have stopped stamping, or reverted to a"; \
	       echo "       bare xTaskGetTickCount() call (round-1 Qodo finding: tick 0 is a legitimate"; \
	       echo "       return value and collides with the Timestamp==0 unknown sentinel) -- re-derive."; \
	       exit 1; }
	@grep -qF 'return (tick == 0) ? 1u : (uint32_t)tick;' $(SCPIDAC_SRC) \
	  || { echo "ERROR: SCPIDAC_ValidTimestamp's tick-zero substitution is gone from $(SCPIDAC_SRC)."; \
	       echo "       test_1034's set_shape() models this exact remap -- re-derive the test if it moved."; \
	       exit 1; }
	@grep -qF 'pSample->Timestamp < 1' $(SCPIDAC_SRC) \
	  || { echo "ERROR: SCPI_DACVoltageGet's single-channel staleness check ('pSample->Timestamp < 1')"; \
	       echo "       is gone from $(SCPIDAC_SRC). test_1034's get_single_FIXED() mirrors this exact"; \
	       echo "       check -- re-derive the test if this moved."; \
	       exit 1; }
	@grep -qF 'pSample->Timestamp >= 1' $(SCPIDAC_SRC) \
	  || { echo "ERROR: SCPI_DACVoltageGet's all-channel staleness check ('pSample->Timestamp >= 1')"; \
	       echo "       is gone from $(SCPIDAC_SRC). test_1034's get_all_FIXED() mirrors this exact"; \
	       echo "       check -- re-derive the test if this moved."; \
	       exit 1; }
	@grep -qF 'const AOutSample invalidatedSample = {0};' $(SCPIDAC_SRC) \
	  || { echo "ERROR: DAC_EnsureHardwareInitialized's post-reinit invalidation local"; \
	       echo "       ('invalidatedSample') is gone from $(SCPIDAC_SRC). test_1034's"; \
	       echo "       reinit_invalidate_shape() mirrors this loop -- re-derive the test."; \
	       exit 1; }
	@i=`grep -nF 'if (!DAC7718_Init(dacInstanceId, 1)) {' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 v=`grep -nF 'const AOutSample invalidatedSample = {0};' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 p=`grep -nF 'dacHardwareInitialized = true;   // publish READY ...' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 test -n "$$i" && test -n "$$v" && test -n "$$p" \
	   && test "$$i" -lt "$$v" && test "$$v" -lt "$$p" \
	   || { echo "ERROR: #1034's invalidation step is no longer ordered AFTER DAC7718_Init()"; \
	        echo "       succeeds and BEFORE dacHardwareInitialized is published true in"; \
	        echo "       $(SCPIDAC_SRC) (init='$$i' invalidate='$$v' publish='$$p')."; \
	        echo "       Invalidating before the reset actually happens, or after READY is"; \
	        echo "       already published, both reopen the confidently-wrong readback #1034"; \
	        echo "       exists to close -- re-derive the test against the new placement."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(DACREINIT_BIN) test_1034_dac_aout_invalidated_on_reinit.c

# #988 added a FOURTH arm (the torn-down latch) between the live suspend reason
# and the card advisory, so this guard now pins four markers, not three. The
# order it pins IS the design decision the test asserts: dirFull -> suspend ->
# torn-down -> card. In particular the torn-down arm sits BELOW the suspend arm
# on purpose -- the WiFi/quarantine teardown clears `mode` on its way into
# APP_SD_STATE_SUSPENDED, so the latch is always set there too, and hoisting it
# would silence the #953 arm in exactly the case #953 shipped for.
#
# Two more markers pin the LATCH itself, which #988 put inside the poll loop and
# which the test now models as a loop rather than as an injected boolean: it must
# sit after the #690 early-exit and before the arm that consumes it.
#
# WHAT THESE GREPS DO NOT PIN, stated rather than implied: that the latch does
# not also `break`. Adding one would still satisfy every marker here while
# destroying the premise of the "cause already lifted" case -- breaking ends the
# wait at the instant the teardown becomes visible, which is also the instant the
# cause is still visible, so that case could never be reached. There is no cheap
# textual check for it; it is held by the test file's own FIDELITY note and by
# review. (A grep for `break` in the loop was tried and rejected: the #690
# early-exit is a legitimate `break` two lines above, so the check would have to
# know which one it was looking at, which is parsing C -- the thing #1032 exists
# to get this repo out of.)
$(SUSPEND_BIN): test_953_bench_suspend_diagnosis.c test_framework.h $(SD_BENCH_SRC) $(RECIPES)
	@a=`grep -nF 'SD:BENCH refused' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 b=`grep -nF '} else if (why != NULL) {' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 c=`grep -nF 'SD:BENCH - File not ready after timeout' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 test -n "$$a" && test -n "$$b" && test -n "$$d" && test -n "$$c" \
	   && test "$$a" -lt "$$b" && test "$$b" -lt "$$d" && test "$$d" -lt "$$c" \
	   || { echo "ERROR: the SD:BENCHmark file-not-ready cascade in $(SD_BENCH_SRC)"; \
	        echo "       is no longer dirFull -> suspend -> torn-down -> card, or an"; \
	        echo "       arm was reworded, moved or duplicated (markers found at"; \
	        echo "       a='$$a' b='$$b' d='$$d' c='$$c')."; \
	        echo "       test_953_bench_suspend_diagnosis.c models THAT ordering and"; \
	        echo "       its whole point is that the order is the fix -- re-read both"; \
	        echo "       before changing either."; \
	        exit 1; }
	@e=`grep -nF '/* #690: early-exit */' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 f=`grep -nF 'armTornDown = true;' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 test -n "$$e" && test -n "$$f" && test -n "$$d" \
	   && test "$$e" -lt "$$f" && test "$$f" -lt "$$d" \
	   || { echo "ERROR: #988's torn-down latch in $(SD_BENCH_SRC) is gone, moved out"; \
	        echo "       of the ready-wait poll loop, or duplicated (markers found at"; \
	        echo "       early-exit='$$e' latch='$$f' arm='$$d')."; \
	        echo "       test_953_bench_suspend_diagnosis.c models the latch as part of"; \
	        echo "       the LOOP -- that is how it distinguishes 'torn down, no cause"; \
	        echo "       ever observable' from 'torn down by a cause that has since"; \
	        echo "       lifted'. Re-read both before changing either."; \
	        exit 1; }
# #988's RECONCILIATION READ. The loop's latch has a sampling gap it cannot close
# itself -- the body samples `mode` and then yields 10 ms before the `readyWait <
# 500` bound is re-tested, so a teardown in that final delay is observed by
# nobody -- and the fix is one more sample at the head of the !IsWriteReady()
# block. Two properties, both grep-checkable and both load-bearing:
#
#   POSITION. It must sit AFTER the in-loop latch and BEFORE the arm that
#     consumes it. Moved above the loop it would sample the arm this callback
#     just published and could never observe a teardown at all.
#   THE `||`. Pinned by matching the literal `armTornDown = armTornDown ||`
#     rather than just the variable. A plain `armTornDown = (mode != WRITE)`
#     would satisfy a position-only check while overwriting the loop's finding
#     whenever a different caller had re-armed WRITE -- reintroducing #988
#     through its own fix. `the_reconciliation_ors_it_does_not_overwrite_the_
#     latch` is the test that models that mutation.
#
# WHAT THIS DOES NOT PIN: that the read is inside the !IsWriteReady() block
# rather than merely between the two markers, and that the loop's own latch was
# not deleted in favour of it (the guard above holds that one). Line order is all
# grep has; the rest is the comment at the firmware site and review.
	@f=`grep -nF 'armTornDown = true;' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 g=`grep -nF 'armTornDown = armTornDown ||' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SD_BENCH_SRC) | cut -d: -f1`; \
	 test -n "$$f" && test -n "$$g" && test -n "$$d" \
	   && test "$$f" -lt "$$g" && test "$$g" -lt "$$d" \
	   || { echo "ERROR: #988's post-loop reconciliation read in $(SD_BENCH_SRC) is"; \
	        echo "       gone, moved out of the file-not-ready block, or rewritten as"; \
	        echo "       an assignment (markers found at latch='$$f'"; \
	        echo "       reconcile='$$g' arm='$$d')."; \
	        echo "       It must read 'armTornDown = armTornDown || (...)' and sit"; \
	        echo "       between the loop's latch and the arm that consumes it."; \
	        echo "       test_953_bench_suspend_diagnosis.c models it as a separate"; \
	        echo "       step (bench_reconcile) for exactly that reason. Re-read both"; \
	        echo "       before changing either."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(SUSPEND_BIN) test_953_bench_suspend_diagnosis.c

# #1121: the same three guards as $(SUSPEND_BIN), against the OTHER file.
#
# GUARD 1 -- THE FIVE-ARM ORDER. This site's cascade is
# dirFull -> diskFull -> suspend -> tornDown -> generic, and every adjacent
# swap destroys a shipped fix: suspend above either recorded verdict
# re-diagnoses a refusal that a later suspension merely followed (#953's own
# mis-diagnosis, pointed the other way); tornDown above the suspend arm
# silences #953 in #953's headline case (the WiFi/FW-update/quarantine teardown
# publishes BOTH through one app_SDCard_GracefulShutdown() call, so the latch is
# always set there); tornDown above either recorded verdict swallows the #689
# refuse text or #851's `%llu B free < %llu B floor` numbers, because both of
# those refusals clear `mode` as part of themselves.
$(STARTARM_BIN): test_1121_start_streaming_arm_cascade.c test_framework.h \
                $(SCPI_INTERFACE_SRC) $(RECIPES)
	@a=`grep -nF 'SD start refused (#689): ' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 b=`grep -nF '} else if (sd_card_manager_StartupDiskFull()) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 c=`grep -nF '} else if (why != NULL) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 e=`grep -nF 'SD file not ready after %d ms' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 test -n "$$a" && test -n "$$b" && test -n "$$c" && test -n "$$d" && test -n "$$e" \
	   && test "$$a" -lt "$$b" && test "$$b" -lt "$$c" && test "$$c" -lt "$$d" \
	   && test "$$d" -lt "$$e" \
	   || { echo "ERROR: the SYST:STR:START file-not-ready cascade in"; \
	        echo "       $(SCPI_INTERFACE_SRC) is no longer dirFull -> diskFull ->"; \
	        echo "       suspend -> torn-down -> generic, or an arm was reworded,"; \
	        echo "       moved or duplicated (markers found at a='$$a' b='$$b'"; \
	        echo "       c='$$c' d='$$d' e='$$e')."; \
	        echo "       test_1121_start_streaming_arm_cascade.c models THAT"; \
	        echo "       ordering and its whole point is that the order is the fix"; \
	        echo "       -- re-read both before changing either."; \
	        exit 1; }
# GUARD 2 -- THE IN-LOOP LATCH, between the early-exit and the arm that reads
# it. The early-exit marker is matched in FULL, including its
# `|| sd_card_manager_StartupDiskFull()` term: this site breaks on BOTH
# recorded flags where the SD:BENCHmark twin breaks on dir-full alone, and
# `the_loop_early_exits_on_either_recorded_flag` models it that way. Drop
# either term and this grep finds nothing and the build stops.
	@f=`grep -nF 'if (sd_card_manager_StartupDirFull() || sd_card_manager_StartupDiskFull()) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 g=`grep -nF 'armTornDown = true;' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 test -n "$$f" && test -n "$$g" && test -n "$$d" \
	   && test "$$f" -lt "$$g" && test "$$g" -lt "$$d" \
	   || { echo "ERROR: #1121's torn-down latch in $(SCPI_INTERFACE_SRC) is gone,"; \
	        echo "       moved out of the ready-wait poll loop, duplicated, or the"; \
	        echo "       loop's early-exit no longer tests BOTH recorded flags"; \
	        echo "       (markers found at early-exit='$$f' latch='$$g' arm='$$d')."; \
	        echo "       test_1121_start_streaming_arm_cascade.c models the latch as"; \
	        echo "       part of the LOOP -- that is how it distinguishes 'torn down,"; \
	        echo "       no cause ever observable' from 'torn down by a cause that has"; \
	        echo "       since lifted'. Re-read both before changing either."; \
	        exit 1; }
# GUARD 3 -- THE RECONCILIATION READ. Position (after the in-loop latch, before
# the arm that consumes it) and the `||` (matched literally, not just the
# variable name -- a plain `armTornDown = (mode != WRITE)` would satisfy a
# position-only check while overwriting the loop's finding whenever a different
# caller had re-armed WRITE in the meantime). Same two properties, same reasons,
# as the twin's third guard.
#
# WHAT THIS DOES NOT PIN, identically to the twin: that the read is inside the
# !IsWriteReady() block rather than merely between the two markers, and that the
# loop's own latch was not deleted in favour of it (guard 2 holds that one).
# Line order is all grep has; the rest is the comment at the firmware site and
# review.
	@g=`grep -nF 'armTornDown = true;' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 h=`grep -nF 'armTornDown = armTornDown ||' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 d=`grep -nF '} else if (armTornDown) {' $(SCPI_INTERFACE_SRC) | cut -d: -f1`; \
	 test -n "$$g" && test -n "$$h" && test -n "$$d" \
	   && test "$$g" -lt "$$h" && test "$$h" -lt "$$d" \
	   || { echo "ERROR: #1121's post-loop reconciliation read in"; \
	        echo "       $(SCPI_INTERFACE_SRC) is gone, moved out of the"; \
	        echo "       file-not-ready block, or rewritten as an assignment"; \
	        echo "       (markers found at latch='$$g' reconcile='$$h' arm='$$d')."; \
	        echo "       It must read 'armTornDown = armTornDown || (...)' and sit"; \
	        echo "       between the loop's latch and the arm that consumes it."; \
	        echo "       test_1121_start_streaming_arm_cascade.c models it as a"; \
	        echo "       separate step (start_reconcile) for exactly that reason."; \
	        echo "       Re-read both before changing either."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(STARTARM_BIN) test_1121_start_streaming_arm_cascade.c

# #1098 guard helper. Counts LITERAL occurrences of a pattern inside ONE
# function body, bounded by the next column-0 `scpi_result_t` signature -- so
# the counts below are per-callback and cannot be satisfied by one of the ~17
# OTHER Take/Give sites elsewhere in the same file, which a whole-file
# `grep -c` could not distinguish. index() is literal, so `()` and `;` in the
# patterns need no regex escaping.
#   $(1)=source file   $(2)=function name   $(3)=literal pattern
CNT_IN_FN = awk -v fn="$(2)" -v pat='$(3)' \
	'$$0 ~ "^(static )?scpi_result_t " fn "[(]" { f=1; next } \
	 f && /^(static )?scpi_result_t /           { exit } \
	 f && index($$0, pat)                       { n++ } \
	 END { print n+0 }' $(1)

# #1098 guard helper. Prints "<take line> <loop line> <give line>", relative to
# the file, for one function body -- 0 for any marker not found.
#   $(1)=source file   $(2)=function name
ORDER_IN_FN = awk -v fn="$(2)" \
	'$$0 ~ "^(static )?scpi_result_t " fn "[(]" { f=1; next } \
	 f && /^(static )?scpi_result_t /                { exit } \
	 f && index($$0, "SCPI_ResponseBuf_Take()")      { t=NR } \
	 f && index($$0, "LOG_MODULE_COUNT; i++")        { l=NR } \
	 f && index($$0, "SCPI_ResponseBuf_Give();")     { g=NR } \
	 END { print t+0, l+0, g+0 }' $(1)

# #1098: the Take/Give pairing of the four converted reply callbacks.
#
# GUARD 1 -- THE MIRRORED CONSTANT. test_1098 copies SCPI_RESPONSE_BUF_SIZE as
# FW_RESPONSE_BUF_SIZE and drives SCPI_StorageSDInfo's `len >= capacity`
# format-error branch off that exact value, so a stale copy would still build
# and still pass while testing the wrong boundary.
SCPI_BOUNDED_WRITE_H := ../../firmware/src/services/SCPI/ScpiBoundedWrite.h

$(REPLYBUF_BIN): test_1098_scpi_reply_buffer_pairing.c test_framework.h \
                $(SCPI_INTERFACE_SRC) $(SCPI_INTERFACE_H) $(SD_BENCH_SRC) \
                $(SCPI_BOUNDED_WRITE_H) $(RECIPES)
	@grep -qE '^[[:space:]]*#define[[:space:]]+SCPI_RESPONSE_BUF_SIZE[[:space:]]+2048U' \
	     $(SCPI_INTERFACE_H) \
	   || { echo "ERROR: SCPI_RESPONSE_BUF_SIZE is no longer 2048U in"; \
	        echo "       $(SCPI_INTERFACE_H)."; \
	        echo "       test_1098_scpi_reply_buffer_pairing.c mirrors it as"; \
	        echo "       FW_RESPONSE_BUF_SIZE and drives SCPI_StorageSDInfo's"; \
	        echo "       format-error boundary off that exact value -- a stale"; \
	        echo "       copy tests the wrong boundary and still passes."; \
	        exit 1; }
# GUARD 2 -- EXACTLY ONE TAKE AND ONE GIVE PER LOG-LEVEL CALLBACK. This is the
# whole contract (SCPIInterface.h:141-182) reduced to something grep can hold:
# a second Give double-releases somebody else's hold, a missing one wedges
# gScpiRespMutex for every SCPI callback on BOTH transports (they wait
# portMAX_DELAY). Counted per function, so converting a NEIGHBOUR cannot
# satisfy this for a callback that was reverted.
	@for fn in SCPI_SysLogLevelSet SCPI_SysLogLevelGet SCPI_SysLogLevelAllSet; do \
	   t=`$(call CNT_IN_FN,$(SCPI_INTERFACE_SRC),$$fn,SCPI_ResponseBuf_Take())`; \
	   g=`$(call CNT_IN_FN,$(SCPI_INTERFACE_SRC),$$fn,SCPI_ResponseBuf_Give();)`; \
	   test "$$t" = "1" && test "$$g" = "1" \
	     || { echo "ERROR: $$fn in $(SCPI_INTERFACE_SRC) no longer holds the"; \
	          echo "       shared SCPI response buffer exactly once"; \
	          echo "       (found take=$$t give=$$g, both must be 1)."; \
	          echo "       Either it was reverted to a stack-local reply buffer"; \
	          echo "       (#1098, the bug) or its Take/Give no longer pair --"; \
	          echo "       a missing Give wedges gScpiRespMutex for every SCPI"; \
	          echo "       callback on both transports. test_1098 asserts the"; \
	          echo "       1:1 pairing on every return path; re-read both."; \
	          exit 1; }; \
	 done
# GUARD 3 -- ONE HOLD FOR THE WHOLE DUMP, in the two LOOP callbacks. Guard 2
# alone does not pin this: a take/give moved INSIDE the loop still counts 1 and
# 1 in the source while acquiring the mutex LOG_MODULE_COUNT times per query and
# dropping the hold between lines, letting a peer callback interleave its reply.
# Line order is what separates them -- take BEFORE the `for`, give AFTER it,
# which is the SCPI_SysInfoTextGet shape the fix follows.
# test_1098's per_iteration_syslog_get models the mutation this rejects.
	@for fn in SCPI_SysLogLevelGet SCPI_SysLogLevelAllSet; do \
	   set -- `$(call ORDER_IN_FN,$(SCPI_INTERFACE_SRC),$$fn)`; \
	   test "$$1" -gt 0 && test "$$2" -gt 0 && test "$$3" -gt 0 \
	     && test "$$1" -lt "$$2" && test "$$3" -gt "$$2" \
	     || { echo "ERROR: $$fn in $(SCPI_INTERFACE_SRC) no longer takes the"; \
	          echo "       shared buffer BEFORE its LOG_MODULE_COUNT loop and"; \
	          echo "       gives it back AFTER (take='$$1' for='$$2' give='$$3';"; \
	          echo "       0 means the marker is gone)."; \
	          echo "       A take/give moved INSIDE the loop still pairs 1:1 --"; \
	          echo "       guard 2 would not notice -- but blocks on the mutex"; \
	          echo "       once per module and drops the hold between lines."; \
	          echo "       test_1098 models that mutation"; \
	          echo "       (per_iteration_syslog_get) and asserts against it."; \
	          exit 1; }; \
	 done
# GUARD 4 -- SCPI_StorageSDInfo GIVES ON BOTH EXITS. The one site where the
# port is not mechanical: pre-#1098 its `len < 0 || len >= sizeof(result)`
# branch returned holding nothing, so it needed no release; post-fix it runs
# under the hold and GAINS one. Hence ONE take and TWO gives -- a count of 1
# means the error path leaks the mutex on every malformed CID.
# test_1098's leaky_sd_info models exactly that omission.
	@t=`$(call CNT_IN_FN,$(SD_BENCH_SRC),SCPI_StorageSDInfo,SCPI_ResponseBuf_Take())`; \
	 g=`$(call CNT_IN_FN,$(SD_BENCH_SRC),SCPI_StorageSDInfo,SCPI_ResponseBuf_Give();)`; \
	 test "$$t" = "1" && test "$$g" = "2" \
	   || { echo "ERROR: SCPI_StorageSDInfo in $(SD_BENCH_SRC) is no longer one"; \
	        echo "       take with a give on BOTH exits (found take=$$t give=$$g,"; \
	        echo "       must be 1 and 2)."; \
	        echo "       The second give is the format-error branch, which the"; \
	        echo "       pre-#1098 code did not need because it held nothing"; \
	        echo "       there. Drop it and a malformed CID wedges"; \
	        echo "       gScpiRespMutex permanently. test_1098 models that"; \
	        echo "       omission (leaky_sd_info) and asserts against it."; \
	        exit 1; }
# GUARD 5 -- THE THREE MIRRORED TIMING CONSTANTS. test_1098 copies
# SCPI_LOGLEVEL_WRITE_BUDGET_MS as FW_LOGLEVEL_BUDGET_MS, and derives
# MS_PER_STALLED_WRITE (1000) from SCPI_WRITE_MAX_RETRIES x
# SCPI_WRITE_RETRY_DELAY_MS. Drift in any of them silently moves where the
# guards fire while every assertion still passes -- guard2's "3 writes then
# latch" arithmetic is exactly 2000 / 900.
#
# NOTE the trailing [[:space:]]*$$ rather than a bare $$: these sources have
# CRLF line endings, so a `200$$` anchor never matches (the line ends "200\r").
# [[:space:]] covers the CR. A bare $$ here fails CLOSED -- the guard reports
# drift that has not happened -- which is the safe direction, but it would send
# the next reader hunting a constant that never moved.
	@grep -qE '^#define[[:space:]]+SCPI_LOGLEVEL_WRITE_BUDGET_MS[[:space:]]+2000U[[:space:]]*$$' \
	     $(SCPI_INTERFACE_SRC) \
	  && grep -qE '^#define[[:space:]]+SCPI_WRITE_MAX_RETRIES[[:space:]]+200[[:space:]]*$$' \
	     $(SCPI_INTERFACE_SRC) \
	  && grep -qE '^#define[[:space:]]+SCPI_WRITE_RETRY_DELAY_MS[[:space:]]+5[[:space:]]*$$' \
	     $(SCPI_INTERFACE_SRC) \
	   || { echo "ERROR: one of SCPI_LOGLEVEL_WRITE_BUDGET_MS (2000U),"; \
	        echo "       SCPI_WRITE_MAX_RETRIES (200) or"; \
	        echo "       SCPI_WRITE_RETRY_DELAY_MS (5) has changed in"; \
	        echo "       $(SCPI_INTERFACE_SRC)."; \
	        echo "       test_1098 mirrors all three (FW_LOGLEVEL_BUDGET_MS and"; \
	        echo "       MS_PER_STALLED_WRITE) and its guard-2 arithmetic depends"; \
	        echo "       on the exact values. Update both together."; \
	        exit 1; }
# GUARD 5b -- THE TICK MODEL. ScpiBoundedWrite_Decide bounds the hold with
# unsigned 32-bit subtraction and test_1098 asserts that across the wrap at
# 0xFFFFFFFF; both are exact only while TickType_t is 32 bits and one tick is
# one millisecond. Same two pins as test_947, against the same file.
	@grep -qE '^#define[[:space:]]+configTICK_RATE_HZ[[:space:]]+\( \( TickType_t \) 1000 \)[[:space:]]*$$' \
	     $(FREERTOS_CONFIG_SRC) \
	  && grep -qE '^#define[[:space:]]+configTICK_TYPE_WIDTH_IN_BITS[[:space:]]+TICK_TYPE_WIDTH_32_BITS[[:space:]]*$$' \
	     $(FREERTOS_CONFIG_SRC) \
	   || { echo "ERROR: the tick model changed in $(FREERTOS_CONFIG_SRC)"; \
	        echo "       (configTICK_RATE_HZ must be 1000 and"; \
	        echo "       configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS)."; \
	        echo "       ScpiBoundedWrite_Decide's wrap arithmetic and"; \
	        echo "       test_1098's wrap assertions both assume 32-bit"; \
	        echo "       millisecond ticks; at another width the deadline is"; \
	        echo "       wrong in a way no assertion here would show."; \
	        exit 1; }
# GUARD 6 -- THE DUMP LOOPS STAY BOUNDED. Guard 3 pins only WHERE the take and
# give sit; it cannot see whether the writes between them are guarded. Both dump
# callbacks must route every write through SysLogLevelWrite (which latches on a
# short write and on the cumulative deadline) and must contain NO bare
# context->interface->write, which would reintroduce the ~10 s hold #1098's own
# fix created and this bound removes. test_1098 models the unbounded shape
# (new_syslog_get_unbounded) and asserts it reaches ~10 s.
	@for fn in SCPI_SysLogLevelGet SCPI_SysLogLevelAllSet; do \
	   w=`$(call CNT_IN_FN,$(SCPI_INTERFACE_SRC),$$fn,SysLogLevelWrite)`; \
	   b=`$(call CNT_IN_FN,$(SCPI_INTERFACE_SRC),$$fn,context->interface->write)`; \
	   test "$$w" = "1" && test "$$b" = "0" \
	     || { echo "ERROR: $$fn in $(SCPI_INTERFACE_SRC) no longer emits its"; \
	          echo "       dump through the bounded SysLogLevelWrite helper"; \
	          echo "       (found guarded=$$w bare-write=$$b; must be 1 and 0)."; \
	          echo "       A bare context->interface->write inside the hold"; \
	          echo "       restores the ~10 s worst-case stall that blocks every"; \
	          echo "       SCPI callback on BOTH transports. test_1098 models"; \
	          echo "       that unbounded shape and asserts against it."; \
	          exit 1; }; \
	 done
# GUARD 7 -- THE PRODUCTION HELPER USES THE HOST-TESTED DECISION.
#
# This is the guard that makes guards 2-6 mean something. Guard 6 proves the
# call sites NAME SysLogLevelWrite; it cannot see inside it. With the deadline
# and the short-write latch written inline there, deleting the deadline check
# was MEASURED to leave the entire host suite green -- the test was asserting
# against its own re-implementation, so the production logic was unguarded by
# construction (Qodo #1132 "Broken hold bounds can pass tests").
#
# The decision now lives in ScpiBoundedWrite.h, which the test compiles and
# calls directly. So this guard pins the one thing that would silently reopen
# the gap: SysLogLevelWrite reimplementing the decision inline instead of
# delegating to it. Both predicates must appear in its body, and the test must
# include the real header rather than a copy.
#
# Scoped by the helper's own signature -- it is `static void`, so the
# scpi_result_t-anchored CNT_IN_FN cannot reach it; the body runs to the first
# column-0 `}`.
	@d=`awk '/^static void SysLogLevelWrite\(/{f=1} f&&index($$0,"ScpiBoundedWrite_Decide"){n++} f&&/^\}/{exit} END{print n+0}' $(SCPI_INTERFACE_SRC)`; \
	 s=`awk '/^static void SysLogLevelWrite\(/{f=1} f&&index($$0,"ScpiBoundedWrite_IsShort"){n++} f&&/^\}/{exit} END{print n+0}' $(SCPI_INTERFACE_SRC)`; \
	 test "$$d" = "1" && test "$$s" = "1" \
	   || { echo "ERROR: SysLogLevelWrite in $(SCPI_INTERFACE_SRC) no longer"; \
	        echo "       delegates to the host-tested predicates in"; \
	        echo "       ScpiBoundedWrite.h (found Decide=$$d IsShort=$$s;"; \
	        echo "       both must be 1)."; \
	        echo "       Inlining that arithmetic again makes it untestable from"; \
	        echo "       tests/host: SCPIInterface.c is not host-includable, so"; \
	        echo "       the suite would silently go back to asserting against a"; \
	        echo "       COPY of the algorithm. Deleting the real deadline check"; \
	        echo "       was measured to leave the whole suite green that way."; \
	        exit 1; }
	@grep -q '#include "../../firmware/src/services/SCPI/ScpiBoundedWrite.h"' \
	     test_1098_scpi_reply_buffer_pairing.c \
	   || { echo "ERROR: test_1098_scpi_reply_buffer_pairing.c no longer includes"; \
	        echo "       the REAL ScpiBoundedWrite.h. Its deadline and"; \
	        echo "       short-write assertions would then be testing a local"; \
	        echo "       re-implementation again, which is exactly the defect"; \
	        echo "       the extraction closed."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(REPLYBUF_BIN) test_1098_scpi_reply_buffer_pairing.c

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

$(HELP_BIN): test_1004_help_write_abort.c test_framework.h $(SCPI_IFACE_SRC) \
             $(SCPI_BOUNDED_WRITE_H) $(FW_RTOS_CFG) $(RECIPES)
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
	@# #1134 GUARD -- THE PRODUCTION HELPER USES THE HOST-TESTED DECISION.
	@#
	@# This is the guard that makes every OTHER assertion in test_1004 mean
	@# something. The five greps above pin the CONSTANTS the test mirrors;
	@# none of them can see inside ScpiHelpWrite. With the deadline and the
	@# short-write latch written inline there -- which is how it shipped from
	@# #1004 until #1134 -- the deadline check could be DELETED and this
	@# binary still reported 8/8 passing, because mock_help_write carried its
	@# own copy and was only ever checked against itself. That is not a
	@# hypothesis: the identical deletion was MEASURED on SysLogLevelWrite and
	@# left the ENTIRE host suite green (#1098, and the same guard there).
	@#
	@# So this pins the one thing that would silently reopen the gap:
	@# ScpiHelpWrite reimplementing the decision inline instead of delegating
	@# to ScpiBoundedWrite.h. Both predicates must appear in its body, exactly
	@# once each, and the test must include the REAL header rather than a copy.
	@#
	@# Scoped by the helper's own signature -- it is `static void`, and its
	@# body runs to the first column-0 `}`.
	@d=`awk '/^static void ScpiHelpWrite\(/{f=1} f&&index($$0,"ScpiBoundedWrite_Decide"){n++} f&&/^\}/{exit} END{print n+0}' $(SCPI_IFACE_SRC)`; \
	 s=`awk '/^static void ScpiHelpWrite\(/{f=1} f&&index($$0,"ScpiBoundedWrite_IsShort"){n++} f&&/^\}/{exit} END{print n+0}' $(SCPI_IFACE_SRC)`; \
	 test "$$d" = "1" && test "$$s" = "1" \
	   || { echo "ERROR: ScpiHelpWrite in $(SCPI_IFACE_SRC) no longer"; \
	        echo "       delegates to the host-tested predicates in"; \
	        echo "       ScpiBoundedWrite.h (found Decide=$$d IsShort=$$s;"; \
	        echo "       both must be 1)."; \
	        echo "       Inlining that arithmetic again makes it untestable from"; \
	        echo "       tests/host: SCPIInterface.c is not host-includable, so"; \
	        echo "       test_1004 would silently go back to asserting against a"; \
	        echo "       COPY of the algorithm. Deleting the real deadline check"; \
	        echo "       was measured to leave the whole suite green that way."; \
	        exit 1; }
	@grep -q '#include "../../firmware/src/services/SCPI/ScpiBoundedWrite.h"' \
	     test_1004_help_write_abort.c \
	   || { echo "ERROR: test_1004_help_write_abort.c no longer includes"; \
	        echo "       the REAL ScpiBoundedWrite.h. Its deadline and"; \
	        echo "       short-write assertions would then be testing a local"; \
	        echo "       re-implementation again, which is exactly the defect"; \
	        echo "       #1134 closed at this call site."; \
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

# #1039: see the LOGCEIL_BIN comment block above for what this proves and
# why it is deliberately independent of the parked #1110 tooling.
$(LOGCEIL_BIN): test_1039_log_message_ceiling.c test_framework.h \
                $(LOGCEIL_SRC1) $(LOGCEIL_SRC2) $(LOGCEIL_SRC3) $(FW_LOG_H) $(FW_LOG_C) $(RECIPES)
	@grep -qE '^#define[[:space:]]+LOG_MESSAGE_SIZE[[:space:]]+128[[:space:]]*$$' $(FW_LOG_H) \
	  || { echo "ERROR: LOG_MESSAGE_SIZE is no longer '128' in $(FW_LOG_H)."; \
	       echo "       Update FW_LOG_MESSAGE_SIZE in test_1039_log_message_ceiling.c and"; \
	       echo "       re-derive LOG_BUDGET (and every asserted margin) before trusting it."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*size = vsnprintf\(buffer, LOG_MESSAGE_SIZE - 2, format, args\);[[:space:]]*$$' $(FW_LOG_C) \
	  || { echo "ERROR: LogMessageFormatImpl's vsnprintf reserve is no longer"; \
	       echo "       'LOG_MESSAGE_SIZE - 2' in $(FW_LOG_C). Update"; \
	       echo "       FW_LOG_VSNPRINTF_RESERVE in test_1039_log_message_ceiling.c and"; \
	       echo "       re-derive LOG_BUDGET before trusting it."; \
	       exit 1; }
	@grep -qE '^[[:space:]]*size = min\(\(LOG_MESSAGE_SIZE - 3\), size\);[[:space:]]*$$' $(FW_LOG_C) \
	  || { echo "ERROR: LogMessageFormatImpl's clamp reserve is no longer"; \
	       echo "       'LOG_MESSAGE_SIZE - 3' in $(FW_LOG_C) -- this is the reserve that"; \
	       echo "       actually sets what survives. Update FW_LOG_CLAMP_RESERVE in"; \
	       echo "       test_1039_log_message_ceiling.c and re-derive LOG_BUDGET (and every"; \
	       echo "       asserted margin) before trusting it."; \
	       exit 1; }
# MSG1/MSG2's guards used to be two independent anchored grep pipelines
# (opening->middle, middle->mask) not tied to the same line position --
# audit wf_8f9d065d-2ef (CONFIRMED, medium) found that duplicating MSG2's
# middle literal line immediately before its "<mask>." line passes both
# pipelines (the duplicate itself satisfies the middle->mask pipeline)
# while the shipped string grows past the 125-byte budget. check_log_block
# below replaces both with ONE positional exact-block check per message:
# the opening LOG_E(...) line must occur exactly once in the file, then
# the N consecutive lines starting there (N = that message's literal line
# count) are compared to the expected block as a single whole-block string
# equality -- not a per-line or substring containment match, so an insert,
# delete, duplicate, or edit of any one line inside the block fails it.
	@check_log_block() { \
	  f="$$1"; open_re="$$2"; n="$$3"; want="$$4"; label="$$5"; fmtname="$$6"; testfile="$$7"; \
	  count=$$(grep -cE "$$open_re" "$$f"); \
	  if [ "$$count" -ne 1 ]; then \
	    echo "ERROR: $$label's opening LOG_E(...) line in $$f is expected exactly"; \
	    echo "       once (anchored full-line match) but occurs $$count time(s)."; \
	    echo "       A duplicate (or missing) opening line means the positional"; \
	    echo "       block check below cannot trust where $$fmtname starts."; \
	    return 1; \
	  fi; \
	  start=$$(grep -nE "$$open_re" "$$f" | head -1 | cut -d: -f1); \
	  got=$$(sed -n "$${start},$$((start + n - 1))p" "$$f" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$$//'); \
	  got=$$(printf '%s' "$$got" | tr '\n' '\001'); \
	  wantnorm=$$(printf '%s' "$$want" | tr '\n' '\001'); \
	  if [ "$$got" != "$$wantnorm" ]; then \
	    echo "ERROR: $$label in $$f no longer matches $$fmtname in $$testfile exactly"; \
	    echo "       (this compares the whole $$n-line block by position, not a"; \
	    echo "       per-line or substring match -- an insert, delete, duplicate,"; \
	    echo "       or edit of any one literal line trips it). Update the copy"; \
	    echo "       and re-derive its length against LOG_BUDGET."; \
	    return 1; \
	  fi; \
	}; \
	want1=$$(printf '%s\n%s' \
	  'LOG_E("Clock mismatch (#716): PBCLK3 %u Hz, built for %u Hz - "' \
	  '"reprogram via PICkit/IPE, not a firmware update.",'); \
	check_log_block $(LOGCEIL_SRC1) \
	  '^[[:space:]]*LOG_E\("Clock mismatch \(#716\): PBCLK3 %u Hz, built for %u Hz - "[[:space:]]*$$' \
	  2 "$$want1" "the #716 clock-mismatch LOG_E" MSG1_FORMAT test_1039_log_message_ceiling.c \
	  || exit 1; \
	want2=$$(printf '%s\n%s\n%s' \
	  'LOG_E("CONFigure:ADC:CHANnel: channel %d not addressable; "' \
	  '"two-arg form is <channel>,<state>, one-arg form is a "' \
	  '"<mask>.",'); \
	check_log_block $(LOGCEIL_SRC2) \
	  '^[[:space:]]*LOG_E\("CONFigure:ADC:CHANnel: channel %d not addressable; "[[:space:]]*$$' \
	  3 "$$want2" "the not-addressable-channel LOG_E" MSG2_FORMAT test_1039_log_message_ceiling.c \
	  || exit 1
	@grep -qF 'LOG_E("WiFi down and an SD card IS present on the shared bus - likely bus-incompatible; remove it (wiki: SD-Card-Compatibility)");' $(LOGCEIL_SRC3) \
	  || { echo "ERROR: the SD-present LOG_E hint in $(LOGCEIL_SRC3) no longer matches"; \
	       echo "       MSG3_TEXT in test_1039_log_message_ceiling.c. Update the copy and"; \
	       echo "       re-derive its length against LOG_BUDGET."; \
	       exit 1; }
	@grep -qF 'LOG_E("WiFi down but SPI4 probes read CLEAR - remove any SD card; transfer-speed interference is not probe-visible");' $(LOGCEIL_SRC3) \
	  || { echo "ERROR: the SPI4-clear LOG_E hint in $(LOGCEIL_SRC3) no longer matches"; \
	       echo "       MSG4_TEXT in test_1039_log_message_ceiling.c. Update the copy and"; \
	       echo "       re-derive its length against LOG_BUDGET."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(LOGCEIL_BIN) test_1039_log_message_ceiling.c

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

# PR firmware#1112 round 3: build-time guard over BOTH halves of the scan-list
# enable-flag race -- see the comment above $(SCAN1112_BIN)'s definition.
$(SCAN1112_BIN): test_1112_scan_list_snapshot_race.c test_framework.h $(MC12BADC_SRC) $(SCPIADC_SRC) $(RECIPES)
	@body=`sed -n '/^uint32_t MC12b_ComputeScanList(/,/^}/p' $(MC12BADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$body" \
	   || { echo "ERROR: MC12b_ComputeScanList() was not found in $(MC12BADC_SRC),"; \
	        echo "       or no longer opens at column 0 -- the guards below can see"; \
	        echo "       nothing, and a guard that examines nothing must fail."; \
	        exit 1; }; \
	 ne=`printf '%s\n' "$$body" | grep -c 'taskENTER_CRITICAL()'`; \
	 nx=`printf '%s\n' "$$body" | grep -c 'taskEXIT_CRITICAL()'`; \
	 nlive=`printf '%s\n' "$$body" | grep -o 'pRt->Data\[' | wc -l | tr -d ' '`; \
	 test "$$ne" -eq 1 && test "$$nx" -eq 1 && test "$$nlive" -eq 1 \
	   || { echo "ERROR: MC12b_ComputeScanList() in $(MC12BADC_SRC) no longer captures"; \
	        echo "       the enable flags exactly once under exactly one critical"; \
	        echo "       section (taskENTER_CRITICAL=$$ne taskEXIT_CRITICAL=$$nx"; \
	        echo "        pRt->Data[]=$$nlive; want 1 1 1)."; \
	        echo "       More than one pRt->Data[] reference means the mask is being"; \
	        echo "       built from LIVE state again, which IS the #1112 race -- and"; \
	        echo "       test_1112_scan_list_snapshot_race.c models the snapshot"; \
	        echo "       shape, so it would pass regardless."; \
	        exit 1; }; \
	 e=`printf '%s\n' "$$body" | grep -n 'taskENTER_CRITICAL()' | cut -d: -f1`; \
	 s=`printf '%s\n' "$$body" | grep -n 'enabledSnapshot\[i\] = pRt->Data\[i\]\.IsEnabled;' | cut -d: -f1`; \
	 x=`printf '%s\n' "$$body" | grep -n 'taskEXIT_CRITICAL()' | cut -d: -f1`; \
	 u=`printf '%s\n' "$$body" | grep -n 'enabledSnapshot\[i\]' | cut -d: -f1 | awk -v x="$$x" '$$1 > x' | wc -l | tr -d ' '`; \
	 test -n "$$s" && test "$$e" -lt "$$s" && test "$$s" -lt "$$x" && test "$$u" -ge 2 \
	   || { echo "ERROR: MC12b_ComputeScanList() in $(MC12BADC_SRC) is no longer"; \
	        echo "       two-phase. The snapshot copy must sit INSIDE the critical"; \
	        echo "       section and the mask must be built from it AFTERWARDS"; \
	        echo "       (enter='$$e' snapshot='$$s' exit='$$x', $$u snapshot uses"; \
	        echo "        after the section; want enter < snapshot < exit, uses >= 2)."; \
	        exit 1; }
	@wbody=`sed -n '/^static scpi_result_t ADCChanEnableSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$wbody" \
	   || { echo "ERROR: ADCChanEnableSetClaimed() was not found in $(SCPIADC_SRC),"; \
	        echo "       or no longer opens at column 0 -- the guards below can see"; \
	        echo "       nothing, and a guard that examines nothing must fail."; \
	        exit 1; }; \
	 nold=`printf '%s\n' "$$wbody" | grep -o 'channelRuntimeConfig->IsEnabled = value;' | wc -l | tr -d ' '`; \
	 nstage=`printf '%s\n' "$$wbody" | grep -o 'maskTargets\[index\] = channelRuntimeConfig;' | wc -l | tr -d ' '`; \
	 nsingle=`printf '%s\n' "$$wbody" | grep -o 'channelRuntimeConfig->IsEnabled = (param2 > 0);' | wc -l | tr -d ' '`; \
	 test "$$nold" -eq 0 && test "$$nstage" -eq 4 && test "$$nsingle" -eq 4 \
	   || { echo "ERROR: the bulk-mask loop of ADCChanEnableSetClaimed() in"; \
	        echo "       $(SCPIADC_SRC) no longer STAGES its per-channel writes"; \
	        echo "       (direct '= value' stores=$$nold, staged=$$nstage,"; \
	        echo "        single-channel stores=$$nsingle; want 0 4 4)."; \
	        echo "       A direct store is the torn writer back: the reader's own"; \
	        echo "       section then only NARROWS the #1112 window, which is the"; \
	        echo "       SNAPSHOT x TORN row of test_1112_scan_list_snapshot_race.c."; \
	        exit 1; }; \
	 we=`printf '%s\n' "$$wbody" | grep -c 'taskENTER_CRITICAL()'`; \
	 wx=`printf '%s\n' "$$wbody" | grep -c 'taskEXIT_CRITICAL()'`; \
	 napply=`printf '%s\n' "$$wbody" | grep -o 'maskTargets\[index\]->IsEnabled = maskValues\[index\];' | wc -l | tr -d ' '`; \
	 test "$$we" -eq 1 && test "$$wx" -eq 1 && test "$$napply" -eq 1 \
	   || { echo "ERROR: ADCChanEnableSetClaimed() in $(SCPIADC_SRC) no longer applies"; \
	        echo "       the staged mask exactly once under exactly one critical"; \
	        echo "       section (taskENTER_CRITICAL=$$we taskEXIT_CRITICAL=$$wx"; \
	        echo "        staged-apply stores=$$napply; want 1 1 1)."; \
	        exit 1; }; \
	 we=`printf '%s\n' "$$wbody" | grep -n 'taskENTER_CRITICAL()' | cut -d: -f1`; \
	 wx=`printf '%s\n' "$$wbody" | grep -n 'taskEXIT_CRITICAL()' | cut -d: -f1`; \
	 wa=`printf '%s\n' "$$wbody" | grep -n 'maskTargets\[index\]->IsEnabled = maskValues\[index\];' | cut -d: -f1`; \
	 wstage=`printf '%s\n' "$$wbody" | grep -n 'maskTargets\[index\] = channelRuntimeConfig;' | cut -d: -f1 | awk -v e="$$we" '$$1 > e' | wc -l | tr -d ' '`; \
	 wsingle=`printf '%s\n' "$$wbody" | grep -n 'channelRuntimeConfig->IsEnabled = (param2 > 0);' | cut -d: -f1 | awk -v e="$$we" '$$1 > e' | wc -l | tr -d ' '`; \
	 test "$$we" -lt "$$wa" && test "$$wa" -lt "$$wx" \
	   && test "$$wstage" -eq 0 && test "$$wsingle" -eq 0 \
	   || { echo "ERROR: ADCChanEnableSetClaimed() in $(SCPIADC_SRC) no longer keeps"; \
	        echo "       the table lookups out of, and the staged apply inside, its"; \
	        echo "       critical section (enter='$$we' apply='$$wa' exit='$$wx',"; \
	        echo "        $$wstage staging lines and $$wsingle single-channel stores"; \
	        echo "        after the section opens; want enter < apply < exit, 0, 0)."; \
	        echo "       The single-channel stores are OUTSIDE it deliberately -- one"; \
	        echo "       aligned bool store is already atomic on PIC32MZ -- so moving"; \
	        echo "       them means that claim was re-thought, not that this guard"; \
	        echo "       should be relaxed."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(SCAN1112_BIN) test_1112_scan_list_snapshot_race.c

# #1108 RETIRED $(DAC1057_BIN) here (run_1057_tests, built from
# test_1057_dac7718_wait_stat.c, which this same block's history recorded
# across EIGHT prior additive collisions -- fw#961 vs fw#983, fw#913 vs
# fw#1004, fw#913 vs fw#1000, fw#913 vs fw#980, fw#913 vs fw#985, and the
# fw#913-accumulated head vs fw#947). Its five grep guards stood in for a loop
# shape a host build could not compile for real; dac7718_WaitStat now calls
# the shared $(WAITLOOP_H) that $(WAITLOOP_BIN) compiles directly (DAC7718_SRC
# is a prerequisite of that target, see above), so there is no copy left for a
# guard to pin. Same move #1056 made retiring the analogous grep guard that
# used to stand in for spi_WaitStat/uart_WaitSta/i2c_WaitMif.

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

# #1141 (fix/909-bootloader-lower-panel-erase): APP_ProgramHexRecord's
# per-chunk address bounds test in the bootloader's nvm.c. The FIRST
# bootloader coverage in this suite -- everything above tests firmware/src.
#
# nvm.c is not host-compilable: it #includes "peripheral/nvm/plib_nvm.h",
# "system/devcon/sys_devcon.h", "peripheral/int/plib_int.h" and <sys/kmem.h>,
# none of which exist outside the MPLAB X / XC32 install (confirmed
# empirically -- `gcc -c` on it fails immediately on the first missing
# header). So, like SCPIInterface.c and SCPIStorageSD.c above, this test
# re-implements the address-classification SHAPE as a pure function and the
# guards below pin that nvm.c's real branch structure still matches it.
#
# UNLIKE those, the two boundary CONSTANTS are not copied: system_config.h
# (in the same directory) has no #includes at all and is hardware-free, so
# test_1141_hex_record_upper_panel_error.c #includes it -- and nvm.h --
# directly for the real APP_FLASH_BASE_ADDRESS / APP_FLASH_END_ADDRESS /
# APP_FLASH_PFM_END_ADDRESS and the real HEX_RECORD_STATUS enum. Nothing
# firmware-numeric is a copy here.
#
# #909 narrowed APP_FLASH_END_ADDRESS to the lower flash panel so an in-app
# update stops erasing (and so destroying) the NVM settings pages, but left
# APP_ProgramHexRecord's existing bounds test as the ONLY thing standing
# between a record and an unerased write: an address in the upper panel
# (still application code space, 0x9D100000-0x9D1FFFFF) took the exact same
# "out of boundaries, skip it" path as a genuine boot-area/config-word
# address, and PROGRAM_FLASH still ACKed the record. The adversarial audit on
# PR #1141 confirmed this independently from two directions
# (issuecomment-5736996706). The fix adds a THIRD branch, between the write
# and the original skip, so an address still inside the application's 2 MB
# PFM span but above the erased window fails the record
# (HEX_REC_PGM_ERROR) instead of being silently dropped -- while an address
# genuinely outside that span (boot flash / config words) keeps the original
# skip-and-succeed path (#764's contract) untouched.
HEXWIN_BIN := run_1141_tests
BTL_SYSCFG_DIR := ../../bootloader/firmware/src/system_config/usbdevice_pic32mz_ef_sk
BTL_NVM_SRC := $(BTL_SYSCFG_DIR)/framework/bootloader/src/nvm.c
BTL_NVM_H := $(BTL_SYSCFG_DIR)/framework/bootloader/src/nvm.h
BTL_SYSCFG_H := $(BTL_SYSCFG_DIR)/system_config.h

$(HEXWIN_BIN): test_1141_hex_record_upper_panel_error.c test_framework.h \
               $(BTL_NVM_H) $(BTL_SYSCFG_H) $(BTL_NVM_SRC) $(RECIPES)
	@grep -qE '^#define[[:space:]]+APP_FLASH_PFM_END_ADDRESS[[:space:]]+\(APP_FLASH_END_ADDRESS \+ APP_FLASH_UPPER_PANEL_SIZE\)[[:space:]]*$$' $(BTL_SYSCFG_H) \
	  || { echo "ERROR: APP_FLASH_PFM_END_ADDRESS is missing or no longer exactly"; \
	       echo "       '(APP_FLASH_END_ADDRESS + APP_FLASH_UPPER_PANEL_SIZE)' in $(BTL_SYSCFG_H)."; \
	       echo "       This is #1141's fix for #909's silent-drop hazard: without this"; \
	       echo "       constant, nvm.c cannot tell an out-of-panel APPLICATION record from a"; \
	       echo "       genuine boot-area/config-word one, and both were silently dropped while"; \
	       echo "       PROGRAM_FLASH still ACKed. Re-derive"; \
	       echo "       test_1141_hex_record_upper_panel_error.c against whatever replaced it."; \
	       exit 1; }
	@grep -qF 'else if((ProgAddress > (void *)APP_FLASH_END_ADDRESS) && (ProgAddress <= (void *)APP_FLASH_PFM_END_ADDRESS))' $(BTL_NVM_SRC) \
	  || { echo "ERROR: $(BTL_NVM_SRC) no longer tests an out-of-panel application address"; \
	       echo "       against APP_FLASH_PFM_END_ADDRESS before falling through to the"; \
	       echo "       boot-area/config-word skip. This is #1141's fix; without this exact"; \
	       echo "       branch a hand-built hex whose application code exceeds the lower panel"; \
	       echo "       is silently dropped and PROGRAM_FLASH still ACKs it."; \
	       exit 1; }
	@awk '/else if\(\(ProgAddress > \(void \*\)APP_FLASH_END_ADDRESS\)/,/^[[:space:]]*\}/' $(BTL_NVM_SRC) \
	     | grep -qF 'return HEX_REC_PGM_ERROR;' \
	  || { echo "ERROR: #1141's new branch in $(BTL_NVM_SRC) no longer returns"; \
	       echo "       HEX_REC_PGM_ERROR -- the whole point is that PROGRAM_FLASH must fail"; \
	       echo "       the record (no ACK), not silently drop it."; \
	       exit 1; }
	@n=$$(grep -cF 'Out of boundaries' $(BTL_NVM_SRC)); \
	 [ "$$n" = "1" ] \
	  || { echo "ERROR: expected exactly 1 occurrence of 'Out of boundaries' in"; \
	       echo "       $(BTL_NVM_SRC), found $$n. #1141 must leave the ORIGINAL"; \
	       echo "       boot-area/config-word skip branch's behaviour completely unchanged"; \
	       echo "       (#764's contract) -- if it moved, was duplicated, or was removed,"; \
	       echo "       re-verify by hand that a standalone hex's config words are still"; \
	       echo "       silently dropped, not this test."; \
	       exit 1; }
	$(CC) $(CFLAGS) -I$(BTL_SYSCFG_DIR) -I$(BTL_SYSCFG_DIR)/framework/bootloader/src \
	      -o $(HEXWIN_BIN) test_1141_hex_record_upper_panel_error.c

# Every suite, and every one of the eleven binaries. SEVENTH additive
# collision in this block: fw#961 vs fw#983, fw#913 vs fw#1004, fw#913 vs
# fw#1000, fw#913 vs fw#980, and now fw#913 vs fw#985 -- every one of them
# two branches appending a line to these same two lists. Resolved the same
# way every time: keep each side's target rule INTACT and union only `run`
# and `clean`. Dropping a rule loses a suite silently; duplicating one
# silently shadows the other.

# Every suite, and every one of the THIRTEEN binaries. NINTH additive collision
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
#
# Every suite, and every one of the SIXTEEN binaries: main's fifteen plus
# #998's $(START_BIN). #998 (the claim/arm/refusal/poll order of
# SCPI_StartStreamingClaimed()'s SD arm) collided with fw#947, fw#1056, fw#980,
# fw#1004, fw#1000, fw#999, fw#985, fw#1112 and fw#1121 here, and was resolved
# the same way as every earlier collision: keep each side's target rule INTACT
# and union only `run` and `clean`. #971's suite (run_971_tests, PR #976) is
# still landing independently on another branch; when it meets this, same rule.
#
# Every suite, and every one of the SEVENTEEN binaries: the sixteen above plus
# #1039's $(LOGCEIL_BIN) (PR #1116, the host guard for the four LOG_E messages
# shortened under Logger's 125-byte ceiling). TENTH additive collision in this
# block -- #1039/#1116 collided here with fw#998, fw#1112 and fw#1121, all
# three already on main by the time this branch refreshed its base -- resolved
# the same way as every earlier collision: keep each side's target rule INTACT
# and union only `run` and `clean`.
#
# Every suite, and every one of the EIGHTEEN binaries: the seventeen above plus
# #1098's $(REPLYBUF_BIN) (the Take/Give pairing of the four SCPI reply
# callbacks moved off the stack onto the shared response buffer). ELEVENTH
# additive collision in this block -- resolved the same way as every earlier
# one: keep each side's target rule INTACT and union only `run` and `clean`.
#
# Every suite, and every one of the NINETEEN binaries: the eighteen above plus
# #1141's $(HEXWIN_BIN) -- the first bootloader coverage in this suite.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
     $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(LOGCEIL_BIN) $(SCAN1112_BIN) $(STARTARM_BIN) $(START_BIN) \
     $(REPLYBUF_BIN) $(HEXWIN_BIN) $(DACREINIT_BIN)
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
	./$(LOGCEIL_BIN)
	./$(SCAN1112_BIN)
	./$(STARTARM_BIN)
	./$(START_BIN)
	./$(REPLYBUF_BIN)
	./$(HEXWIN_BIN)
	./$(DACREINIT_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
	      $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(GEN_1000) $(LOGCEIL_BIN) $(SCAN1112_BIN) $(STARTARM_BIN) $(START_BIN) \
	      $(REPLYBUF_BIN) $(HEXWIN_BIN) $(DACREINIT_BIN)

.PHONY: run clean
