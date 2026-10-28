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

# #971: the under-claim settle order of a REFUSED SD arm. Same file, same
# reason as #943 -- SCPIStorageSD.c is not host-includable -- so this is also
# a model rather than the real code, and it also needs a way to fail loudly
# when what it models moves underneath it.
#
# The guard here is a CONTENT HASH of the modelled function, not a grep for
# the property. That is deliberate. PR #976 spent three review rounds finding
# fifteen ways an honest refactor slips past a textual assertion about this
# exact function, and `tools/lint/scpi_sd_arm_path.py` is being narrowed for
# over-claiming on the strength of it; a second, cruder copy of that
# assertion here would give false comfort. A hash claims only "this text is
# unchanged", which is all a build recipe can honestly know, and no refactor
# can slip past it. When it fires, re-read ~8 lines of C against the model in
# test_971_sd_arm_refusal_order.c and update the hash -- it is a tripwire
# asking for a review, not a verdict on whether the code is right.
#
# Hashed: the function from its `static bool <name>(` line to its closing
# brace, with comments removed BY TOKEN and whitespace collapsed. So rewording
# a comment does not fire, and any change to the code does.
#
# BY TOKEN, not by line shape, and that distinction is the whole point (#976
# pre-merge audit). The first version deleted any line STARTING with `/*`, `*`
# or `//`. `*(&cfg->mode) = SD_CARD_MANAGER_MODE_NONE;` is an ordinary C
# pointer-deref assignment that starts with `*`, so it was silently deleted
# before hashing -- adding one left the pin byte-identical. Measured, not
# argued: a probe function containing that line hashed the same with and
# without it. A guard whose whole claim is "any edit to this code forces a
# review" must not have a spelling of edit that it cannot see.
#
# The strip is regex-based and NOT string-aware, so the recipe refuses outright
# if the function ever contains a string literal rather than hashing text it
# might have mangled. It contains none today (pure control flow).
ARM_BIN := run_971_tests
FORCEGATE_BIN := run_756_tests
SDSPI_DRV_SRC := ../../firmware/src/config/default/driver/sdspi/src/drv_sdspi.c
SD_ARM_HELPER := SD_ArmOrRefuseWithCleanup
SD_ARM_HELPER_SHA := 48fc182fdf552b9a5cb860d7dc16821212874f734d9504d68fd110c75c97ee9f

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
#   2. its consume site is, IN THIS ORDER, taskENTER_CRITICAL() ->
#      `if (gFailNextWrite && ...UNMOUNT_DISK) { gFailNextWrite = false;
#      injectWriteFailure = true; }` -> taskEXIT_CRITICAL() -- a positional
#      check (line numbers within the bounded block), not four independent
#      "is this token present anywhere" checks. An adversarial audit on PR
#      #1013 found that the four-independent-greps version could not fail on
#      two real mutations: moving taskEXIT_CRITICAL() before the armed test
#      (every token still present, atomicity destroyed), and flipping the
#      real `injectWriteFailure = true;` to `= false;` (the old guard never
#      even looked for that token);
#   3. `sd_card_manager_Init()` still scrubs it to false (the #409 rail: a
#      reset must always clear an outstanding arm);
#   4. `sd_card_manager_SetFailNextWrite()`'s body is still a PLAIN store
#      with no critical section of its own (the atomicity argument for #981:
#      a store that reads nothing first is not an RMW).
#
# That fixes the STRUCTURAL/textual side, but a textual check -- however
# positional -- is still a PROXY: it proves the shape of the source, not that
# running it does the right thing. $(FAILNEXT_REAL_BIN) below is the other
# half: it compiles and RUNS the whole real SDCardWrite(), verbatim (no
# hand-typed model), and asserts on its actual return value and on whether
# the filesystem write was reached -- which is what catches the
# `injectWriteFailure = true/false` mutation, and the deletion of the payload
# block's `goto __exit;`, BEHAVIOURALLY rather than by noticing a token went
# missing.
FAILNEXT_BIN := run_981_tests
SD_MANAGER_SRC := ../../firmware/src/services/sd_card_services/sd_card_manager.c

# #981 (post-audit): compiles and RUNS the ENTIRE real SDCardWrite() --
# signature through closing brace, extracted verbatim from $(SD_MANAGER_SRC)
# at build time, never hand-typed -- against host stand-ins in
# test_981_sd_failnext_real_consume.head.c (no-op critical-section macros and
# LOG_E, `gFailNextWrite`, the five `gSDCardData` fields the function touches,
# and a MOCK SYS_FS_FileWrite that counts its calls and returns a byte count
# that is never -1). This is a BEHAVIOURAL check, not a textual one:
# $(FAILNEXT_BIN) above proves the source has the right SHAPE, this proves
# that shape, when actually executed, produces the right EFFECT.
#
# It extracts the whole function because extracting less did not work. The
# first revision of this target stopped the extraction at
# `taskEXIT_CRITICAL();` and let the tail file close the spliced fragment with
# a hand-written `return injectWriteFailure;` -- so the payload
# (`if (injectWriteFailure) { LOG_E(...); writeLen = -1; goto __exit; }`, the
# real SYS_FS_FileWrite() it must skip, and the `__exit: return writeLen;`)
# was never compiled or run, and the assertion was on a LOCAL BOOL. Deleting
# ONLY the `goto __exit;` from the real source left all eight binaries green
# while, on hardware, the hook consumed the arm, logged, set writeLen = -1 and
# then fell through into the real write, which succeeded and overwrote it --
# fault injection dead, nothing red. Nothing short of compiling the function's
# actual return path can see that, so now nothing short of it is compiled.
#
# The two observables the tail asserts are the function's real int return and
# the mock's call counter: fired => -1 with ZERO filesystem writes; not fired
# => the mock's byte count with exactly one. Its first case is a CONTROL that
# asserts the write IS reachable, so "zero writes" cannot pass vacuously.
#
# The stand-ins no longer need per-symbol grep guards the way the old bounded
# snippet did: with the whole function spliced in, any symbol it gains that is
# not stubbed is a compile error in the generated file. See the head file for
# the stubs' fidelity notes (one is load-bearing: the mock must never return
# -1), and for why the ORDERING mutation is deliberately NOT this file's job
# (a single-threaded host run cannot observe a concurrency property; that
# stays $(FAILNEXT_BIN)'s).
FAILNEXT_REAL_BIN := run_981_real_consume
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

# #1029: the three sd_card_manager.c LOG_E messages whose tails -- and so
# whose operator-facing remedies -- were cut by the SAME Util/Logger.c 128-byte
# message frame #1000 (above) measures against. Same "not includable on the
# host" situation (sd_card_manager.c pulls in Harmony's file system, FreeRTOS
# and the board graph; Logger.c pulls in FreeRTOS), so this test also mirrors
# LogMessageFormatImpl and measures the REAL message texts through it --
# independently of $(SDLOG_BIN)/$(GEN_1000) above, which is #1000's own site.
# Named SDLOG1029_BIN/SDLOG1029_GEN (not SDLOG_BIN/SDLOG_GEN) specifically to
# avoid colliding with #1000's identically-shaped names when both PRs'
# Makefile hunks landed in the same window (#1025 merged first).
#
# Everything it measures -- LOG_MESSAGE_SIZE, both of Logger.c's reservations,
# the SD directory/bucket constants, sd_BuildBucketPath's format string and the
# three live LOG_E format strings -- is EXTRACTED at build time by
# gen_1029_log_budget.py rather than grepped-and-then-hand-copied. The greps
# above pin a value the test duplicates; a concatenated multi-line C string
# literal cannot be pinned that way (and a "does it still say X" grep on a
# message is the wrong guard anyway -- rewording a message must re-measure it,
# not fail the build). So the generator IS the guard: it anchors each LOG_E on
# the statement that follows it, requires that anchor exactly once, decodes the
# literal group, and round-trips it back into the source bytes -- exiting
# non-zero with a named diagnostic on anything it cannot find, finds twice, or
# cannot reproduce. Nothing is hand-copied except the three PRE-FIX message
# texts, which are frozen in the test (the fix deleted them from the source, so
# they cannot be extracted) and exist to prove the test can fail.
SDLOG1029_BIN := run_1029_tests
SDLOG1029_GEN := gen_1029_log_budget.h
SD_MGR_SRC    := ../../firmware/src/services/sd_card_services/sd_card_manager.c
SD_MGR_HDR    := ../../firmware/src/services/sd_card_services/sd_card_manager.h
PYTHON        ?= python3
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
# WHAT IT SHARES WITH tools/lint/log_budget.py, AND WHAT IT MUST NOT.
#
# ⛔ An earlier revision of this comment said it "deliberately does NOT reuse
# tools/lint/log_budget.py's parser". That stopped being true, on purpose, and
# the comment outlived the decision -- so read the split precisely:
#
#   * the MEASUREMENT is not shared, and must never be. That tool is the
#     static gate, and it MODELS truncation; this test's value is running the
#     real strings through real vsnprintf. The model must not sit on both
#     sides of the check.
#   * the CEILING READER is shared, and must be. `LOG_MESSAGE_SIZE - 2` and
#     the min() clamp are two integers transcribed out of Logger.c, with one
#     right answer, so two readers can only agree or be wrong -- and they
#     silently disagreed twice (codex audit on #1110 r4 and r5).
#
# $(LOG_BUDGET_GATE) is therefore a REAL BUILD INPUT of $(LOG1039_GEN), not a
# neighbouring tool. It is listed as a prerequisite below and in
# host-tests.yml's path filters; `gen_1039_log_budget.py --self-test` asserts
# both of those registrations still exist, so the edge cannot be dropped
# silently the way it was when the seam was first added (r5 finding 4).
LOG1039_BIN := run_1039_tests
LOG1039_GEN := gen_1039_log_budget.h
FW_APP_SRC  := ../../firmware/src/app_freertos.c
FW_ADC_SCPI := ../../firmware/src/services/SCPI/SCPIADC.c
FW_WIFI_SRC := ../../firmware/src/services/wifi_services/wifi_manager.c
# Imported by gen_1039_log_budget.py at run time (see _gate()). Same shape as
# $(CDEF) below: a tools/lint module a host-test target genuinely reads.
LOG_BUDGET_GATE := ../../tools/lint/log_budget.py
PYTHON      ?= python3

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

# Every binary depends on this file as well as on its sources. 19 of the
# targets below carry GUARDS in their recipes -- greps that fail the build when
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

# #924: the per-session SD integrity manifest. Same category as AD7609_BIN and
# JSONESC_BIN -- the pure text operations were split into
# firmware/src/Util/SdManifest.h so this test compiles the REAL header rather
# than a copy of its shape (sd_card_manager.c is not includable on a host:
# Harmony, FreeRTOS, FatFs and the whole board graph). CRC32.c is LINKED IN
# for the same reason it needs no stub -- it includes nothing but its own
# header -- so the accumulation claims are made against the real implementation
# too, not a reference one.
#
# It DOES carry grep guards, and they pin something the two targets above have
# no equivalent of. The suite proves the two pieces are correct; it cannot see
# whether sd_card_manager.c still USES them. Two things would silently void the
# whole feature while leaving every assertion here green:
#   - the running CRC stops being folded in SDCardWrite(), the single funnel
#     every stream-file byte passes through, so the recorded CRC covers only
#     part of the file;
#   - lines stop being rendered through SdManifest_FormatLine, so the format
#     this suite pins is no longer the format written to the card.
# Both are exactly the "test passes against a model the firmware no longer
# has" failure the guards elsewhere in this file exist for, so they fail the
# BUILD rather than being left to the bench.
MANIFEST_BIN := run_924_tests
GATE1124_BIN := run_1124_tests
# #1124 r11 finding 4: the bucket-name prefix the test's guard must match, read
# out of the REAL sd_card_manager.h at parse time. A hand-copied constant that
# drifts keeps PASSING while checking nothing -- and this guard's correctness is
# exactly "the prefix checked here is the prefix buckets are named with". Empty
# is fatal in the recipe below, never a default.
SD_BUCKET_PREFIX := $(shell sed -n 's/^[[:space:]]*#define[[:space:]]\{1,\}SD_CARD_MANAGER_BUCKET_PREFIX[[:space:]]\{1,\}\("[^"]*"\).*/\1/p' ../../firmware/src/services/sd_card_services/sd_card_manager.h | head -1)
SD_MANAGER_SRC := ../../firmware/src/services/sd_card_services/sd_card_manager.c

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
CSV_ENC_SRC   := ../../firmware/src/services/csv_encoder.c
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

# #1154: CONFigure:ADC:chanCALM / chanCALB accepted ANY double for the
# calibration coefficient, including one that overflows to +-inf, and stored
# it with no SCPI error (`CONF:ADC:chanCALB 0,1e400` -> `chanCALB? 0` reads
# `inf`, SYST:ERR? reads clean -- measured on an NQ1, serial
# 7E2837886201026A). See test_1154_adc_cal_coefficient_finite.c's header for
# the fix's scope (setter-only; the magnitude/range question and #908's boot
# re-stamping path are deliberately NOT this guard's job).
CALFINITE_BIN := run_1154_tests

# #1100: DAC_EnsureHardwareInitialized()'s caller-level power check missed the
# 5/10V rail's own commanded enable (PowerWriteVars.EN_5_10V_Val) -- the same
# blind spot #1099 closed one call site over, at the HAL level in
# DAC7718_ReadWriteReg(). A separate binary/target from $(DAC980_BIN) rather
# than a new Part in that file, deliberately: $(DAC980_BIN)'s own source file
# is held by PR #1099's in-flight EN_5_10V_Val work (a new Part there, plus
# RegMockEnv additions near the top), so editing the same lines from a second
# PR would race it. This target only reads $(SCPIDAC_SRC) (grep guards) and
# never touches test_980_dac7718_error_paths.c.
SCPIDAC_RAILGATE_BIN := run_1100_tests

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
# ⚠ THIS VALUE IS THE **CRLF** SLICE, AND THAT IS NOT INCIDENTAL -- DO NOT
# "CORRECT" IT TO THE LF ONE. I did exactly that in this branch and broke CI;
# this comment is here so the next reader does not repeat it.
#
# .gitattributes marks this path `text eol=crlf`, so a compliant checkout --
# including CI's -- writes CRLF, and the recipe below hashes the WORKING TREE.
# Measured, both representations of the same commit:
#
#   CRLF working tree  404e01fdd3b7...  <- this pin, and what CI computes
#   LF   (same bytes)  80d9cebbee1b...
#
# ⛔ THE TRAP: `git ls-files --eol` reports `i/lf w/lf attr/text eol=crlf` in
# some worktrees here -- the attribute says crlf and the file on disk is LF
# anyway. In such a tree the guard fails locally while CI passes, and every
# obvious instinct then makes it worse:
#
#   - Hashing the committed blob (`git show <rev>:<path>`) CANNOT reproduce
#     this pin at ANY revision, because git stores the blob LF-normalised. So
#     a history sweep over blobs returns "the pin never matched once", which is
#     consistent, confident, and measuring the wrong artifact. That is the
#     error I made, and it reads as proof precisely because it is uniform.
#   - Stripping CR before hashing is the same mistake spelled differently.
#   - A local `make run` proves nothing in either direction: green means the
#     tree's endings happen to match the pin's origin, red means they do not.
#
# To re-derive this pin, reproduce CI's representation explicitly rather than
# trusting the checkout:
#
#   git show HEAD:firmware/src/services/SCPI/SCPIInterface.c | sed 's/$/\r/' \
#     | sed -n '/^[[:space:]]*<BEGIN anchor>/,/^[[:space:]]*<END anchor>/p' \
#     | sha256sum
#
# ⚠ The sibling pin SD_ARM_HELPER_SHA is NOT exposed to this, because it goes
# through hash_function.py instead of `sed | sha256sum`. That asymmetry is the
# real defect here: a line-ending-sensitive guard in a repo that rewrites line
# endings on checkout. Routing this pin through the same helper would remove
# the hazard rather than document it, and is the right follow-up -- deliberately
# NOT done inside a merge-refresh commit.
#
# Independently of the hash, the model's three properties do still hold in the
# slice (SCPIInterface.c:5816-5935): refusal at 5823 before the poll at 5935,
# ClearStartupDirFull at 5836 before ReleaseClaim at 5880, and exactly one
# TryClaim in the file, which the anchor-count check above pins separately.
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

$(GATE1124_BIN): test_1124_overwrite_gate.c test_framework.h \
                 $(SD_MANAGER_SRC) $(SD_MGR_HDR) $(SD_BENCH_SRC) $(RECIPES)
	@# ⛔ #1124 L=C PIN 1 -- the gate exists and the opt-in is what it reads.
	@#
	@# ⛔ RULING V=A: the gate reads the SESSION'S authorisation, not the staged
	@# one. The staged flag is cleared when the session arms, so a gate still
	@# reading the config would refuse the session that legitimately asked --
	@# and would let an SD:FILE from the other transport grant or revoke
	@# permission mid-rotation. This arm asserted `overwriteExisting` until r20,
	@# so it encoded the pre-ruling design; it now asserts the session copy.
	@#
	@# ⚠ AND THIS COMMENT LIVES HERE, NOT BESIDE THE ARM IT DESCRIBES. A `@#`
	@# line inside a `\`-continued recipe is NOT a comment -- make hands it to
	@# the shell as part of the command, and the apostrophe in "SESSION'S" then
	@# opens an unterminated quoted string. Twice this session. Comments go
	@# BEFORE the first line of a continued command.
	@# The model's entire premise: nothing is destroyed unless the client asked
	@# for THIS target, so if the gate stops consulting overwriteExisting the
	@# model is describing firmware that no longer exists.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null \
	   | awk '/^static bool sd_TargetSafeToOpenForWrite\(const char \*dataPath\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	   > /tmp/.g1124.$$$$; \
	 test -s /tmp/.g1124.$$$$ \
	  || { echo "ERROR: sd_TargetSafeToOpenForWrite() not found in"; \
	       echo "       $(SD_MANAGER_SRC) (#1124 L=C). test_1124_overwrite_gate.c"; \
	       echo "       models THAT function; with it renamed or gone this pin"; \
	       echo "       measures nothing and must not report a pass."; \
	       rm -f /tmp/.g1124.$$$$; exit 1; }; \
	 grep -q 'overwriteAuthorizedForSession' /tmp/.g1124.$$$$ \
	  || { echo "ERROR: the #1124 gate no longer reads the SESSION's overwrite"; \
	       echo "       authorisation (ruling V=A). Reading the staged flag"; \
	       echo "       instead would refuse the session that asked -- the arm"; \
	       echo "       consumed it -- and would let a concurrent SD:FILE move"; \
	       echo "       the permission mid-rotation."; \
	       rm -f /tmp/.g1124.$$$$; exit 1; }; \
	 rm -f /tmp/.g1124.$$$$
	@# ⛔ PIN 2 -- FAIL CLOSED ON AN UNREADABLE PATH, TWICE: once for the data
	@# file and once for the manifest. r18 finding 3 was exactly the conflation
	@# of "cannot inspect" with "absent", and a two-valued existence test lands
	@# on the PERMISSIVE value when the third state appears. Counted, not merely
	@# present, because one of the two inspections having the guard tells you
	@# nothing about the other.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null \
	   | awk '/^static bool sd_TargetSafeToOpenForWrite\(const char \*dataPath\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	   > /tmp/.g1124b.$$$$; \
	 n=`grep -c 'SYS_FS_ERROR_NO_PATH && e != SYS_FS_ERROR_NO_FILE' /tmp/.g1124b.$$$$`; \
	 test "$$n" -eq 2 \
	  || { echo "ERROR: the #1124 gate has $$n fail-closed inspection guard(s),"; \
	       echo "       expected 2 -- one for the data file and one for the"; \
	       echo "       manifest. A stat failure that is not NO_FILE/NO_PATH is"; \
	       echo "       a FAULT, not an absence (r18 finding 3). Reading it as"; \
	       echo "       absence truncates a file that could not even be looked"; \
	       echo "       at, beside a manifest that still describes it."; \
	       rm -f /tmp/.g1124b.$$$$; exit 1; }; \
	 rm -f /tmp/.g1124b.$$$$
	@# ⛔ PIN 3 -- EVERY refuse reason has a case in the text function.
	@# A reason with no case falls into its `default:` arm and reports "no
	@# refusal recorded" -- a silent WRONG answer about a refusal that did
	@# happen, and the compiler cannot warn because that default exists. The
	@# enum is the authority, so the list is DERIVED from the header rather
	@# than written out here, where it would drift the same way.
	@miss=''; \
	 for r in `sed -n '/typedef enum {/,/} SdWriteRefuseReason;/p' $(SD_MGR_HDR) \
	            | grep -oE 'SD_REFUSE_[A-Z_]+'`; do \
	   grep -q "case $$r:" $(SD_MANAGER_SRC) || miss="$$miss $$r"; \
	 done; \
	 test -z "$$miss" \
	  || { echo "ERROR: SdWriteRefuseReason value(s) with no case in"; \
	       echo "       sd_card_manager_WriteRefuseReasonText():$$miss"; \
	       echo "       They fall into its default: arm and report \"no refusal"; \
	       echo "       recorded\" -- telling a caller nothing happened while a"; \
	       echo "       refusal did. The default exists for SD_REFUSE_NONE, so"; \
	       echo "       the compiler cannot warn about the gap."; exit 1; }
	@# ⛔ PIN 4 -- the #1124 tag LEADS every refusal message. SYSTem:LOG? is the
	@# only way a client can tell this refusal from any other -200, and the
	@# logger clamps at 125 bytes by truncating the TAIL -- so a tag written
	@# anywhere but first can be the part that is lost. conv-ts matches on it.
	@bad=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null \
	       | grep -oE 'LOG_E\("[^"]*SD1124[^"]*"' | grep -vcE 'LOG_E\("\[SD1124\]'`; \
	 test "$$bad" -eq 0 \
	  || { echo "ERROR: $$bad #1124 LOG_E message(s) do not START with the"; \
	       echo "       [SD1124] tag. The logger truncates at 125 bytes from the"; \
	       echo "       TAIL, so a tag that is not first is exactly what gets"; \
	       echo "       lost on the longest paths -- and the tag is the only"; \
	       echo "       thing distinguishing this refusal from every other -200"; \
	       echo "       in SYSTem:LOG?."; exit 1; }
	@# ⛔ PIN 5 -- the opt-in DEFAULTS TO WITHDRAWN on every SD:FILE. If the
	@# parameter stops defaulting to 0, an authorisation granted for one target
	@# silently carries onto the next name the client sets -- and the whole
	@# reason it rides SD:FILE rather than being a standing mode is that it is
	@# bound to the name it authorises.
	@$(CC) -fpreprocessed -E $(SD_BENCH_SRC) 2>/dev/null \
	   | tr '\n' ' ' | tr -s ' ' \
	   | grep -q 'int overwriteParam = 0;' \
	  || { echo "ERROR: SD:FILE's overwrite parameter no longer defaults to 0"; \
	       echo "       (#1124 L=C). A one-argument client would then inherit"; \
	       echo "       whatever the last caller asked for, and a bare SD:FILE"; \
	       echo "       would carry a previous authorisation onto a new target."; \
	       exit 1; }
	@# ⛔ PIN 6 -- A REFUSAL MUST DISARM THE SESSION, and this is a WEDGE guard,
	@# not a tidiness one. sd_card_manager_IsBusyLocked() tests
	@# `mode != MODE_NONE` BEFORE any state, so a refusal that leaves mode ==
	@# WRITE keeps IsBusy() true for ever: the generic failed-open arm unmounts,
	@# re-INITs with the session still armed, re-enters OPEN_FILE and refuses
	@# again. Every SD command is then rejected -- INCLUDING the SD:DELete that
	@# would remove the file and the SD:FILE that would grant the opt-in. The
	@# refusal would wedge the card against its own remedy.
	@# Scoped to the refusal branch itself, because `mode = MODE_NONE` appears
	@# at ten other sites in this file and a file-wide grep could not tell them
	@# apart.
	@awk '/if \(!sd_TargetSafeToOpenForWrite\(gSDCardData.filePath\)\)/{f=1} f{print} f&&/\} else \{/{exit}' \
	     $(SD_MANAGER_SRC) > /tmp/.g1124c.$$$$; \
	 test -s /tmp/.g1124c.$$$$ \
	  || { echo "ERROR: could not extract the #1124 refusal branch from"; \
	       echo "       $(SD_MANAGER_SRC) -- this pin measures nothing."; \
	       rm -f /tmp/.g1124c.$$$$; exit 1; }; \
	 grep -q 'mode = SD_CARD_MANAGER_MODE_NONE' /tmp/.g1124c.$$$$ \
	  || { echo "ERROR: the #1124 refusal no longer disarms the session"; \
	       echo "       (mode -> MODE_NONE). IsBusyLocked() tests mode BEFORE"; \
	       echo "       any state, so the card stays BUSY for ever and every SD"; \
	       echo "       command is refused -- including the SD:DELete and the"; \
	       echo "       SD:FILE opt-in that are the only ways out. A refusal"; \
	       echo "       that wedges the card against its own remedy is worse"; \
	       echo "       than the truncation it prevents."; \
	       rm -f /tmp/.g1124c.$$$$; exit 1; }; \
	 rm -f /tmp/.g1124c.$$$$
	@# ⛔ PIN 7 -- r19 finding 1: ONE derivation for the gate AND creation.
	@# The gate stripped a subdirectory while sd_OpenSessionManifest did not, so
	@# SD:FILE "sub/exp.csv" made the gate ask about <dir>/exp.csv.mfst -- a
	@# different capture's sidecar. r19's body claimed one resolved path was
	@# used for both; it was not, so this pin asserts the structure instead of
	@# the intention. Both callers must go through sd_SessionManifestPath, and
	@# NEITHER may build a manifest name itself.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null`; \
	 n=`printf '%s' "$$pp" | grep -c 'sd_SessionManifestPath('`; \
	 test "$$n" -ge 3 \
	  || { echo "ERROR: sd_SessionManifestPath() is referenced $$n time(s),"; \
	       echo "       expected >= 3 (the definition, the gate, and"; \
	       echo "       sd_OpenSessionManifest). Fewer means a caller has gone"; \
	       echo "       back to building the manifest name itself, which is"; \
	       echo "       r18/r19 finding 1: the gate and the creation path"; \
	       echo "       disagreeing about which sidecar is in question."; exit 1; }
	@# ⛔ PIN 8 -- r19 finding 2: an opt-in on a part of a RECORDED SET is
	@# refused, UNCONDITIONALLY. SD:FILE "exp-1.csv",1 truncates a part that a
	@# previous session recorded in exp.csv.mfst, and that manifest is not at
	@# this target's manifest name, so nothing else in the gate sees it. The
	@# client authorised overwriting exp-1.csv; they did not authorise
	@# invalidating a base they may never have named.
	@#
	@# ⚠ THE INVARIANT IS THE CONDITION, NOT THE POSITION. My first version of
	@# this pin asserted the check appeared BEFORE the opt-in was honoured, and
	@# it fired on correct code: the check sits after the existence branch and
	@# refuses anyway, because its condition carries no opt-in term at all.
	@# Order is irrelevant to an unconditional refusal; what would break it is a
	@# conjunction. So this pins the exact condition -- the predicate alone --
	@# on the flattened text, so a reflow is not an input.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr '\n' ' ' | tr -s ' '`; \
	 case "$$pp" in \
	   *"if (sd_NameIsRotationPartOfExistingManifest(gpSDCardSettings->file)) {"*) ;; \
	   *) echo "ERROR: the #1124 gate does not refuse a recorded set's part on"; \
	      echo "       the predicate ALONE (r19 finding 2). Either the check is"; \
	      echo "       gone, or its condition gained a term -- most likely an"; \
	      echo "       opt-in conjunction, which is exactly what must NOT gate"; \
	      echo "       it: the opt-in cannot authorise invalidating a manifest"; \
	      echo "       whose base the client never named."; \
	      exit 1;; \
	 esac
	@# ⛔ PIN 9 -- r19 finding 4 / RULING V=A: an SD:DIR change WITHDRAWS the
	@# authorisation, in the same critical section as the directory write. The
	@# authorisation is bound to the RESOLVED target; without this, a grant
	@# under one directory silently followed SD:DIR onto another directory's
	@# file. Checked on the flattened text so where the statement wraps is not
	@# an input.
	@$(CC) -fpreprocessed -E $(SD_BENCH_SRC) 2>/dev/null | tr '\n' ' ' | tr -s ' ' \
	   | grep -q 'overwriteExisting = false; } taskEXIT_CRITICAL' \
	  || { echo "ERROR: SD:DIR does not withdraw the overwrite authorisation in"; \
	       echo "       the same critical section as the directory write"; \
	       echo "       (#1124 r19 finding 4). A grant made under one directory"; \
	       echo "       would follow an SD:DIR onto a file the client never"; \
	       echo "       named, and a reader could observe the new directory"; \
	       echo "       still carrying the old authorisation."; exit 1; }
	@# ⛔ PIN 10 -- RULING V=A: the authorisation is CONSUMED at the arm, as one
	@# atomic transition. Two steps would be the #1182 read-modify-write race.
	@# Both halves are asserted -- the session copy taken AND the staged flag
	@# cleared -- because one without the other is either an authorisation that
	@# never expires or one that never applies.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr '\n' ' ' | tr -s ' '`; \
	 case "$$pp" in \
	   *"gSDCardData.overwriteAuthorizedForSession = pSettings->overwriteExisting; pSettings->overwriteExisting = false;"*) ;; \
	   *) echo "ERROR: the #1124 overwrite authorisation is not consumed as one"; \
	      echo "       atomic transition at the arm (ruling V=A). The session"; \
	      echo "       copy must be taken and the staged flag cleared together;"; \
	      echo "       split, a concurrent SD:FILE is lost between them, or an"; \
	      echo "       authorisation the task already took is re-granted."; \
	      exit 1;; \
	 esac
	$(CC) $(CFLAGS) -o $(GATE1124_BIN) test_1124_overwrite_gate.c

$(MANIFEST_BIN): test_924_sd_manifest.c test_framework.h $(FW_UTIL)/SdManifest.h \
                 $(FW_UTIL)/CRC32.h $(FW_UTIL)/CRC32.c $(SD_MANAGER_SRC) \
                 $(SD_BENCH_SRC) $(SD_MGR_HDR) $(RECIPES)
	@grep -q 'CRC32_Update(gSDCardData.fileCrcRunning' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer folds the write-path running"; \
	       echo "       CRC (expected 'CRC32_Update(gSDCardData.fileCrcRunning')."; \
	       echo "       #924's manifest records a CRC accumulated as the bytes are"; \
	       echo "       WRITTEN; without that fold the recorded value covers only"; \
	       echo "       part of each file and test_924_sd_manifest.c can no longer"; \
	       echo "       say anything about it. Re-derive both before proceeding."; \
	       exit 1; }
	@grep -q 'SdManifest_FormatLine' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer renders manifest lines through"; \
	       echo "       SdManifest_FormatLine, so the line format pinned by"; \
	       echo "       test_924_sd_manifest.c is not the one written to the card."; \
	       exit 1; }
	@# ⛔ #1124 r16: THE `SdManifest_RelativeName` POSITIVE GUARD IS DELETED,
	@# because it had become a pin satisfied ENTIRELY BY PROSE. r13 retired the
	@# helper (operator item 8 = B: record the FULL path, so retrieval strips
	@# once instead of twice). The only remaining occurrence of the name in
	@# this file is the sentence in the block comment that says it is gone --
	@# MEASURED: 1 raw grep hit, 0 occurrences in `cc -fpreprocessed -E`
	@# output. So the guard passed over a call that no longer exists, which is
	@# the exact failure mode a positive pin has.
	@#
	@# It also CONTRADICTED the negative pin further down, which asserts on the
	@# preprocessed text that SdManifest_RelativeName( is never called again.
	@# Two pins demanding opposite things both passed, one of them on a
	@# comment. The negative one encodes the ruling and stays; this one goes.
	@grep -q 'SdManifest_PathsEqualCaseInsensitive' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer compares the manifest path against"; \
	       echo "       the session's own stream file through"; \
	       echo "       SdManifest_PathsEqualCaseInsensitive (#924 post-merge audit,"; \
	       echo "       defect 2). FatFs's own duplicate-open lock (FF_FS_LOCK) is"; \
	       echo "       case-insensitive; a plain strcmp here misses a collision FatFs"; \
	       echo "       itself will refuse, silently losing the whole session's"; \
	       echo "       integrity records with no diagnostic."; \
	       exit 1; }
	@#1124: every path construction must go through the ONE canonicalising
	@# accessor, so no site can emit "dir//file" when the configured directory
	@# ends in '/'. KEYED ON THE FORMAT PREFIX, and read from the PREPROCESSED
	@# translation unit, for two measured reasons:
	@#   - an OPERAND key cannot work. `gpSDCardSettings->directory` sits on the
	@#     CONTINUATION line at 4 of the 8 sites, and 2 more never name it at all
	@#     (they receive a `directory` parameter), so an identifier grep found
	@#     2 of 8. `cc -E` does NOT fix that: it resolves the semantics and
	@#     PRESERVES THE GEOMETRY -- a newline inside parens is not a line
	@#     continuation and is never joined by any text pass.
	@#   - the FORMAT literal, unlike the operand, is on the snprintf( line at
	@#     every site. That is the one textual property that survives
	@#     continuation lines, so it is what this keys on.
	@# `-fpreprocessed` is here only to strip COMMENTS (verified: an operand
	@# inside a comment contributes 0), so prose quoting a format cannot count.
	@# ⛔ #1124: 9 -> 8 -> 9 -> 8 across r17, r18 and r20. Every movement is kept
	@# because the totals coincide and the sites do not:
	@#   -1 r20  the gate's TWO constructions and sd_OpenSessionManifest's ONE
	@#           collapsed into sd_SessionManifestPath(), the single derivation
	@#           both now call -- which is the structural form of r18/r19
	@#           finding 1, where the gate stripped a subdirectory and the
	@#           creation path did not.
	@#   +1 r20  sd_NameIsRotationPartOfExistingManifest() builds the OWNING
	@#           base's manifest name, to refuse an opt-in on a part of a
	@#           recorded set (r19 finding 2).
	@# Net -1, and the two counters still move together, which is the property
	@# this pin exists for.
	@#   -1 r17  the FALLBACK name construction went with the two-candidate
	@#           scheme.
	@#   +1 r18  sd_TargetSafeToOpenForWrite() builds TWO names, not one: the
	@#           TARGET's manifest and this session's LIVE manifest, so it can
	@#           tell them apart and never delete its own.
	@# Both join through the same accessor and the same "%.*s/" format, which is
	@# why the two counts move together; a new site moving only ONE of them is
	@# the defect this pin exists to catch.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null > /tmp/.sd1124_pp.$$$$; \
	 raw=`grep -c '"%s/' /tmp/.sd1124_pp.$$$$`; \
	 canon=`grep -cF '"%.*s/' /tmp/.sd1124_pp.$$$$`; \
	 acc=`grep -c 'SdManifest_CanonicalDirLen(' /tmp/.sd1124_pp.$$$$`; \
	 rm -f /tmp/.sd1124_pp.$$$$; \
test "$$raw" -eq 0 && test "$$canon" -eq 8 && test "$$acc" -eq 8 \
	  || { echo "ERROR: $(SD_MANAGER_SRC)'s path construction is no longer"; \
	       echo "       uniformly canonical (#1124)."; \
	       echo "         raw \"%s/ joins:        $$raw (must be 0)"; \
	       echo "         canonical \"%.*s/ joins: $$canon (expected 9)"; \
	       echo "         accessor calls:        $$acc (expected 9)"; \
	       echo "       A raw \"%s/ join emits 'dir//file' whenever the configured"; \
	       echo "       directory ends in '/', which is reachable from config alone."; \
	       echo "       Route the new site through SdManifest_CanonicalDirLen() and"; \
	       echo "       build it with \"%.*s/\", or re-derive these counts if a site"; \
	       echo "       was deliberately added or removed."; \
	       exit 1; }
	@# The two sites a FORMAT key is structurally blind to, pinned BY NAME:
	@# both write a bare "%.*s" with no separator, so no join-shaped pattern can
	@# see them -- and both are PROPAGATION points. sd_BuildBucketPath's
	@# bucket==0 arm copies the directory into bucketPath, which generateFilename
	@# then joins onto without ever naming the settings field; the walk root
	@# seeds every deeper path the directory listing builds.
	@# ⛔ #1124 r8 finding 1: SdManifest.h did not COMPILE under -Wall -Werror.
	@# A nested `/`+`*` inside a comment is -Wcomment, which is -Werror in the
	@# firmware build -- so the header broke that build while this host suite
	@# stayed green, because the suite never compiled it under those flags.
	@#
	@# ⭐ MEASURED, not inferred, and the differential is why it is load-bearing:
	@# a clean build of the FIXED commit cannot distinguish "the fix closed a
	@# break" from "there was never a break", so both legs were run with the
	@# real compiler -- XC32 v4.60 (pic32m-gcc 8.3.1), -Wall -Werror
	@# -fsyntax-only -mprocessor=32MZ2048EFM144, same flags, same minute:
	@#     eef645af9 (pre-fix)  rc=1  SdManifest.h:329:9: error: "/*" within
	@#                                comment [-Werror=comment]
	@#     c4b01b04a (post-fix) rc=0  clean
	@# -Wcomment was separately confirmed to be inside XC32's OWN -Wall, rather
	@# than assuming gcc 8.3 matches the host's gcc 13. That agreement across
	@# both compilers, on this warning class, is the ONLY thing that licenses
	@# the cheap host check below as a proxy for the expensive one -- the proxy
	@# can claim no more than the classes it has been measured to agree on.
	@# A bench-free lane can run that check itself: xc32-gcc is at
	@# /opt/microchip/xc32/v4.60/bin/ on this station, no board required.
	@#
	@# The warning was seen and
	@# dismissed as pre-existing; the baseline checked was MY BRANCH's HEAD,
	@# which already contained the PR's own earlier commits. Against origin/main
	@# the header does not exist at all. One command, no board, ~1 s:
	@# ⛔ THIS CHECK REPORTS THREE STATES, NEVER TWO. `-fsyntax-only` on a
	@# firmware source that includes xc.h (the XC32 device header, absent from a
	@# host gcc) ABORTS at line 1. Collapsing that into "not an error" makes the
	@# check vacuous while it prints success -- measured on another row, where it
	@# reported ok for five files that had every one aborted on a missing
	@# include. An ABORTED compile is not evidence of a clean one.
	@#
	@# It is a REAL check here only because SdManifest.h is self-contained. If
	@# that stops being true the check silently stops checking, so the abort is
	@# detected and FAILS rather than passing. sd_card_manager.c is deliberately
	@# NOT compiled this way; the honest gate for the .c is an XC32 build.
	@printf '#include "SdManifest.h"\nint main(void){return 0;}\n' > /tmp/.sd1124_hdr.$$$$.c; \
	 $(CC) -Wall -Werror -fsyntax-only -I$(FW_UTIL) /tmp/.sd1124_hdr.$$$$.c \
	   >/tmp/.sd1124_hdr.$$$$.err 2>&1; \
	 rc=$$?; \
	 aborted=`grep -c 'No such file or directory' /tmp/.sd1124_hdr.$$$$.err`; \
	 if [ "$$aborted" -ne 0 ]; then \
	   echo "ERROR: the -Wall -Werror check on $(FW_UTIL)/SdManifest.h ABORTED on a"; \
	   echo "       missing include -- it compiled nothing and proves nothing."; \
	   echo "       This check is only valid while that header is SELF-CONTAINED."; \
	   echo "       Something now pulls in a device header (xc.h or similar):"; \
	   grep 'No such file or directory' /tmp/.sd1124_hdr.$$$$.err | sed 's/^/         /' | head -4; \
	   echo "       Either keep the header self-contained, or replace this gate"; \
	   echo "       with an XC32 build -- do NOT let it report success."; \
	   rm -f /tmp/.sd1124_hdr.$$$$.c /tmp/.sd1124_hdr.$$$$.err; exit 1; \
	 elif [ "$$rc" -ne 0 ]; then \
	   echo "ERROR: $(FW_UTIL)/SdManifest.h does not compile under -Wall -Werror,"; \
	   echo "       which is what the firmware build uses -- so this header breaks"; \
	   echo "       the firmware build even while this suite is green:"; \
	   sed 's/^/         /' /tmp/.sd1124_hdr.$$$$.err | head -6; \
	   rm -f /tmp/.sd1124_hdr.$$$$.c /tmp/.sd1124_hdr.$$$$.err; exit 1; \
	 fi; \
	 rm -f /tmp/.sd1124_hdr.$$$$.c /tmp/.sd1124_hdr.$$$$.err
	@# ⛔ #1124 r8 finding 2: C's invariant is about every USE of the configured
	@# directory, and the count arm above enumerates only JOINS. SdManifest_
	@# RelativeName SLICES it -- strlen + strncmp, no format -- so no join-keyed
	@# check can see it, and with a raw strlen it broke the MOMENT construction
	@# became canonical, because the doubled separator had been paying for the
	@# un-stripped length. Two defects cancelling; fixing one exposed the other.
	@#
	@# So: an ALLOWLIST BY COUNT over every raw read of the field. A new read
	@# changes the number and forces the author to classify it here. The 24 in
	@# the current head, all read and classified once:
	@#   4  the accessor's own argument                              canonical
	@#   8  "%.*s/" join operands                                    canonical
	@#   5  sd_EnterBucket / sd_BuildBucketPath arguments,   canonicalised at
	@#      and sd_OpenSessionManifest's bucket-name check             callee
	@#   5  LOG_* / "dir='%s'" message text                           cosmetic
	@#   2  strncpy -> SYS_FS_DirectoryMake, and #1124 r12's         benign: FatFs
	@#      CountDirEntries -> SYS_FS_DirOpen occupancy check
	@#      create_name() ignores a terminating separator (E3, ff.c:2880), so
	@#      f_mkdir("d/") and f_mkdir("d") name the same directory. Benign because
	@#      of a VENDOR behaviour, not because of anything we do -- which is
	@#      exactly why it is written down rather than left to be re-derived.
	@#   (sd_ListDirTarget's two returns are inside that 22; its only consumer is
	@#    the walk root, canonicalised there. A SECOND consumer would get a raw
	@#    value, so it is pinned by name below.)
	@#
	@# 20 -> 22 at #1124 r11 finding 4, both reads in sd_OpenSessionManifest:
	@# the SdManifest_DirShadowsBucketName argument (that predicate calls
	@# SdManifest_CanonicalDirLen itself, so it is the callee-canonicalised
	@# class, same as sd_EnterBucket's) and the directory named in the LOG_E
	@# that reports why the session gets no manifest (cosmetic). NEITHER
	@# constructs a path, which is the property this count protects.
	@#
	@# 22 -> 21 at #1124 r13, OPERATOR ITEM 8 = B (issuecomment-6450419546):
	@# THREE reads removed, not one. The ruling records every file by its FULL
	@# path and drops the P###-folder-name refusal, which retired (a) the
	@# SdManifest_RelativeName argument in sd_AppendManifestLine -- the record
	@# side no longer slices the directory at all -- and (b,c) the
	@# SdManifest_DirShadowsBucketName argument plus its diagnostic's read, both
	@# in sd_OpenSessionManifest, which the r11-finding-4 entry above added.
	@# The canonicalisation worry those two lines carried is GONE rather than
	@# relocated: with full paths there is no slice to get wrong.
	@# 22 -> 24 at #1124 r12 finding 1, both in the same function's new
	@# occupancy guard: the CountDirEntries argument and its LOG_E.
	@# ⚠ The CountDirEntries one is NOT callee-canonicalised -- that function
	@# hands the path straight to SYS_FS_DirOpen. It is in the FatFs-benign
	@# class for the SAME vendor reason as the DirectoryMake read above:
	@# create_name() ignores a terminating separator (E3, ff.c:2880), so
	@# f_opendir("d/") and f_opendir("d") name one directory. Classified by what
	@# the callee actually DOES, not by which class looks adjacent -- this file
	@# already records a round lost to classifying a read by its variable name.
	@# 21 -> 22 at #1124 r14: the manifest-open DELIMITER guard now scans the
	@# directory as well as the file, because r13 put the directory into every
	@# recorded name. CLASSIFICATION: canonicalisation-INDEPENDENT, and that is
	@# a property of the predicate rather than of the variable -- strpbrk() asks
	@# whether a ',' CR or LF appears ANYWHERE in the string, and a trailing
	@# separator cannot change that answer. So SdManifest_CanonicalDirLen() would
	@# add nothing here. (Contrast the join sites, where a doubled '/' changes
	@# the constructed path and the accessor is load-bearing.)
	@# 22 -> 24 at #1124 r16: both in sd_DiscardStaleManifests(), which builds
	@# the manifest name to REMOVE a previous session's record when this session
	@# refuses to create its own. The two reads are the accessor argument and
	@# the "%.*s" operand of one join.
	@# CLASSIFICATION: canonicalisation-DEPENDENT, and already routed through
	@# SdManifest_CanonicalDirLen() -- which is why the join and accessor counts
	@# above moved 8 -> 9 in the same edit. This is the ordinary join shape, not
	@# a new class: a doubled '/' here would name a file the create path never
	@# created, so the removal would silently miss the stale manifest it exists
	@# to delete, and the defect would present as the fix simply not working.
	@# 24 -> 22 -> 20 -> 22 -> 20 across #1124 r16/r17/r18/r20. Every movement
	@# stays listed; the totals repeat and the sites never do:
	@#   +2 r16  the stale-manifest sweep's join.
	@#   -2 r16  two LOG_E messages stopped printing the directory, after the
	@#           #1110 gate measured them at 128 and 144 bytes against 125.
	@#   -2 r17  the FALLBACK name construction was deleted.
	@#   +2 r18  the LIVE-manifest name is built alongside the TARGET's, so the
	@#           invalidation can refuse to delete this session's own record.
	@#   -6 r20  THREE joins collapsed into one. The gate built the TARGET and
	@#           LIVE names and sd_OpenSessionManifest built the session's,
	@#           each with its own pair of reads -- and the gate's differed
	@#           from creation's, which IS r18/r19 finding 1 (the gate stripped
	@#           a subdirectory, creation did not). They now call one
	@#           derivation, sd_SessionManifestPath(), so the agreement is
	@#           structural instead of a property three sites must remember.
	@#   +4 r20  TWO new joins: sd_SessionManifestPath() itself, and
	@#           sd_NameIsRotationPartOfExistingManifest(), which builds the
	@#           OWNING base's manifest name so an opt-in on a part of a
	@#           recorded set can be refused (r19 finding 2).
	@#           CLASSIFICATION for both: ordinary canonicalisation-DEPENDENT
	@#           join shape, and both ARE routed through
	@#           SdManifest_CanonicalDirLen() -- checked, not assumed, and the
	@#           join/accessor counters above moved 9 -> 8 in the same edit,
	@#           which is the coincidence this ledger is for.
	@# CLASSIFICATION: all of them are the ordinary canonicalisation-DEPENDENT
	@# join shape, already routed through SdManifest_CanonicalDirLen(). A NET
	@# would let a future edit add one read and drop an unrelated one unseen,
	@# which is the one thing a count ledger exists to notice.
	@n=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null \
	    | grep -oE '(gpSDCardSettings|cfg)->(directory|opDirectory)' | wc -l`; \
	 test "$$n" -eq 20 \
	  || { echo "ERROR: $(SD_MANAGER_SRC) now has $$n raw reads of ->directory /"; \
	       echo "       ->opDirectory, not the 20 classified in this recipe (#1124)."; \
	       echo "       The canonicalisation invariant is about every USE, not just"; \
	       echo "       every join: a read can reach a callee that neither joins nor"; \
	       echo "       canonicalises, and no join-keyed check sees it. (Until r13 the"; \
	       echo "       example here was SdManifest_RelativeName slicing the directory;"; \
	       echo "       item 8 = B deleted that function, so the example is gone but the"; \
	       echo "       invariant is not.) Read the new read, decide whether it needs"; \
	       echo "       SdManifest_CanonicalDirLen(), and update the classification"; \
	       echo "       above with the reason."; \
	       exit 1; }
	@# ⛔ #1124 r13: THE r8-FINDING-2 PIN IS RETIRED, not relaxed.
	@# It required SdManifest_RelativeName() to canonicalise the directory it
	@# sliced, because a strlen()-based slice returned the whole path. Operator
	@# item 8 = B deleted that function: nothing slices the directory any more,
	@# so there is no slice to get wrong and no accessor for the two sides to
	@# disagree about. The property it protected is now provided by construction
	@# rather than by a check -- the recorded name IS the constructed path.
	@# Its replacement is the full-path pin further up, which asserts the one
	@# line this now rests on. The absence of the function is pinned there too,
	@# so this cannot quietly come back half-done.
	@# ⛔⛔ #1124 r9: THE OPERAND MATRIX. A path join has TWO user-controlled
	@# operands and the count arm above covers only the DIRECTORY one. r9's
	@# finding was the NAME operand on the adjacent line of the same snprintf --
	@# the sixth instance of one generator, each round fixing the operand the
	@# previous finding happened to NAME. So this pins the matrix, not a site
	@# list: every operand of every join, against the rejection that applies to
	@# ITS half.
	@#
	@# ⚠ THE TWO HALVES NEED OPPOSITE TREATMENTS, which is why a uniform
	@# "canonicalise every path operand" would have been actively wrong:
	@#     DIRECTORY + trailing '/'  = a redundant spelling   -> STRIP
	@#     FILE NAME + trailing '/'  = names no file at all   -> REFUSE
	@#
	@# ARM 1 -- the NVM arm, and the one the operator called for. The refusal
	@# must exist at USE, in sd_card_manager.c, NOT only in the SCPI setter: an
	@# NVM restore writes the settings struct directly and never reaches
	@# SD_ValidatePathParam. That route-around is exactly what made a
	@# setter-only fix unacceptable for the directory half. Counting the USE
	@# site here is what makes "we validated it in the setter" insufficient.
	@n=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null \
	    | grep -c 'SdManifest_NameIsUsable('`; \
	 test "$$n" -eq 1 \
	  || { echo "ERROR: $(SD_MANAGER_SRC) calls SdManifest_NameIsUsable() $$n"; \
	       echo "       time(s), expected 1 (#1124 r10)."; \
	       echo "       ONE call, at the PROCESS_STATE_OPEN_FILE guard, where a"; \
	       echo "       user-supplied name becomes a STREAM-FILE path."; \
	       echo "       r9 had three. The two in sd_OpenSessionManifest were"; \
	       echo "       removed at r10: the manifest appends SD_MANIFEST_EXT, so"; \
	       echo "       its completed name cannot end in a separator, and"; \
	       echo "       checking the stripped intermediate refused three legal"; \
	       echo "       names (.csv, sub/.csv, noext/) while MISSING log.csv/,"; \
	       echo "       the case it was written for. Do not re-add them."; \
	       echo "       ⛔ This must hold IN THIS FILE, not only in the SCPI"; \
	       echo "       setter: an NVM restore populates the settings struct"; \
	       echo "       directly and never reaches SD_ValidatePathParam, which"; \
	       echo "       is the route-around that sank a setter-only fix for the"; \
	       echo "       directory half. A refusal that only exists in the setter"; \
	       echo "       is not a refusal."; \
	       exit 1; }
	@# ⛔⛔ #1124 r10 finding 3 (HIGH): THE UNMOUNT RECORD MUST BE MARKED WHEN
	@# ITS BYTES ARE IN DOUBT. FF_FS_TINY is 0 (ffconf.h:226), so FatFs keeps a
	@# per-file sector cache. If the unmount FileSync returned -1, or the close
	@# returned FAILURE, counted bytes may still be in that cache when the
	@# unmount discards them -- so the record would OVERSTATE the bytes and
	@# carry a CRC over data the card never received, which is the one failure
	@# mode a manifest exists to rule out. The code used to justify appending
	@# unmarked with "the bytes counted are the bytes sent either way"; that is
	@# false exactly when the sync failed.
	@#
	@# EVERY ARM BY POSITION, not by count: a count of assignments cannot see
	@# WHICH branch each one is in -- the r9 arm-1b lesson. Each failure LOG_E
	@# must be followed by the marker within a few lines.
	@#
	@# 3 -> 4 at #1124 r12's review leg, which added the HEADER-write arm: on an
	@# error there Harmony has already destroyed FatFs's partial count, so bytes
	@# may be on the card that neither currentFileBytes nor the CRC fold can
	@# account for -- the same mechanism as the data-write arm. The header is a
	@# separate site only because it is the one stream-file byte range that does
	@# not pass through SDCardWrite(). It gets its OWN position arm below, so the
	@# raised count still cannot be satisfied by assignments in wrong branches.
	@# Distances are measured on the PREPROCESSED text, where comments are gone:
	@# the header marker sits 6 lines past its LOG_E in that form and the nearest
	@# other assignment is ~500 lines away, so these windows cannot cross-match.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null`; \
	 n=`printf '%s\n' "$$pp" | grep -c 'fileRecordIsPrefix = true'`; \
	 sl=`printf '%s\n' "$$pp" | grep -n 'Error flushing before unmount' | head -1 | cut -d: -f1`; \
	 cl=`printf '%s\n' "$$pp" | grep -n 'Failed to close file during unmount' | head -1 | cut -d: -f1`; \
	 hl=`printf '%s\n' "$$pp" | grep -n 'header write short' | head -1 | cut -d: -f1`; \
	 sm=0; cm=0; hm=0; \
	 if [ -n "$$sl" ]; then sm=`printf '%s\n' "$$pp" | sed -n "$$sl,$$((sl+6))p" | grep -c 'fileRecordIsPrefix = true'`; fi; \
	 if [ -n "$$cl" ]; then cm=`printf '%s\n' "$$pp" | sed -n "$$cl,$$((cl+6))p" | grep -c 'fileRecordIsPrefix = true'`; fi; \
	 if [ -n "$$hl" ]; then hm=`printf '%s\n' "$$pp" | sed -n "$$hl,$$((hl+8))p" | grep -c 'fileRecordIsPrefix = true'`; fi; \
	 test "$$n" -eq 4 && test "$$sm" -ge 1 && test "$$cm" -ge 1 && test "$$hm" -ge 1 \
	  || { echo "ERROR: #1124 r10 finding 3 -- the unmount manifest record is no"; \
	       echo "       longer marked when its byte count is in doubt."; \
	       echo "         fileRecordIsPrefix = true assignments: $$n (expected 4)"; \
	       echo "         within 6 lines of the SYNC failure LOG_E:  $$sm (need >= 1)"; \
	       echo "         within 6 lines of the CLOSE failure LOG_E: $$cm (need >= 1)"; \
	       echo "         within 8 lines of the HEADER-write LOG_E:   $$hm (need >= 1)"; \
	       echo "       FF_FS_TINY is 0, so a failed sync or close can leave"; \
	       echo "       counted bytes in FatFs's sector cache for the unmount to"; \
	       echo "       discard. An unmarked record then OVERSTATES the bytes and"; \
	       echo "       its CRC covers data the card never received."; \
	       echo "       Position is asserted, not just the count: four"; \
	       echo "       assignments in the wrong branches satisfy a count."; \
	       exit 1; }
	@# ARM 2 -- the asymmetry is not interchangeable. If someone ever "unifies"
	@# the two predicates, these go non-zero and this fails. That is the whole
	@# defence against the next round of this generator.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null`; \
	 bad1=`printf '%s\n' "$$pp" | grep -cE 'SdManifest_NameIsUsable\((gpSDCardSettings->|cfg->)?(directory|opDirectory)'`; \
	 bad2=`printf '%s\n' "$$pp" | grep -cE 'SdManifest_CanonicalDirLen\((gpSDCardSettings->|gSDCardData\.)?(file|opFile|baseFilename)'`; \
	 test "$$bad1" -eq 0 && test "$$bad2" -eq 0 \
	  || { echo "ERROR: #1124 r9 -- a path operand is being given the WRONG"; \
	       echo "       half's treatment in $(SD_MANAGER_SRC)."; \
	       echo "         NameIsUsable on a DIRECTORY field:  $$bad1 (must be 0)"; \
	       echo "         CanonicalDirLen on a FILE field:     $$bad2 (must be 0)"; \
	       echo "       A trailing '/' is a redundant spelling on a directory and"; \
	       echo "       is not a file name at all, so STRIP and REFUSE are not"; \
	       echo "       interchangeable. Refusing a directory breaks SD:DIR"; \
	       echo "       \"DAQiFi/\"; stripping a file name invents a target the"; \
	       echo "       caller never asked for."; \
	       exit 1; }
	@# BOTH arms of sd_BuildBucketPath, by exact form. A presence check that the
	@# function merely CALLS the accessor is not enough, and this was measured:
	@# reverting ONLY the bucket==0 arm to `"%s", dir` leaves raw=0 (no slash to
	@# key on), canon=8, accessor=8 and the call still present -- every count arm
	@# above passes while the propagation point is live again. That mutation is
	@# also caught by gen_1029_log_budget.py, which pins the same line for its own
	@# reasons, but relying on THAT would make this hole depend on a pin belonging
	@# to another ticket. Checked here so the two are independent.
	@awk '/^static bool sd_BuildBucketPath\(/{f=1} f{print} f&&/^\}/{exit}' \
	   $(SD_MANAGER_SRC) > /tmp/.sd1124_bb.$$$$; \
	 acc=`grep -c 'SdManifest_CanonicalDirLen(' /tmp/.sd1124_bb.$$$$`; \
	 cp0=`grep -cF '"%.*s", dirLen, dir' /tmp/.sd1124_bb.$$$$`; \
	 jn=`grep -cF '"%.*s/"' /tmp/.sd1124_bb.$$$$`; \
	 rm -f /tmp/.sd1124_bb.$$$$; \
	 test "$$acc" -eq 1 && test "$$cp0" -eq 1 && test "$$jn" -eq 1 \
	  || { echo "ERROR: sd_BuildBucketPath() in $(SD_MANAGER_SRC) no longer"; \
	       echo "       canonicalises BOTH arms (#1124)."; \
	       echo "         accessor calls:              $$acc (expected 1)"; \
	       echo "         bucket==0 canonical copy:    $$cp0 (expected 1)"; \
	       echo "         bucket!=0 canonical join:    $$jn (expected 1)"; \
	       echo "       The bucket==0 arm copies the directory into bucketPath, and"; \
	       echo "       generateFilename() joins onto that buffer WITHOUT naming the"; \
	       echo "       settings field -- so a trailing slash surviving here resurfaces"; \
	       echo "       as 'dir//file' two hops away, where no format-shaped or"; \
	       echo "       identifier-shaped check can see it."; \
	       exit 1; }
	@awk '/^static ListDirResult ListFilesInDirectoryChunked\(/{f=1} f{print} f&&/^\}/{exit}' \
	   $(SD_MANAGER_SRC) | grep -q 'SdManifest_CanonicalDirLen(' \
	  || { echo "ERROR: ListFilesInDirectoryChunked() in $(SD_MANAGER_SRC) no longer"; \
	       echo "       canonicalises its walk ROOT (#1124). The root is seeded from"; \
	       echo "       sd_ListDirTarget(), i.e. opDirectory or directory, and every"; \
	       echo "       deeper path is built by joining onto that frame -- which is why"; \
	       echo "       the join in this function is NOT the false positive it was"; \
	       echo "       first ruled to be."; \
	       exit 1; }
	@awk '/^static void sd_CloseSessionManifest\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	   $(SD_MANAGER_SRC) > /tmp/.sd924_close.$$$$; \
	 n=`grep -c 'manifestHandle = SYS_FS_HANDLE_INVALID;' /tmp/.sd924_close.$$$$`; \
	 clr=`grep -n 'manifestHandle = SYS_FS_HANDLE_INVALID;' /tmp/.sd924_close.$$$$ | head -1 | cut -d: -f1`; \
	 ret=`grep -n '^        return;' /tmp/.sd924_close.$$$$ | sed -n 2p | cut -d: -f1`; \
	 rm -f /tmp/.sd924_close.$$$$; \
	 test -n "$$clr" && test -n "$$ret" && test "$$n" -eq 1 && test "$$ret" -lt "$$clr" \
	  || { echo "ERROR: sd_CloseSessionManifest() in $(SD_MANAGER_SRC) no longer keeps"; \
	       echo "       the handle when SYS_FS_FileClose() FAILS (#924 round-2"; \
	       echo "       finding 4). This pin checks POSITION, not presence: the"; \
	       echo "       failure branch must 'return;' BEFORE the single"; \
	       echo "       'manifestHandle = ...' clear. The anchor is the SECOND"; \
	       echo "       'return;' -- the FIRST is the invalid-handle guard at the"; \
	       echo "       top, which precedes the clear in the BROKEN version too, so"; \
	       echo "       anchoring on it made this pin pass its own mutant."; \
	       echo "       'manifestHandle = SYS_FS_HANDLE_INVALID;' on the success path."; \
	       echo "       got: $$n clear(s), first clear at line $$clr, first return at $$ret."; \
	       echo "       WHY IT MATTERS, from the vendor source: SYS_FS_FileClose clears"; \
	       echo "       fileObj->inUse ONLY on success (sys_fs.c:951-960), and FatFs"; \
	       echo "       f_close skips 'fp->obj.fs = 0' when its leading f_sync fails"; \
	       echo "       (ff.c:4226-4242). So a failed close leaves the file OPEN in"; \
	       echo "       both layers; clearing the handle discards the only reference"; \
	       echo "       and burns one of SYS_FS_MAX_FILES (10) slots permanently."; \
	       exit 1; }
	@# ⛔ #1124 r16: SCOPED TO THE CREATE FUNCTION, and TIGHTENED rather than
	@# relaxed. The invariant is 'every manifest name this session will OPEN is
	@# length-checked', because the operand cap is about RETRIEVAL -- SD:GET,
	@# SD:CRC? and SD:DELete naming the file later. sd_DiscardStaleManifests()
	@# builds a name to REMOVE one, and must NOT be length-checked: refusing to
	@# delete a stale manifest because its name is too long to retrieve would
	@# leave the mismatched record on the card in exactly the case hardest to
	@# notice, which is the defect r16 exists to close.
	@# So the count equality now spans sd_OpenSessionManifest's body
	@# only (retargeted at r17, when the attempt/wrapper split was reverted\n	@# and the function took its original name back). It still catches the\n	@# original defect -- a THIRD creating path
	@# appearing unguarded -- because any such path is inside that function.
	@# THREE non-vacuity assertions, because a scoped count is exactly the
	@# shape that silently measures an EMPTY region (this file has lost a round
	@# to a pin whose awk range extracted nothing):
	@#   - the extracted body must be non-empty;
	@# ⛔ r20: WHAT COUNTS AS "PRODUCING A NAME" CHANGED, and this pin caught it
	@# by REFUSING TO RUN -- its body count went to 0 and the non-vacuity arm
	@# fired rather than letting the equality pass over nothing. That arm earned
	@# its keep, so this is re-expressed and not relaxed.
	@# The construction moved into sd_SessionManifestPath(), the ONE derivation
	@# the gate and this function now share (r18/r19 finding 1), so the name is
	@# no longer BUILT in this body -- it is REQUESTED here. The coupling the
	@# pin protects is unchanged: every manifest name this function produces,
	@# and therefore RECORDS, must be length-checked by
	@# SdManifest_NameFitsOperand, because buffer capacity and RETRIEVAL
	@# capacity are still different limits. So the construction count becomes a
	@# count of CALLS to the shared derivation and the equality still reads
	@# "every name produced here is checked here".
	@# The gate deliberately does NOT check NameFitsOperand: it only stats the
	@# name and never records it, so retrieval capacity does not apply there.
	@#   - it must build at least 1 name, so a range that clipped early cannot
	@#     read as a pass. This was 2 (primary and fallback) until r17 deleted
	@#     the second candidate; lowered with the reason rather than quietly,
	@#     because a bound that drops without one is how a count pin dies;
	@#   - the WHOLE-FILE count must EXCEED the body count, which is what
	@#     proves the removal site is genuinely outside the scope and the
	@#     scoping is doing something.
	@pp=`$(CC) -fpreprocessed -dD -E $(SD_MANAGER_SRC) 2>/dev/null`; \
	 body=`printf '%s\n' "$$pp" | awk '/^static void sd_OpenSessionManifest\(void\) \{/{f=1} f{print} f&&/^\}/{exit}'`; \
	 all=`printf '%s\n' "$$pp" | grep -c 'SD_MANIFEST_EXT,'`; \
	 g=`printf '%s\n' "$$body" | grep -c 'SdManifest_NameFitsOperand'`; \
	 n=`printf '%s\n' "$$body" | grep -c 'sd_SessionManifestPath('`; \
	 test -n "$$body" && test "$$n" -ge 1 && test "$$all" -gt "$$n" \
	  || { echo "ERROR: the scoped manifest-name count is not measuring what it"; \
	       echo "       claims (#1124 r16 non-vacuity)."; \
	       echo "         extracted body lines: `printf '%s\n' "$$body" | wc -l`"; \
	       echo "         names produced in body: $$n (must be >= 1)"; \
	       echo "         names built in file:  $$all (must exceed the body's)"; \
	       echo "       Either sd_OpenSessionManifest was renamed, or the"; \
	       echo "       awk range no longer spans it, or the removal site that"; \
	       echo "       justifies the scoping is gone. Re-derive before trusting"; \
	       echo "       the equality below -- an empty range makes it vacuous."; \
	       exit 1; }; \
	 test "$$g" = "$$n" \
	  || { echo "ERROR: $(SD_MANAGER_SRC) builds $$n manifest name(s) but length-"; \
	       echo "       checks $$g of them (#924 round-6 finding 2)."; \
	       echo "       EVERY path that produces a manifest name must pass through"; \
	       echo "       SdManifest_NameFitsOperand. Round 5 added that guard at the"; \
	       echo "       PRIMARY site and nowhere else, and the FALLBACK -- which keeps"; \
	       echo "       the whole filename, so it is LONGER by the extension -- went"; \
	       echo "       unguarded. Buffer capacity and RETRIEVAL capacity are"; \
	       echo "       different limits; the 'written >= sizeof(path)' check is the"; \
	       echo "       first and does not imply the second."; \
	       echo "       This pin is a COUNT EQUALITY, deliberately: a position pin"; \
	       echo "       cannot see a site that does not exist yet, and the whole"; \
	       echo "       defect was a THIRD path appearing without the guard."; \
	       echo "       Both counts are taken comment-STRIPPED (\$$(CC)"; \
	       echo "       -fpreprocessed), because the explanatory comments name both"; \
	       echo "       symbols -- a raw grep would be satisfied by prose."; \
	       exit 1; }
	@awk '/case SD_CARD_MANAGER_PROCESS_STATE_IDLE:/{f=1;a=""} \
	      f{a=a $$0 "\n"} \
	      f&&/^[[:space:]]*break;/{if(a ~ /manifestHandle = SYS_FS_HANDLE_INVALID;/) ok=1; f=0} \
	      END{exit ok?0:1}' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: no PROCESS_STATE_IDLE arm in $(SD_MANAGER_SRC) releases"; \
	       echo "       manifestHandle (#924 round-2 audit HIGH)."; \
	       echo "       THIS IS A CHOKE-POINT PIN, and it is scoped for two reasons."; \
	       echo "       (1) This file has SIX 'manifestHandle = SYS_FS_HANDLE_INVALID;'"; \
	       echo "           sites, so an unscoped grep is satisfied by any of them and"; \
	       echo "           asserts nothing -- the way an earlier pin here was satisfied"; \
	       echo "           by sd_CloseSessionManifest()'s invalid-handle GUARD return."; \
	       echo "       (2) There are THREE 'case ...PROCESS_STATE_IDLE:' lines; two are"; \
	       echo "           in other switches (one just returns the string \"IDLE\"), so"; \
	       echo "           this accepts ANY such arm containing the release rather than"; \
	       echo "           assuming which one comes first."; \
	       echo "       WHY A CHOKE POINT: this file has TWELVE assignments of"; \
	       echo "       PROCESS_STATE_IDLE and ONE sd_CloseSessionManifest() call."; \
	       echo "       Three successive fixes each enumerated the exits their author"; \
	       echo "       could see and each list agreed with the previous one and with"; \
	       echo "       nothing else. The IDLE arm is where every exit is dispatched on"; \
	       echo "       the next tick, so a new exit inherits the close without anyone"; \
	       echo "       remembering. Do not replace this with another exit list."; \
	       exit 1; }
	@awk '/if \(SYS_FS_Unmount\(SD_CARD_MANAGER_DISK_MOUNT_NAME\) == 0\)/{f=1;d=0} \
	      f{print; d+=gsub(/\{/,"{"); d-=gsub(/\}/,"}"); if(d==0 && NR>1 && f) exit}' \
	   $(SD_MANAGER_SRC) > /tmp/.sd924_umnt.$$$$; \
	 n=`grep -c 'manifestHandle = SYS_FS_HANDLE_INVALID;' /tmp/.sd924_umnt.$$$$`; \
	 rm -f /tmp/.sd924_umnt.$$$$; \
	 test "$$n" -ge 1 \
	  || { echo "ERROR: the SUCCESSFUL-unmount branch in $(SD_MANAGER_SRC) no longer"; \
	       echo "       releases manifestHandle (#924 round-2 audit HIGH). got $$n."; \
	       echo "       SCOPE MATTERS HERE: this file has several"; \
	       echo "       'manifestHandle = SYS_FS_HANDLE_INVALID;' sites, so the pin"; \
	       echo "       extracts ONLY the 'if (SYS_FS_Unmount(...) == 0) { ... }'"; \
	       echo "       block by brace depth. An unscoped grep would be satisfied by"; \
	       echo "       sd_CloseSessionManifest()'s own clear and assert nothing --"; \
	       echo "       the way an earlier pin here was satisfied by the function's"; \
	       echo "       invalid-handle GUARD return."; \
	       echo "       WHY: SYS_FS_Unmount clears inUse for every file object on the"; \
	       echo "       disk, but ONLY on success (sys_fs.c:725-731). A handle retained"; \
	       echo "       past that point is stale, its slot is already back, and"; \
	       echo "       SYS_FS_FileClose refuses it on the inUse==false guard"; \
	       echo "       (sys_fs.c:923-927) -- so sd_OpenSessionManifest would decline a"; \
	       echo "       manifest for every later session. Bounded leak -> permanent"; \
	       echo "       feature loss, which is worse than the defect the retention fixed."; \
	       exit 1; }
	@# ⛔ #1124 r16: RETARGETED. The body moved to
	@# sd_OpenSessionManifest(); at r16 that body lived in a separate
	@# ...Attempt() function behind a wrapper, and r17 reverted that split, so
	@# the anchor has moved twice. Each time the emptiness check below caught
	@# it rather than letting the pin pass while measuring nothing -- which is
	@# the whole reason it was added. Retargeting is the fix; weakening the
	@# check is not.
	@awk '/^static void sd_OpenSessionManifest\(void\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	   $(SD_MANAGER_SRC) > /tmp/.sd924_open.$$$$; \
	 test -s /tmp/.sd924_open.$$$$ \
	  || { echo "ERROR: the close-before-open pin extracted an EMPTY body from"; \
	       echo "       $(SD_MANAGER_SRC) (#1124 r16). Its awk anchor names"; \
	       echo "       sd_OpenSessionManifest(void); if that is renamed or"; \
	       echo "       reshaped the pin measures NOTHING, which is the one way a"; \
	       echo "       position pin passes while blind."; \
	       rm -f /tmp/.sd924_open.$$$$; exit 1; }; \
	 cls=`grep -n 'SYS_FS_FileClose(' /tmp/.sd924_open.$$$$ | head -1 | cut -d: -f1`; \
	 opn=`grep -n 'SYS_FS_FileOpen(' /tmp/.sd924_open.$$$$ | head -1 | cut -d: -f1`; \
	 rm -f /tmp/.sd924_open.$$$$; \
	 test -n "$$cls" && test -n "$$opn" && test "$$cls" -lt "$$opn" \
	  || { echo "ERROR: sd_OpenSessionManifest() in $(SD_MANAGER_SRC) no longer retries"; \
	       echo "       a surviving close BEFORE opening (#924 round-2 finding 4)."; \
	       echo "       Position pin: a SYS_FS_FileClose( must precede the"; \
	       echo "       SYS_FS_FileOpen( that assigns manifestHandle."; \
	       echo "       got: first close at line $$cls, first open at line $$opn."; \
	       echo "       Without it the open OVERWRITES a handle kept for retry, which"; \
	       echo "       is where a pending leak becomes a permanent one -- and the"; \
	       echo "       leak stops being bounded at one slot for the whole boot."; \
	       exit 1; }
	@test $$(grep -c 'sd_ManifestPathSafeToOverwrite(' $(SD_MANAGER_SRC)) -ge 3 \
	  || { echo "ERROR: $(SD_MANAGER_SRC) calls sd_ManifestPathSafeToOverwrite fewer"; \
	       echo "       than 3 times (its own definition plus 2 call sites: the primary"; \
	       echo "       manifest path and the collision fallback path). #924's"; \
	       echo "       post-merge audit defect 1 (round 2) requires BOTH candidate"; \
	       echo "       paths checked before a truncating SYS_FS_FILE_OPEN_WRITE --"; \
	       echo "       checking only the primary leaves the fallback name as a second"; \
	       echo "       undefended way to destroy an unrelated file."; \
	       exit 1; }
	@grep -q 'SdManifest_FirstLineLooksLikeManifest' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer inspects an existing file's first"; \
	       echo "       line through SdManifest_FirstLineLooksLikeManifest (#924"; \
	       echo "       post-merge audit, defect 1). Without it, a manifest open would"; \
	       echo "       truncate whatever already sits at the manifest path, no matter"; \
	       echo "       what it is."; \
	       exit 1; }
	@test $$(grep -c 'SdManifest_FreshFileAccounting(' $(SD_MANAGER_SRC)) -ge 2 \
	  || { echo "ERROR: $(SD_MANAGER_SRC) calls SdManifest_FreshFileAccounting fewer"; \
	       echo "       than 2 times. OPEN_FILE must reset currentFileBytes/"; \
	       echo "       fileCrcRunning through it at BOTH places a new file becomes"; \
	       echo "       current -- the normal open, and an ABORTED rotation-open (the"; \
	       echo "       one #924's post-merge audit defect 3 found missing: without"; \
	       echo "       it, UNMOUNT_DISK's unconditional manifest line for the new,"; \
	       echo "       empty file it just closed carries the OLD file's byte count"; \
	       echo "       and CRC instead). If a real refactor legitimately drops one"; \
	       echo "       call site, re-derive whether the abort arm still resets before"; \
	       echo "       lowering this count."; \
	       exit 1; }
	@# ⛔ #1124 r11 finding 3: the #924 manifest gate must read a SNAPSHOT of
	@# gWriteSessionIsStreamingLog taken in the #782 critical section, never the
	@# live global.
	@#
	@# SCPI is priority 7 and the SD task 5, so a SYST:STR:STOP landing after
	@# that critical section clears the global -- and the gate sits ~200 lines
	@# below it, past the header SYS_FS_FileWrite. Reading the global there
	@# means a session that DID open its first stream file and write its header
	@# silently gets no manifest. openAborted already covers a STOP arriving
	@# BEFORE the section; this covers the window after it. The adjacent header
	@# write (openNeedsStreamHeader) has always snapshotted for exactly this
	@# reason -- the manifest gate just had not followed suit.
	@#
	@# COUNT BEFORE POSITION. The count is what makes the position meaningful:
	@# with exactly one assignment and exactly one use, "the assignment is
	@# inside the critical section and the use is after it" describes THE pair,
	@# not merely some pair. Pinning order alone would pass a second, live read
	@# added anywhere.
	@#
	@# AND THE ASSIGNMENT IS PINNED BY ITS EXACT FORM, both sides. A mutant
	@# measured during this pin's own development proves why: with
	@# `bool openIsStreamingLog = openNeedsStreamHeader;` the count is still 1,
	@# the order still holds and no live global is read in the success arm --
	@# every weaker arm of this guard passes -- yet openNeedsStreamHeader is
	@# ALSO gated on gSdRotating, false for the session's FIRST file, so the
	@# manifest would never open at all. A position-and-count pin cannot see
	@# which value it counted; only the exact form can.
	@#
	@# ⚠ ORDER, NOT LINE NUMBERS. `-fpreprocessed -E` deletes comment LINES
	@# (6406 -> 3589 here), so absolute numbering does NOT survive it; the
	@# existing "preserves the geometry" note above is about continuation lines
	@# WITHIN a statement, which is a different claim. Relative order does
	@# survive -- the preprocessor never reorders code -- and relative order is
	@# all this needs.
	@#
	@# A missing anchor FAILS. If the #782 critical section is refactored away
	@# this cannot evaluate its invariant, and "could not evaluate" must not
	@# read as "holds" (the third-state lesson this file's -Wall gate records).
	@#
	@# EACH FAILURE MODE EMITS ITS OWN FIRST `ERROR:` LINE, deliberately.
	@# pin1124_mutate.sh matches on the FIRST ^ERROR line, so one shared
	@# message would let every r11 mutant satisfy the same substring -- proving
	@# only that SOMETHING fired, never that the right arm did. A control that
	@# matches on both sides is not a control.
	@#
	@# ⚠ And the checks below are shell `if`s inside ONE backslash-continued
	@# command, so they carry NO inline comments: the continuation joins every
	@# line, and a `#` anywhere in it would comment out all the checks that
	@# follow. Explanation belongs here, above the command, not inside it.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null > /tmp/.sd1124r11.$$$$; \
	 n_snap=`grep -cF 'bool openIsStreamingLog = gWriteSessionIsStreamingLog;' /tmp/.sd1124r11.$$$$`; \
	 n_use=`grep -c 'if (openIsStreamingLog' /tmp/.sd1124r11.$$$$`; \
	 ord=`awk '/taskENTER_CRITICAL/{e=NR} \
	           /openAborted = \(gpSDCardSettings->mode/{a=NR; enter=e} \
	           /bool openIsStreamingLog *=/{s=NR} \
	           /taskEXIT_CRITICAL/{if(a&&!x)x=NR} \
	           /if \(openIsStreamingLog/{g=NR} \
	           END{if(!a||!enter||!x){print "ANCHOR"; exit} \
	               if(!s||!g){print "PARTIAL"; exit} \
	               print (enter<s && s<x && x<g) ? "OK" : "ORDER"}' \
	          /tmp/.sd1124r11.$$$$`; \
	 stale=`awk '/openAborted = \(gpSDCardSettings->mode/{a=1} \
	             a&&/taskEXIT_CRITICAL/{x=1} \
	             x&&/if \(openIsStreamingLog/{exit} \
	             x&&/gWriteSessionIsStreamingLog/{c++} \
	             END{print c+0}' /tmp/.sd1124r11.$$$$`; \
	 rm -f /tmp/.sd1124r11.$$$$; \
	 if [ "$$ord" = ANCHOR ]; then \
	   echo "ERROR: #1124 r11's snapshot invariant was NOT EVALUATED -- the #782"; \
	   echo "       critical section or the #924 gate is no longer where this guard"; \
	   echo "       can find it in $(SD_MANAGER_SRC)."; \
	   echo "       'Could not evaluate' must never read as 'holds'. Re-derive the"; \
	   echo "       anchors and this guard together."; \
	   exit 1; fi; \
	 if [ "$$n_snap" -ne 1 ]; then \
	   echo "ERROR: #1124 r11's latch snapshot is missing or not in its exact form"; \
	   echo "       in $(SD_MANAGER_SRC) (found $$n_snap, expected exactly 1 of"; \
	   echo "       'bool openIsStreamingLog = gWriteSessionIsStreamingLog;')."; \
	   echo "       BOTH SIDES are pinned: a snapshot of openNeedsStreamHeader sits"; \
	   echo "       in the right place with the right count and still breaks the"; \
	   echo "       feature outright -- that one is ALSO gated on gSdRotating, which"; \
	   echo "       is false for the session's FIRST file, so no manifest would ever"; \
	   echo "       open. Measured as a surviving mutant; hence the exact-form key."; \
	   exit 1; fi; \
	 if [ "$$n_use" -ne 1 ]; then \
	   echo "ERROR: the #924 manifest gate in $(SD_MANAGER_SRC) no longer reads the"; \
	   echo "       openIsStreamingLog snapshot (found $$n_use 'if (openIsStreamingLog',"; \
	   echo "       expected exactly 1) -- #1124 r11."; \
	   echo "       Reading the live global here is the defect itself: SCPI clears it"; \
	   echo "       at priority 7 while this task runs at 5, so a session that opened"; \
	   echo "       its first stream file would silently get no manifest."; \
	   exit 1; fi; \
	 if [ "$$ord" != OK ]; then \
	   echo "ERROR: #1124 r11's latch snapshot is taken OUTSIDE the #782 critical"; \
	   echo "       section in $(SD_MANAGER_SRC) (order check: $$ord)."; \
	   echo "       Required: taskENTER_CRITICAL < snapshot < taskEXIT_CRITICAL <"; \
	   echo "       the #924 gate. Outside it, the snapshot can be taken after a"; \
	   echo "       priority-7 SYST:STR:STOP has already cleared the latch, which is"; \
	   echo "       the exact race the snapshot exists to close."; \
	   exit 1; fi; \
	 if [ "$$stale" -ne 0 ]; then \
	   echo "ERROR: a LIVE gWriteSessionIsStreamingLog read is back in the OPEN_FILE"; \
	   echo "       success arm of $(SD_MANAGER_SRC) ($$stale found between the #782"; \
	   echo "       critical section and the #924 gate) -- #1124 r11."; \
	   echo "       Everything in that arm must use the snapshot; the global is"; \
	   echo "       clearable by SCPI at priority 7 while this task runs at 5."; \
	   exit 1; fi
	@# ⛔ #1124 r14: THE DELIMITER GUARD COVERS THE WHOLE RECORDED PATH.
	@# r13 put the configured DIRECTORY into every recorded name, which moved
	@# this guard's scope without moving the guard. SD_ValidatePathParam rejects
	@# control characters, backslash and colon -- but NOT a comma -- so
	@# SD:DIR "trial,1" was accepted and the line became
	@#     trial,1/exp.csv,20480,0x1A2B3C4D
	@# four fields where the format has three: a parser reads the name as
	@# "trial", the bytes as "1/exp.csv" and the CRC as "20480". A well-formed
	@# LOOKING lie, not a truncation.
	@#
	@# BOTH arms are pinned. The directory arm is the finding; the file arm is
	@# the pre-existing half, and dropping it while keeping the other would be
	@# the same defect mirrored. The bucket segment needs no arm -- it is
	@# generated as "P%03u" and cannot carry a delimiter -- so these two are
	@# between them the whole user-controlled surface of the recorded path.
	@#
	@# Comments stripped and the text flattened before matching, for the two
	@# reasons this file has now recorded twice: the removed names are still
	@# NAMED in nearby comments, and `-E` preserves the geometry so a wrapped
	@# call is invisible to a line-based grep.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr '\n' ' '`; \
	 for f in directory file; do \
	   printf '%s' "$$pp" \
	     | grep -qE "strpbrk\(gpSDCardSettings->$$f, *\",?.?.?\"\)|strpbrk\(gpSDCardSettings->$$f," \
	    || { echo "ERROR: $(SD_MANAGER_SRC)'s manifest-open guard no longer scans"; \
	         echo "       gpSDCardSettings->$$f for a manifest delimiter (#1124 r14)."; \
	         echo "       Since r13 the recorded name is the FULL path, so a ',' in"; \
	         echo "       EITHER the directory or the file produces a line with four"; \
	         echo "       fields instead of three -- which parses, wrongly, rather"; \
	         echo "       than failing. Both arms are required."; \
	         exit 1; }; \
	 done
	@# ⛔ #1124 r13 / OPERATOR ITEM 8 = B: THE RULING'S TWO HALVES, PINNED
	@# TOGETHER, because either one alone is the r11 defect again.
	@#
	@# Ruled 2026-10-27 (issuecomment-6450419546): record every file by its FULL
	@# path, and drop the P###-folder-name refusal. r11 recorded names RELATIVE
	@# to SD:DIR while retrieval ALSO strips SD:DIR, so the two strips composed
	@# into a different file -- which is what the refusal was guarding. Full
	@# paths mean exactly ONE strip, so the round trip is the identity for every
	@# directory and there is nothing left to refuse.
	@#
	@# Keeping the refusal AND full paths would merely be redundant; keeping the
	@# refusal and RELATIVE paths is r11; dropping the refusal while leaving
	@# relative paths is the defect with its guard removed. Only the pair is
	@# correct, so all three pins live in one block.
	@#
	@# Comments are stripped with `-fpreprocessed -E` before matching, because
	@# both removed names are still NAMED in the explanatory comments at each
	@# former site -- a bare literal grep would read those as live calls.
	@#
	@# ⚠ AND THE PREPROCESSED TEXT IS FLATTENED (`tr`), because `-E` strips
	@# comments but PRESERVES THE GEOMETRY: the record call wraps after
	@# `sizeof(line),` so the argument sits on the NEXT line, and grep is
	@# line-based. The unflattened form of this pin failed on correct source --
	@# measured, not hypothetical.
	@pp=`$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr '\n' ' '`; \
	 printf '%s' "$$pp" | grep -q 'SdManifest_FormatLine(line, sizeof(line),' \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer formats the manifest line the"; \
	       echo "       way this pin reads it (#1124 r13) -- re-read the record site"; \
	       echo "       before trusting the two checks below."; \
	       exit 1; }; \
	 printf '%s' "$$pp" | grep -qE 'SdManifest_FormatLine\(line, sizeof\(line\),[[:space:]]*gSDCardData\.filePath' \
	  || { echo "ERROR: $(SD_MANAGER_SRC) does not record the FULL path (#1124"; \
	       echo "       r13, operator item 8 = B). sd_AppendManifestLine must pass"; \
	       echo "       gSDCardData.filePath to SdManifest_FormatLine. Recording a"; \
	       echo "       name relative to SD:DIR makes retrieval strip the directory"; \
	       echo "       TWICE, which opens a different file -- and the refusal that"; \
	       echo "       used to cover that is gone, by ruling."; \
	       exit 1; }; \
	 printf '%s' "$$pp" | grep -q 'SdManifest_RelativeName(' \
	  && { echo "ERROR: $(SD_MANAGER_SRC) calls SdManifest_RelativeName again"; \
	       echo "       (#1124 r13). The record side must not slice the configured"; \
	       echo "       directory at all; see the full-path pin above."; \
	       exit 1; } || true
	@for f in $(SD_MANAGER_SRC) $(SD_BENCH_SRC); do \
	   $(CC) -fpreprocessed -E $$f 2>/dev/null \
	     | grep -q 'SdManifest_DirShadowsBucketName(' \
	    && { echo "ERROR: $$f calls SdManifest_DirShadowsBucketName again (#1124"; \
	         echo "       r13). The bucket-name refusal was DROPPED by ruling -- the"; \
	         echo "       ambiguity it guarded is removed at source by recording"; \
	         echo "       full paths, so re-adding it would refuse a configuration"; \
	         echo "       that now works."; \
	         exit 1; } || true; \
	 done
	@# ⛔ Part 2 of 2: the test MODELS SD_StripConfiguredDir, because
	@# SCPIStorageSD.c is not host-compilable. Pin the two lines the model's
	@# fidelity rests on -- the prefix compare and the separator-run skip -- so
	@# the real function cannot drift into something the model no longer mirrors
	@# while every assertion in the suite stays green.
	@grep -q 'strncmp(p, directory, dirLen) != 0 || p\[dirLen\] != ' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SD_StripConfiguredDir's prefix compare in $(SD_BENCH_SRC)"; \
	       echo "       changed shape. test_924_sd_manifest.c models that function to"; \
	       echo "       test the record->retrieve round trip; re-derive the model"; \
	       echo "       (strip_configured_dir_model) before trusting those results."; \
	       exit 1; }
	@# ⛔ #1124 r12 finding 1: the manifest's OCCUPANCY CEILING.
	@# The manifest is always created in the PARENT, never in the session's #689
	@# bucket, so once a session rolls into P001 nothing consults the parent's
	@# file count again and one manifest per session accumulates with no ceiling
	@# -- walking the parent's FatFs entry count back toward the O(directory
	@# size) create wedge #689 exists to escape.
	@#
	@# Pinned on the JOINED preprocessed text, deliberately. The `true` that
	@# selects files-only counting sits on a CONTINUATION line, and `cc -E`
	@# preserves that geometry (this file records the measurement), so no
	@# line-oriented grep can see the argument and the call together. Joining is
	@# the right instrument for an argument spanning lines -- what is wrong is
	@# ASSUMING the preprocessor already joined it.
	@#
	@# filesOnly is pinned as part of the call, not just the call: keying on the
	@# call alone would pass `false`, and `false` counts the bucket
	@# SUBDIRECTORIES too. Bucket 0 IS the parent, so the combined count is
	@# already ~MAX_DIR_FILES before the first bucket exists -- the guard would
	@# refuse every manifest from the moment bucketing began. That is not a
	@# ceiling, it is switching the feature off, and it would look like a
	@# working guard.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr -s '[:space:]' ' ' \
	  | grep -qE 'CountDirEntries\( *gpSDCardSettings->directory, *SD_CARD_MANAGER_MAX_DIR_FILES, *true' \
	  || { echo "ERROR: $(SD_MANAGER_SRC) no longer bounds manifest creation by the"; \
	       echo "       configured directory's FILE occupancy (#1124 r12 finding 1)."; \
	       echo "       Expected CountDirEntries(<directory>, MAX_DIR_FILES, true, ...)"; \
	       echo "       in sd_OpenSessionManifest. Note the THIRD argument: with"; \
	       echo "       false it counts bucket subdirectories too, and since bucket 0"; \
	       echo "       IS the parent that count is already ~MAX_DIR_FILES before any"; \
	       echo "       bucket exists -- the guard would then refuse EVERY manifest"; \
	       echo "       while still looking like it worked."; \
	       exit 1; }
	@# And the mode itself must still exist in the signature: without the
	@# parameter there is no files-only count to ask for, and the guard above
	@# would not compile -- but a default-valued or reordered parameter could
	@# compile and silently mean something else.
	@$(CC) -fpreprocessed -E $(SD_MANAGER_SRC) 2>/dev/null | tr -s '[:space:]' ' ' \
	  | grep -qE 'static uint32_t CountDirEntries\(const char\* dirPath, uint32_t cap, bool filesOnly, bool\* pFsError\)' \
	  || { echo "ERROR: CountDirEntries' signature in $(SD_MANAGER_SRC) changed."; \
	       echo "       #1124 r12 finding 1's occupancy guard needs the filesOnly"; \
	       echo "       mode, and the parent's entry budget has separate file and"; \
	       echo "       subdirectory terms that a combined count cannot separate."; \
	       echo "       Re-derive the guard with the signature."; \
	       exit 1; }
	@# The prefix the guard must match is the one buckets are NAMED with, so the
	@# test is handed the macro's real value rather than a hand-copied copy. Read
	@# from sd_card_manager.h -- where #1124 r11 moved it so the setter can see
	@# it. Extraction failing is FATAL, never a default: a guard checked against
	@# the wrong prefix would pass while covering nothing.
	@test -n '$(SD_BUCKET_PREFIX)' \
	  || { echo "ERROR: could not read SD_CARD_MANAGER_BUCKET_PREFIX out of"; \
	       echo "       $(SD_MGR_HDR). test_924_sd_manifest.c needs the REAL"; \
	       echo "       prefix (#1124 r11 finding 4) -- a hand-copied or defaulted"; \
	       echo "       value would let the bucket-name guard be checked against a"; \
	       echo "       string the firmware does not use. The test also carries an"; \
	       echo "       #error for the same reason, so neither half can default."; \
	       exit 1; }
	@# ⚠ HONEST SCOPE OF THAT GUARD: gen_1029_log_budget.py reads the SAME macro
	@# out of the SAME header and runs as an earlier build prerequisite, so for
	@# a rename or a removal IT aborts first and this never gets to speak. This
	@# is a fail-closed BACKSTOP, not an independently reachable arm, and
	@# pin1124_mutate.sh cannot demonstrate it firing -- stated here rather than
	@# left to look like a proven check. What R16 DOES prove is the other
	@# direction: an indented #define still extracts, because this sed now
	@# tolerates leading whitespace exactly as that generator's parser does. The
	@# two disagreeing is what would turn a harmless reformat into a build
	@# failure blaming the prefix.
	@# ⛔⛔ #1124 r17: INVALIDATE BEFORE TRUNCATE. r16 cleaned up AFTER a refusal
	@# and the audit found two windows that cannot reach: a STOP between the
	@# truncating open and the manifest call (the function never runs), and a
	@# SUCCESS that left the OTHER candidate name standing. Both are gone by
	@# construction now -- ONE name, removed BEFORE the data file is truncated --
	@# so these pins assert the new structure, and r16's PIN B (no bare `return;`
	@# in an attempt function) and PIN D (both candidate names swept) are DELETED
	@# rather than weakened: the attempt/wrapper split and the second candidate no
	@# longer exist, and a pin asserting a property the design dropped is the
	@# prose-satisfied guard this very recipe removed three of.
	@# PIN A -- the invalidation is called, and BEFORE the truncating open. A
	@# position pin, because calling it afterwards would delete the record of data
	@# this session just wrote. Scoped to the OPEN_FILE write arm.
	@awk '/\(SYS_FS_FILE_OPEN_WRITE_PLUS\)\)/{print NR": MARK_OPEN"} /sd_TargetSafeToOpenForWrite\(gSDCardData.filePath\)/{print NR": MARK_INV"}' \
	     $(SD_MANAGER_SRC) > /tmp/.sd1124_ord.$$$$; \
	 inv=`grep -m1 MARK_INV /tmp/.sd1124_ord.$$$$ | cut -d: -f1`; \
	 opn=`grep -m1 MARK_OPEN /tmp/.sd1124_ord.$$$$ | cut -d: -f1`; \
	 rm -f /tmp/.sd1124_ord.$$$$; \
	 test -n "$$inv" && test -n "$$opn" && test "$$inv" -lt "$$opn" \
	  || { echo "ERROR: the session manifest is not invalidated BEFORE the data file"; \
	       echo "       is truncated (#1124 r17 PIN A)."; \
	       echo "         sd_TargetSafeToOpenForWrite() at line $$inv"; \
	       echo "         the WRITE_PLUS open at line        $$opn"; \
	       echo "       The open is FA_CREATE_ALWAYS, so from that instant any"; \
	       echo "       manifest describing this name is wrong. Invalidating after it"; \
	       echo "       -- or not at all -- leaves a record that VALIDATES and"; \
	       echo "       describes bytes that are gone, which is worse than none."; \
	       echo "       An empty line number means the call or the open was renamed."; \
	       exit 1; }
	@# ⛔ PIN A2 -- INVERTED AT r18, and the inversion is the finding. r17 pinned
	@# that the invalidation was gated on isSessionFirstOpen. That gate was a PROXY
	@# for "do not delete our own live manifest", and the proxy failed where the
	@# two come apart: rotation reaches the SAME truncating open, and its
	@# existing-target search can land on a previous session's first file, which
	@# has a manifest of its own. So the gate is GONE and this pin now asserts its
	@# ABSENCE plus the real discriminator -- IDENTITY, keyed on the target path.
	@n=`grep -c 'sd_TargetSafeToOpenForWrite(gSDCardData.filePath)' $(SD_MANAGER_SRC)`; \
	 g=`grep -B6 'sd_TargetSafeToOpenForWrite(gSDCardData.filePath)' $(SD_MANAGER_SRC) \
	    | grep -c 'if (isSessionFirstOpen)'`; \
	 test "$$n" -eq 1 && test "$$g" -eq 0 \
	  || { echo "ERROR: the manifest invalidation is not keyed on the TARGET path, or"; \
	       echo "       a first-open gate came back (#1124 r18 PIN A2)."; \
	       echo "         calls keyed on gSDCardData.filePath: $$n (must be 1)"; \
	       echo "         isSessionFirstOpen gates above it:   $$g (must be 0)"; \
	       echo "       EVERY truncating open must invalidate ITS OWN target's"; \
	       echo "       manifest. A first-open gate skips rotation, whose target can"; \
	       echo "       be a previous session's first file -- the r17 defect."; \
	       exit 1; }
	@# PIN F -- A FAILED INVALIDATION REFUSES THE TRUNCATION. r17 logged the failure
	@# and truncated anyway, which is fail-OPEN on the one step that exists to stop
	@# a stale record. The refusal works by leaving the handle INVALID so the
	@# existing failed-open arm runs, so what is pinned is that the INVALID
	@# assignment sits between the check and the open.
	@awk '/if \(!sd_TargetSafeToOpenForWrite/{a=NR} \
	      /gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;/{if(a&&!b)b=NR} \
	      /\(SYS_FS_FILE_OPEN_WRITE_PLUS\)\)/{if(a&&!c)c=NR} \
	      END{print a" "b" "c}' $(SD_MANAGER_SRC) > /tmp/.sd1124_ref.$$$$; \
	 read a b c < /tmp/.sd1124_ref.$$$$; rm -f /tmp/.sd1124_ref.$$$$; \
	 test -n "$$a" && test -n "$$b" && test -n "$$c" \
	   && test "$$a" -lt "$$b" && test "$$b" -lt "$$c" \
	  || { echo "ERROR: a failed manifest invalidation no longer refuses the"; \
	       echo "       truncation (#1124 r18 PIN F)."; \
	       echo "         check at $$a, handle-INVALID at $$b, WRITE_PLUS open at $$c"; \
	       echo "       All three must be present in that order. If the refusal is"; \
	       echo "       gone, a .mfst that cannot be removed (read-only, FR_DENIED)"; \
	       echo "       is logged and the data is truncated beside it anyway."; \
	       exit 1; }
	@# PIN G -- the SETTER rejects a name FatFs would normalise. "exp.csv." opens and
	@# truncates exp.csv while every derived name keeps the dot, so the manifest
	@# goes to exp.csv..mfst and exp.csv's own record is neither invalidated nor
	@# written. Pinned in the VALIDATOR, which is the narrowest place that closes it.
	@awk '/^static bool SD_ValidatePathParam/{f=1} f{print} f&&/^\}/{exit}' \
	     $(SD_BENCH_SRC) > /tmp/.sd1124_val.$$$$; \
	 test -s /tmp/.sd1124_val.$$$$ \
	   && grep -q "p\[i - 1u\] == '\.' || p\[i - 1u\] == ' '" /tmp/.sd1124_val.$$$$ \
	  || { echo "ERROR: SD_ValidatePathParam no longer rejects a path segment that"; \
	       echo "       FatFs would normalise (#1124 r18 PIN G) -- a trailing '.' or"; \
	       echo "       ' '. Without it, 'exp.csv.' truncates exp.csv while the"; \
	       echo "       manifest name keeps the dot, so the data file's real record"; \
	       echo "       is left stale and the new one lands at a name nothing reads."; \
	       echo "       An empty extraction means the function was renamed."; \
	       rm -f /tmp/.sd1124_val.$$$$; exit 1; }; \
	 rm -f /tmp/.sd1124_val.$$$$
	@# PIN C -- the unlink stays gated on the create path's own predicate, and the
	@# predicate stays able to say no. Unchanged from r16; still the check that
	@# keeps this from deleting a user file that occupies the manifest name.
	@#
	@# ⛔ r20, TWO updates, and the first one broke this recipe before it was
	@# noticed: the signature gained a FAULT out-param, so BOTH the awk range
	@# and the call pattern had to move with it. An awk range that no longer
	@# matches extracts NOTHING, and the `test -s` non-vacuity arm is the only
	@# reason that surfaced as a failure instead of a pass.
	@# Why the out-param: the predicate's `false` used to mean both "not a
	@# manifest" (safe -- leave it) and "could not tell" (a fault -- refuse),
	@# and this function picked the first meaning and returned TRUE, i.e.
	@# permission to truncate. r19's findings at :2246/:2249. The coupling this
	@# pin asserts is unchanged -- the unlink is still gated on the create
	@# path's own predicate -- it now also honours WHICH answer it got.
	@awk '/^static bool sd_RemoveStaleManifestAt\(const char \*path, bool \*fsFault\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	     $(SD_MANAGER_SRC) > /tmp/.sd1124_rm.$$$$; \
	 test -s /tmp/.sd1124_rm.$$$$ \
	   && grep -q 'sd_ManifestPathSafeToOverwrite(path,' /tmp/.sd1124_rm.$$$$ \
	   && grep -q 'SYS_FS_FileDirectoryRemove(path)' /tmp/.sd1124_rm.$$$$ \
   && grep -q 'return false;' /tmp/.sd1124_rm.$$$$ \
	  || { echo "ERROR: sd_RemoveStaleManifestAt() no longer gates its unlink on the"; \
	       echo "       create path's predicate (#1124 r17 PIN C)."; \
	       rm -f /tmp/.sd1124_rm.$$$$; exit 1; }; \
	 rm -f /tmp/.sd1124_rm.$$$$
	@# PIN E -- a DIRECTORY is refused, and refused BEFORE the fsize==0 shortcut.
	@# ORDER IS THE WHOLE PIN: a directory stats with fsize == 0, so a check placed
	@# after that early `return true` never runs. The predicate was written to
	@# guard an open-for-write, where saying yes about a directory is harmless;
	@# r16 reused it to guard an UNLINK, where SYS_FS_FileDirectoryRemove removes
	@# directories too. Reusing a predicate is only sound where the guarded ACTION
	@# is the same.
	@# ⛔ r20: signature updated with the FAULT out-param. Third awk range on
	@# this row to need that, which is the cost of keying an extraction on a
	@# full signature -- precise, and it moves with every parameter change. Each
	@# one failed LOUDLY via its non-vacuity arm rather than measuring nothing,
	@# which is the only reason a signature change is a nuisance here instead of
	@# a silent hole.
	@awk '/^static bool sd_ManifestPathSafeToOverwrite\(const char \*path, bool \*fsFault\) \{/{f=1} f{print} f&&/^\}/{exit}' \
	     $(SD_MANAGER_SRC) > /tmp/.sd1124_safe.$$$$; \
	 d=`grep -n 'SYS_FS_ATTR_DIR' /tmp/.sd1124_safe.$$$$ | head -1 | cut -d: -f1`; \
	 z=`grep -n 'st.fsize == 0u' /tmp/.sd1124_safe.$$$$ | head -1 | cut -d: -f1`; \
	 rm -f /tmp/.sd1124_safe.$$$$; \
	 test -n "$$d" && test -n "$$z" && test "$$d" -lt "$$z" \
	  || { echo "ERROR: sd_ManifestPathSafeToOverwrite() does not refuse a DIRECTORY"; \
	       echo "       before its zero-size shortcut (#1124 r17 PIN E)."; \
	       echo "         SYS_FS_ATTR_DIR check at line $$d"; \
	       echo "         'st.fsize == 0u' shortcut at  $$z"; \
	       echo "       A directory stats with fsize == 0 and would take the"; \
	       echo "       shortcut, so a later check is dead and the unlink it guards"; \
	       echo "       can remove a DIRECTORY. An empty line number means one of"; \
	       echo "       the two was renamed and this pin measures nothing."; \
	       exit 1; }
	$(CC) $(CFLAGS) -I$(FW_UTIL) -DFW_SD_BUCKET_PREFIX='$(SD_BUCKET_PREFIX)' -o $(MANIFEST_BIN) test_924_sd_manifest.c $(FW_UTIL)/CRC32.c

$(BENCH_BIN): test_943_bench_stall_bound.c test_framework.h $(SD_BENCH_SRC) $(RECIPES)
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_TIMEOUT_MS[[:space:]]+10000U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_TIMEOUT_MS is no longer '10000U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_TIMEOUT_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	@# ⛔ #756 item 4 -> A (operator 2026-10-25): THIS PIN'S POLARITY IS INVERTED
	@# from what it was. It used to assert the presence WAIT existed and that it
	@# pumped USB before delaying (while < pump < delay). The wait is withdrawn,
	@# so that pin is not merely obsolete -- deleting it would leave the NEW
	@# invariant unguarded, and the new invariant is the safety-relevant one:
	@#
	@#   SCPI_CheckSDCardPresent() MUST NOT BLOCK.
	@#
	@# Over USB this function runs ON the USB task. A 500 ms park there filled
	@# the ring, blocked the encoder and overflowed the sample pool: ACQUIRED
	@# SAMPLES LOST. Pumping inside the wait divided the window by 20 and left
	@# the defect the same KIND, which is what the round-3 HIGH said. So the
	@# thing worth pinning is the absence of any wait, not the shape of one.
	@#
	@# ⚠ READ ON COMMENT-STRIPPED SOURCE, scoped to this ONE function.
	@#   - Stripped, because the call site necessarily EXPLAINS the withdrawn
	@#     wait, and that prose contains the words "while", "delay" and
	@#     "UsbCdc_PumpWrite". Measured: the raw count was 1 and the code count
	@#     0, the single hit being the word "while" in the new explanation. A
	@#     raw-text pin here would be re-broken by every future author who
	@#     documents WHY the wait is gone -- i.e. by doing the right thing.
	@#   - Scoped, because other functions in this file delay legitimately
	@#     (SD:BENCHmark's stall loop below is one), so a file-wide absence
	@#     check would be false on correct code.
	@#   - And the awk anchor matches the FULL SIGNATURE, not a name prefix.
	@#     Measured: with a prefix anchor, renaming the function to
	@#     SCPI_CheckSDCardPresentX STILL MATCHED, so the extraction still
	@#     succeeded and the vacuity guard below could never fire. An
	@#     unanchored pattern makes its own guard unreachable, and the mutant
	@#     that was supposed to prove the guard is what exposed it.
	@$(CC) -fpreprocessed -E $(SD_BENCH_SRC) 2>/dev/null \
	   | awk '/^static bool SCPI_CheckSDCardPresent\(scpi_t \*context\) \{$$/{f=1} f{print} f&&/^\}/{exit}' \
	   > /tmp/.sd756_fn.$$$$; \
	 n=`grep -c . /tmp/.sd756_fn.$$$$`; \
	 blk=`grep -cE 'vTaskDelay|UsbCdc_PumpWrite|while[[:space:]]*\(|for[[:space:]]*\(' /tmp/.sd756_fn.$$$$`; \
	 kick=`grep -c 'DRV_SDSPI_DetectPollExpireNow' /tmp/.sd756_fn.$$$$`; \
	 rm -f /tmp/.sd756_fn.$$$$; \
	 test "$$n" -gt 4 && test "$$blk" -eq 0 && test "$$kick" -eq 1 \
	  || { echo "ERROR: SCPI_CheckSDCardPresent() in $(SD_BENCH_SRC) no longer"; \
	       echo "       matches #756's withdrawn-wait invariant."; \
	       echo "         function lines extracted: $$n (must be > 4, else the"; \
	       echo "           extraction found nothing and this check is vacuous)"; \
	       echo "         blocking constructs:      $$blk (must be 0)"; \
	       echo "         detect-poll kicks:        $$kick (must be 1)"; \
	       echo "       A delay, pump or loop here parks the USB task and loses"; \
	       echo "       ACQUIRED SAMPLES (round-2/3 HIGHs). The kick must stay:"; \
	       echo "       without it a stale-DETACHED read is never corrected and"; \
	       echo "       the next query is as wrong as this one."; \
	       exit 1; }
	@grep -qE '^#define[[:space:]]+SCPI_SD_BENCH_STALL_POLL_MS[[:space:]]+5U[[:space:]]*$$' $(SD_BENCH_SRC) \
	  || { echo "ERROR: SCPI_SD_BENCH_STALL_POLL_MS is no longer '5U' in $(SD_BENCH_SRC)."; \
	       echo "       Update FW_STALL_POLL_MS in test_943_bench_stall_bound.c and re-derive its numbers."; \
	       exit 1; }
	@# (#1124 r9 arm 3 lives HERE, not in $(MANIFEST_BIN)'s recipe, because
	@# SCPIStorageSD.c is a prerequisite of THIS target and not of that one.
	@# A pin that reads a file its target does not depend on passes STALE:
	@# the file changes, the target is still up to date, the check never
	@# re-runs. Same reason $(RECIPES) is a prerequisite elsewhere here.
	@# ⛔⛔ ARM 3 (REPLACED at r10) -- THE ROLE TABLE, PER CALLBACK, NOT A COUNT.
	@# r9's version asserted the call sites split "5 file / 1 directory" and it
	@# locked in a WRONG classification: SCPI_StorageSDListDir fills
	@# opDirectory, but its length variable is named `fileLen`, so it was
	@# counted as a file operand and given isFileName=true. LISt? "DAQiFi/" was
	@# then refused -- the exact directory-half regression the parameterisation
	@# existed to prevent, arriving through the one site whose variable name
	@# lies about its destination.
	@#
	@# ⭐ A COUNT CANNOT CARRY A CLASSIFICATION. "Five of these are file
	@# operands" is true of any five, so the count passed while the wrong five
	@# were chosen. Same family as r9's arm 1b ("a count cannot see which
	@# argument it counted"), one level up: here it could not see which SITE.
	@# So each callback is named, with the role its subject requires:
	@#
	@#   SCPI_StorageSDLoggingSet    -> file        the logging target
	@#   SCPI_StorageSDCrcStart      -> file        CRC of one stored file
	@#   SCPI_StorageSDGetData       -> file        retrieve one file
	@#   SCPI_StorageSDDelete        -> file        delete one file
	@#   SCPI_StorageSDListDir       -> DIRECTORY   lists a directory (opDirectory)
	@#   SCPI_StorageSDDirectorySet  -> DIRECTORY   sets the working directory
	@#
	@# A new caller fails this until it is added here WITH its role decided,
	@# which is the point: the decision is strip-vs-reject and it cannot be
	@# inferred from the surrounding variable names.
	@#
	@# ⚠ The extraction uses awk's index() against the FULL signature, not a
	@# dynamic regex. The first version passed `^scpi_result_t NAME\\(` through
	@# make -> shell -> awk and every escaping layer had its own opinion; all
	@# six extractions silently returned nothing. It FAILED CLOSED only because
	@# the test is `calls -ne 1`, which catches 0 -- had it been `-gt 0` or a
	@# flag-only comparison, a broken extraction would have passed six empty
	@# bodies as compliant. index($$0,nm)==1 has no metacharacters to lose.
	@pp=`$(CC) -fpreprocessed -E $(SD_BENCH_SRC) 2>/dev/null`; \
	 bad=""; \
	 for spec in LoggingSet:true CrcStart:true GetData:true Delete:true \
	             ListDir:false DirectorySet:false; do \
	   cb=SCPI_StorageSD`echo "$$spec" | cut -d: -f1`; \
	   want=`echo "$$spec" | cut -d: -f2`; \
	   body=`printf '%s\n' "$$pp" | awk -v nm="scpi_result_t $$cb(scpi_t * context)" 'index($$0,nm)==1{g=1} g{print} g&&/^\}/{exit}'`; \
	   n=`printf '%s\n' "$$body" | grep -c 'SD_ValidatePathParam('`; \
	   got=`printf '%s\n' "$$body" | grep -oE 'SD_ValidatePathParam\([^)]*, (true|false)\)' | grep -oE '(true|false)' | head -1`; \
	   if [ "$$n" -ne 1 ] || [ "$$got" != "$$want" ]; then \
	     bad="$$bad $$cb(calls=$$n,flag=$$got,want=$$want)"; \
	   fi; \
	 done; \
	 test -z "$$bad" \
	  || { echo "ERROR: #1124 r10 -- SD_ValidatePathParam's role table is violated"; \
	       echo "       in $(SD_BENCH_SRC):"; \
	       for b in $$bad; do echo "         $$b"; done; \
	       echo "       Each callback must validate its operand as the half its"; \
	       echo "       SUBJECT requires -- file operands reject a trailing '/',"; \
	       echo "       directory operands accept and strip it. ⛔ Do NOT infer the"; \
	       echo "       role from the length variable's name: SCPI_StorageSDListDir"; \
	       echo "       writes opDirectory while its variable is called fileLen,"; \
	       echo "       and reading that name instead of the destination is what"; \
	       echo "       broke LISt? \"DAQiFi/\" at r9."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(BENCH_BIN) test_943_bench_stall_bound.c

$(BIN970): check_ci_path_filters.py ../../.github/workflows/host-tests.yml \
           test_970_encoder_consume_accounting.c test_framework.h $(STREAMING_SRC) $(STREAMING_HDR) $(JSON_ENC_SRC) $(NANOPB_ENC_SRC) $(CSV_ENC_SRC) $(RECIPES)
	@python3 check_ci_path_filters.py >/dev/null \
	  || { python3 check_ci_path_filters.py; exit 1; }
	@for fld in encoderDroppedSamples encoderDroppedSamplesSteady; do \
	   n=`grep -oE "gStreamStats\.$$fld[[:space:]]*(\+\+|\+=|-=|=[^=])" $(STREAMING_SRC) | wc -l`; \
	   [ "$$n" -eq 1 ] \
	     || { echo "ERROR: $(STREAMING_SRC) has $$n write sites on gStreamStats.$$fld, expected exactly 1."; \
	          echo "       The #970 fix left ONE writer -- Streaming_ReportEncoderSampleLoss --"; \
	          echo "       and removed the increment the encoded==0 arm used to do on a"; \
	          echo "       zero-BYTE return. A second writer means that inference is back."; \
	          echo "       This counts ANY write operator, not the literal '++' it used to"; \
	          echo "       grep: a regression reintroduced as '+= 1' matched nothing and the"; \
	          echo "       pin stayed green (audit finding, executed both directions)."; \
	          exit 1; }; \
	 done
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
	@set -- "$(JSON_ENC_SRC)" 1 "$(NANOPB_ENC_SRC)" 2 "$(CSV_ENC_SRC)" 1; \
	 while [ $$# -gt 0 ]; do \
	   f=$$1; want=$$2; shift 2; \
	   grep -qE '^[[:space:]]*#[[:space:]]*if[[:space:]]+0([[:space:]]|$$)' "$$f" \
	     && { echo "ERROR: $$f contains a '#if 0' region, and this pin cannot see it."; \
	          echo "       The count below comes from \$$(CC) -fpreprocessed, which strips"; \
	          echo "       COMMENTS but does NOT evaluate preprocessor conditionals -- a call"; \
	          echo "       inside '#if 0' is still counted (verified: it counts as 1). So the"; \
	          echo "       pin would vouch for a reporter call that never compiles."; \
	          echo "       Resolve by hand: delete the dead region, or re-derive this pin."; \
	          exit 1; } || true; \
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
	@# ⛔ #991 PART 3's NUMBERS COME FROM THE FIRMWARE, so pin them. Part 3
	@# asserts that one DIO+PB encode failure destroys TWO samples, and that
	@# figure is not a modelling choice -- it is read from NanoPB_Encoder.c's
	@# AIN-failure arm, whose own comment says "exactly one AIN sample dies
	@# here -- plus the DIO sample when this is the message carrying it
	@# (dioS > 0)". The count arm above proves there are >= 2 report CALLS in
	@# that file; it says nothing about what they report. If this argument
	@# became 1u, Part 3's expectations would be silently wrong and every
	@# assertion would still pass against the model.
	@#
	@# Same shape as the #1029 generator pinning the format its own test
	@# formats: a test that hard-codes a firmware constant must pin the
	@# constant, or it is testing its own copy of a number nobody checks.
	@grep -qF 'Streaming_ReportEncoderSampleLoss((dioS > 0) ? 2u : 1u);' $(NANOPB_ENC_SRC) \
	  || { echo "ERROR: $(NANOPB_ENC_SRC)'s AIN-failure arm no longer reports"; \
	       echo "       '(dioS > 0) ? 2u : 1u' samples destroyed (#991 Part 3)."; \
	       echo "       Part 3 of test_970_encoder_consume_accounting.c asserts that"; \
	       echo "       ONE DIO+PB encode failure destroys TWO samples, and it takes"; \
	       echo "       that 2 from this expression. If the arm now reports a"; \
	       echo "       different count, re-derive Part 3's expectNew column before"; \
	       echo "       changing this pin -- the test would otherwise keep passing"; \
	       echo "       against its own stale copy of the number."; \
	       exit 1; }
	@grep -qF 'Streaming_ReportEncoderSampleLoss(1u);' $(NANOPB_ENC_SRC) \
	  || { echo "ERROR: $(NANOPB_ENC_SRC)'s standalone-DIO arm no longer reports"; \
	       echo "       1 sample destroyed (#991 Part 3, the :1688 arm whose own"; \
	       echo "       comment records that it 'previously had no else at all:"; \
	       echo "       no counter, no log')."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(BIN970) test_970_encoder_consume_accounting.c
# Two separate failures with two separate messages: the function is GONE
# (renamed/moved -- the model has lost its subject) vs the function is there
# and its CODE CHANGED (the model may have gone stale). Collapsing them would
# report "hash mismatch" for a rename, sending the reader to diff a function
# that is no longer in the file.
# $(RECIPES) is a prerequisite because SD_ARM_HELPER and SD_ARM_HELPER_SHA live
# in the Makefile and CONTROL this target's guard: without it, editing either
# pin left an already-built binary up to date and make ran the stale one
# without the revised validation (#976 audit). Uses the shared RECIPES
# variable (#983) rather than a literal "Makefile", same as every other
# target below.
# `$(CDEF)` is a prerequisite because the definition RULE lives there now,
# not in hash_function.py: without it an edit to the rule would leave this
# binary up to date and the guard would run the previous rule, which is the
# same defect `$(RECIPES)` was added for one level down (#976).
CDEF := ../../tools/lint/cdef.py
# #756 r6 (audit MEDIUM, both legs): DRV_SDSPI_DetectPollExpireNow samples the
# time OUTSIDE its critical section, so a caller preempted between its sample
# and the lock can see a NEWER published detectForceAt, underflow the unsigned
# subtraction, pass the interval gate, and double-force an exclusive SPI4
# detection -- then rewind detectForceAt and mis-gate the next caller too.
#
# drv_sdspi.c is not host-compilable (Harmony/OSAL/SYS_TIME), so the test
# re-implements the gate's two shapes, same technique as $(BENCH_BIN) and
# $(BIN970). These greps are what stop the model from drifting away from the
# real one -- without them the test measures its own copy of the logic.
$(FORCEGATE_BIN): test_756_detect_force_rate_limit.c test_framework.h $(SDSPI_DRV_SRC) $(RECIPES)
	@# the STALE-SAMPLE ARM must be present, by exact form. This is the fix;
	@# if it goes, the test still passes (it models both shapes itself) while
	@# the firmware is broken again -- which is the whole failure mode a
	@# re-implementation test has.
	@grep -qF 'if (dObj->detectForceEver && (nowCount < dObj->detectForceAt))' $(SDSPI_DRV_SRC) \
	  || { echo "ERROR: $(SDSPI_DRV_SRC) no longer refuses a STALE time sample in"; \
	       echo "       DRV_SDSPI_DetectPollExpireNow (#756 r6)."; \
	       echo "       nowCount is sampled OUTSIDE the critical section, so without"; \
	       echo "       this arm a caller preempted before the lock computes"; \
	       echo "       nowCount - detectForceAt as an unsigned UNDERFLOW, passes the"; \
	       echo "       pollingIntervalMs gate, forces a second back-to-back exclusive"; \
	       echo "       SPI4 detection, and moves detectForceAt BACKWARDS."; \
	       echo "       test_756_detect_force_rate_limit.c models both shapes, so it"; \
	       echo "       keeps passing without this -- which is why the pin exists."; \
	       exit 1; }
	@# and the sample must still be taken outside the section: if someone
	@# "fixes" this by moving it inside, the arm above becomes dead and the
	@# test's premise is gone. Both facts, or neither is meaningful.
	@n=`grep -c 'const uint64_t nowCount = SYS_TIME_Counter64Get();' $(SDSPI_DRV_SRC)`; \
	 c=`grep -c 'critState = OSAL_CRIT_Enter(OSAL_CRIT_TYPE_HIGH);' $(SDSPI_DRV_SRC)`; \
	 test "$$n" -eq 1 && test "$$c" -ge 1 \
	  || { echo "ERROR: $(SDSPI_DRV_SRC)'s detect-force gate no longer has the"; \
	       echo "       shape test_756 models: nowCount samples=$$n (expected 1),"; \
	       echo "       OSAL_CRIT_Enter sites=$$c (expected >= 1)."; \
	       echo "       If the sample moved INSIDE the section the stale-sample arm"; \
	       echo "       is dead code and this test is measuring nothing -- re-derive"; \
	       echo "       both before changing this pin."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(FORCEGATE_BIN) test_756_detect_force_rate_limit.c

$(ARM_BIN): test_971_sd_arm_refusal_order.c test_framework.h $(SD_BENCH_SRC) hash_function.py $(CDEF) $(RECIPES)
	@python3 hash_function.py --find $(SD_BENCH_SRC) $(SD_ARM_HELPER) >/dev/null 2>&1 \
	  || { python3 hash_function.py --find $(SD_BENCH_SRC) $(SD_ARM_HELPER); \
	       echo "ERROR: $(SD_ARM_HELPER)() is not a single definition in $(SD_BENCH_SRC)."; \
	       echo "       test_971_sd_arm_refusal_order.c models that function, and the"; \
	       echo "       hash below extracts it from that line to its closing brace. If"; \
	       echo "       it was renamed, moved, or its signature reformatted, re-read it"; \
	       echo "       against the model and update SD_ARM_HELPER / SD_ARM_HELPER_SHA."; \
	       exit 1; }
	@python3 hash_function.py --self-test >/dev/null \
	  || { echo "ERROR: hash_function.py's own self-test fails, so the drift pin"; \
	       echo "       below cannot be trusted to see an edit. Fix the hasher"; \
	       echo "       before reading its verdict."; \
	       python3 hash_function.py --self-test; exit 1; }
	@got=$$(python3 hash_function.py $(SD_BENCH_SRC) 'static bool $(SD_ARM_HELPER)(') || exit 1; \
	 [ "$$got" = "$(SD_ARM_HELPER_SHA)" ] \
	  || { echo "ERROR: the CODE of $(SD_ARM_HELPER)() in $(SD_BENCH_SRC) has changed."; \
	       echo "         expected sha256 $(SD_ARM_HELPER_SHA)"; \
	       echo "         got      sha256 $$got"; \
	       echo "       This is a tripwire, not a verdict: the change may be perfectly"; \
	       echo "       correct. Re-read that function against the model in"; \
	       echo "       test_971_sd_arm_refusal_order.c -- if the model still describes"; \
	       echo "       it, update SD_ARM_HELPER_SHA; if it does not, fix the model."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(ARM_BIN) test_971_sd_arm_refusal_order.c

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
	@test "$$(grep -c 'pSample->Timestamp < 1' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: 'pSample->Timestamp < 1' count in $(SCPIDAC_SRC) changed from 2."; \
	       echo "       Audit correction on PR #1147 (disposition fix_now) made the all-channel"; \
	       echo "       form of SCPI_DACVoltageGet use the IDENTICAL staleness check as the"; \
	       echo "       single-channel form (both now error a channel whose Timestamp is still 0,"; \
	       echo "       instead of the all-channel form substituting a fabricated 0.0V). test_1034's"; \
	       echo "       get_single_FIXED() and get_all_FIXED() both mirror this exact check --"; \
	       echo "       re-derive both if this moved, and re-derive the whole test file if only one"; \
	       echo "       of the two branches still uses it (that would reopen the asymmetry #1147"; \
	       echo "       exists to close)."; \
	       exit 1; }
	@grep -qF 'const AOutSample invalidatedSample = {0};' $(SCPIDAC_SRC) \
	  || { echo "ERROR: DAC_EnsureHardwareInitialized's post-reinit invalidation local"; \
	       echo "       ('invalidatedSample') is gone from $(SCPIDAC_SRC). test_1034's"; \
	       echo "       reinit_invalidate_shape() mirrors this loop -- re-derive the test."; \
	       exit 1; }
	@i=`grep -nF 'bool initSucceeded = DAC7718_Init(dacInstanceId, 1);' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 v=`grep -nF 'const AOutSample invalidatedSample = {0};' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 f=`grep -nF 'if (!initSucceeded) {' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 p=`grep -nF 'dacHardwareInitialized = true;   // publish READY ...' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 test -n "$$i" && test -n "$$v" && test -n "$$f" && test -n "$$p" \
	   && test "$$i" -lt "$$v" && test "$$v" -lt "$$f" && test "$$f" -lt "$$p" \
	   || { echo "ERROR: #1034's invalidation step is no longer ordered AFTER DAC7718_Init() runs,"; \
	        echo "       BEFORE the initSucceeded failure check, and BEFORE dacHardwareInitialized"; \
	        echo "       is published true in $(SCPIDAC_SRC) (init='$$i' invalidate='$$v'"; \
	        echo "       fail-check='$$f' publish='$$p')."; \
	        echo "       Invalidating before the reset actually happens, or skipping invalidation"; \
	        echo "       on a DAC7718_Init() FAILURE (round-2 Qodo finding, importance 10 --"; \
	        echo "       DAC7718_Init()'s post-reset-pulse failure paths still leave the hardware"; \
	        echo "       physically reset), or invalidating after READY is already published, all"; \
	        echo "       reopen the confidently-wrong readback #1034 exists to close -- re-derive"; \
	        echo "       the test against the new placement."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(DACREINIT_BIN) test_1034_dac_aout_invalidated_on_reinit.c

# #1154: build-time guard that the real SCPIADC.c still rejects a non-finite
# CONF:ADC:chanCALM/chanCALB coefficient -- see the comment above
# $(CALFINITE_BIN)'s definition and test_1154_adc_cal_coefficient_finite.c's
# header for what this fix is (and is deliberately not) in scope for.
#
# Tightened per PR #1157 review (worker pre-audit comment, 2026-09-21): the
# original guard was presence-only (`grep -qF`, no counts, no negative-form
# check, no comment-stripping), and TWO mutants of SCPIADC.c passed it AND
# the host test outright: (A) `if (isfinite(value))` -> `if (!isfinite(value))`
# -- accepts inf/NaN and rejects every finite value, worse than the #1154
# defect -- and (B) `if (!AdcCalCoefficientFinite(...)) {` ->
# `(void)AdcCalCoefficientFinite(...); if (0) {` -- reinstates the #1154
# defect exactly, since the helper is still called (and still logs) but its
# return value is discarded. Mirrors $(SCAN1112_BIN)'s technique in this
# SAME file: strip comment lines first, pin exact counts (not mere
# presence), pin an explicit negative-form's absence, and pin ordering.
#
# Tightened AGAIN, same PR, adversarial audit round 2 (2026-09-21): presence
# + count + ORDER still cannot see NESTING. Moving the closing brace of
# `if (isfinite(value)) {` down to sit right before `return true;` --
#     if (isfinite(value)) {
#     }
#     return true;             /* now unconditional */
#     LOG_E(...); SCPI_ErrorPush(...); return false;   /* both dead */
# -- keeps npos/nneg/nerr/nt/nf at 1/0/1/1/1 and keeps isfinite-line <
# true-line < false-line, so it passed every check above outright and is
# the #1154 defect again (accepts inf/NaN unconditionally). Setter-side
# guards (mbody/bbody below) were ALREADY immune to this exact trick --
# they pin that `return SCPI_RES_ERR;` is the line IMMEDIATELY after the
# call, i.e. adjacency, not just ordering. This gives the accept arm the
# same treatment: `if (isfinite(value)) {`, `return true;` and the closing
# `}` must be three CONSECUTIVE lines, checked the same line-oriented way
# (no parser) by indexing p+1/p+2 off the isfinite line already found above.
#
# That still isn't enough on its own: a `/* */` block comment or a
# `#if 0`/`#endif` span, with the delimiters on their own lines, hides the
# pinned text from a human and from the CPU while leaving it perfectly
# intact for `grep -v '^[[:space:]]*[*/]'` to find (that filter only drops
# lines that themselves OPEN with `*` or `/`, not a comment SPAN) -- so
# `if (isfinite(value)) { return true; }` sitting dead inside either wrapper
# still satisfies every check above, including the new adjacency one,
# while the compiler never runs it. Rather than hand-roll a second
# comment/preprocessor state machine (this project's own history is
# explicit that a hand-rolled parser-substitute is the wrong tool -- build
# the oracle from the reference implementation), this re-extracts the SAME
# range through the real C preprocessor (`$(CC) -E -P -xc -`, no headers
# included, so no macro surprises) and re-pins presence + the same
# adjacency triple against ITS output. Comments and disabled `#if 0` spans
# vanish under real preprocessing the way they vanish at actual compile
# time; live code does not, so this is a strict tightening, not a
# different check -- verified byte-identical to the plain-text extraction
# on the real (unmutated) source.
#
# Extended AGAIN, same PR (PR #1157 review comment
# https://github.com/daqifi/daqifi-nyquist-firmware/pull/1157#issuecomment-5764273765):
# the same comment/`#if 0` hiding trick reproduces against the CALM and CALB
# SETTER guards (mbody/bbody below) exactly as it did against the helper --
# `grep -v '^[[:space:]]*[*/]'` strips only the `/*`/`*/` delimiter lines
# themselves, not the guarded call-shape text between them, so wrapping
# either setter's `if (!AdcCalCoefficientFinite(...)) { return SCPI_RES_ERR;
# }` in a block comment or `#if 0`/`#endif` still satisfies every plain-text
# check below (presence, write-count, and check-before-write adjacency)
# while the compiler discards it, reinstating #1154 on that channel's
# setter. mbody/bbody now get the SAME preprocessor re-extraction and
# re-pin (call-shape presence + immediate `return SCPI_RES_ERR;` adjacency)
# against their own `$(CC) -E -P -xc -` output, right after each one's
# existing plain-text checks -- same instrument, same technique, applied to
# all three guarded bodies in this rule rather than fbody alone. Every
# existing plain-text check on mbody/bbody is unchanged; this only adds a
# second, stricter pass on top, per "keep every existing check that already
# works."
#
# KNOWN, ACCEPTED RESIDUAL (adversarial audit round on PR #1157, 2026-09-21,
# CONFIRMED and reproduced independently): every check below -- presence,
# counts, ordering, adjacency, and the preprocessor re-extraction above -- is
# a text/preprocessor scan, so none of it can observe RUNTIME control flow.
# Wrapping a setter's WHOLE `if (!AdcCalCoefficientFinite(...)) { return
# SCPI_RES_ERR; }` block in a runtime `if (0) { ... }` -- not the
# preprocessor's `#if 0`, which the pass above DOES catch -- leaves the
# guarded text, its ordering and its adjacency to the write completely
# intact, so it satisfies every check in this rule outright while the
# compiler never executes the block: the mutated setter accepts and stores a
# non-finite coefficient with no SCPI error, reinstating #1154 exactly, and
# this whole suite still reports green (`run_1154_tests` still passes 6
# tests / 18 assertions, because that binary only ever exercises its own
# re-implemented model, never this file). This is NOT a hypothetical --
# reproduced against the real source at this PR's head.
#
# This is intentionally NOT closed with a fourth special-cased text pattern.
# Arbitrary C admits unboundedly many ways to make matched text ineffective
# (this is the third or fourth shape found in this rule alone), and this
# guard is already ~190 Makefile lines pinning a 6-line helper -- the
# durable fix is not another pattern here but to stop proving firmware
# contracts by splicing and grepping, which is tracked project-wide as
# firmware issue #1033
# (https://github.com/daqifi/daqifi-nyquist-firmware/issues/1033) and is
# where this residual is recorded, not silently.
#
# Because this guard cannot close that class, it is NOT the authoritative
# proof that a non-finite coefficient is rejected on real firmware -- it is
# a fast, host-side check that the source has not visibly regressed in the
# shapes a text scan can see. The authoritative behavioural regression is
# daqifi-python-test-suite PR #464
# (https://github.com/daqifi/daqifi-python-test-suite/pull/464),
# test_1154_adc_cal_coefficient_finite.py, which drives the real BUILT
# firmware over SCPI on hardware and so catches every mutation class above,
# the runtime `if (0)` bypass included, because it observes what the device
# actually does rather than what its source text says. Bench-verified 8
# PASS / 0 FAIL at firmware crc32 `553E07E4`, built from firmware commit
# `39d1381a8` on this same branch; SCPIADC.c is byte-identical from that
# commit through this rule's own PR head. That test-suite regression is NOT
# run in CI (it needs a physical board), so in CI this Makefile guard is the
# ONLY continuously-enforced check of this behaviour, and it is the weaker
# of the two.
$(CALFINITE_BIN): test_1154_adc_cal_coefficient_finite.c test_framework.h $(SCPIADC_SRC) $(RECIPES)
	@fbody=`sed -n '/^static bool AdcCalCoefficientFinite(/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$fbody" \
	   || { echo "ERROR: AdcCalCoefficientFinite() was not found in $(SCPIADC_SRC),"; \
	        echo "       or no longer opens at column 0 -- the guards below can see"; \
	        echo "       nothing, and a guard that examines nothing must fail."; \
	        exit 1; }; \
	 npos=`printf '%s\n' "$$fbody" | grep -cF 'if (isfinite(value)) {'`; \
	 nneg=`printf '%s\n' "$$fbody" | grep -cF '!isfinite'`; \
	 nerr=`printf '%s\n' "$$fbody" | grep -cF 'SCPI_ERROR_DATA_OUT_OF_RANGE'`; \
	 nt=`printf '%s\n' "$$fbody" | grep -cE 'return[[:space:]]+true;'`; \
	 nf=`printf '%s\n' "$$fbody" | grep -cE 'return[[:space:]]+false;'`; \
	 test "$$npos" -eq 1 && test "$$nneg" -eq 0 && test "$$nerr" -eq 1 \
	   && test "$$nt" -eq 1 && test "$$nf" -eq 1 \
	   || { echo "ERROR: AdcCalCoefficientFinite() in $(SCPIADC_SRC) no longer has"; \
	        echo "       exactly the shape this guard pins (positive 'if (isfinite(value))"; \
	        echo "       {' occurrences=$$npos, negated '!isfinite' occurrences=$$nneg,"; \
	        echo "       SCPI_ERROR_DATA_OUT_OF_RANGE=$$nerr, return true=$$nt,"; \
	        echo "       return false=$$nf; want 1 0 1 1 1). An inverted branch"; \
	        echo "       (isfinite -> !isfinite) accepts inf/NaN and rejects every"; \
	        echo "       finite value -- worse than the #1154 defect -- and it used to"; \
	        echo "       pass this guard outright; test_1154's SetCalCoefficient()"; \
	        echo "       models the correct polarity and would pass regardless."; \
	        exit 1; }; \
	 p=`printf '%s\n' "$$fbody" | grep -nF 'if (isfinite(value)) {' | cut -d: -f1`; \
	 t=`printf '%s\n' "$$fbody" | grep -nE 'return[[:space:]]+true;' | cut -d: -f1`; \
	 f=`printf '%s\n' "$$fbody" | grep -nE 'return[[:space:]]+false;' | cut -d: -f1`; \
	 test "$$p" -lt "$$t" && test "$$t" -lt "$$f" \
	   || { echo "ERROR: AdcCalCoefficientFinite() in $(SCPIADC_SRC) no longer returns"; \
	        echo "       true INSIDE the isfinite() branch and false afterward"; \
	        echo "       (isfinite-line='$$p' return-true='$$t' return-false='$$f';"; \
	        echo "       want isfinite < true < false)."; \
	        exit 1; }; \
	 tadj=`printf '%s\n' "$$fbody" | sed -n "$$((p+1))p"`; \
	 badj=`printf '%s\n' "$$fbody" | sed -n "$$((p+2))p"`; \
	 printf '%s\n' "$$tadj" | grep -qE '^[[:space:]]*return[[:space:]]+true;[[:space:]]*$$' \
	   && printf '%s\n' "$$badj" | grep -qE '^[[:space:]]*}[[:space:]]*$$' \
	   || { echo "ERROR: AdcCalCoefficientFinite() in $(SCPIADC_SRC) no longer has"; \
	        echo "       'return true;' and the closing brace as the two lines"; \
	        echo "       IMMEDIATELY following 'if (isfinite(value)) {' (line"; \
	        echo "       $$p+1 got '$$tadj', line $$p+2 got '$$badj'; want"; \
	        echo "       'return true;' then '}')."; \
	        echo "       The count and ordering checks above pin PRESENCE and"; \
	        echo "       ORDER but not NESTING: moving the closing brace down to"; \
	        echo "       sit just above an unconditional 'return true;' still"; \
	        echo "       satisfies every one of them while turning the isfinite()"; \
	        echo "       test into a no-op that accepts inf/NaN unconditionally --"; \
	        echo "       this IS the #1154 defect, reinstated through the one"; \
	        echo "       shape those checks cannot see."; \
	        exit 1; }; \
	 fbodycpp=`sed -n '/^static bool AdcCalCoefficientFinite(/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | $(CC) -E -P -xc - 2>/dev/null`; \
	 pc=`printf '%s\n' "$$fbodycpp" | grep -cF 'if (isfinite(value)) {'`; \
	 test "$$pc" -eq 1 \
	   || { echo "ERROR: AdcCalCoefficientFinite() in $(SCPIADC_SRC): the"; \
	        echo "       'if (isfinite(value)) {' this guard pins survives the plain"; \
	        echo "       comment-stripping grep above (npos=1) but DISAPPEARS when the"; \
	        echo "       body is run through the real C preprocessor (occurrences=$$pc,"; \
	        echo "       want 1) -- that only happens when it is dead text: inside a"; \
	        echo "       /* block comment */ whose delimiters sit on their own lines, or"; \
	        echo "       inside a '#if 0' / '#endif' span. Either shape still SHOWS the"; \
	        echo "       check to every line-oriented grep above while the compiler"; \
	        echo "       discards it -- so the check never runs and a non-finite"; \
	        echo "       coefficient reaches the runtime array again. This IS the #1154"; \
	        echo "       defect, reinstated by disabling the guard rather than editing it."; \
	        exit 1; }; \
	 pc1=`printf '%s\n' "$$fbodycpp" | grep -nF 'if (isfinite(value)) {' | cut -d: -f1`; \
	 tadjc=`printf '%s\n' "$$fbodycpp" | sed -n "$$((pc1+1))p"`; \
	 badjc=`printf '%s\n' "$$fbodycpp" | sed -n "$$((pc1+2))p"`; \
	 printf '%s\n' "$$tadjc" | grep -qE '^[[:space:]]*return[[:space:]]+true;[[:space:]]*$$' \
	   && printf '%s\n' "$$badjc" | grep -qE '^[[:space:]]*}[[:space:]]*$$' \
	   || { echo "ERROR: AdcCalCoefficientFinite() in $(SCPIADC_SRC): after real"; \
	        echo "       preprocessing, 'return true;' and the closing brace are no"; \
	        echo "       longer the two lines immediately following"; \
	        echo "       'if (isfinite(value)) {' (line $$pc1+1 got '$$tadjc', line"; \
	        echo "       $$pc1+2 got '$$badjc') -- same defect as the plain-text adjacency"; \
	        echo "       check above, surviving because of an intervening comment or"; \
	        echo "       preprocessor directive that this preprocessed copy strips."; \
	        exit 1; }
	@mbody=`sed -n '/^static scpi_result_t ADCChanCalmSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$mbody" \
	   || { echo "ERROR: ADCChanCalmSetClaimed() was not found in $(SCPIADC_SRC),"; \
	        echo "       or no longer opens at column 0."; \
	        exit 1; }; \
	 nc=`printf '%s\n' "$$mbody" | grep -cF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALM")) {'`; \
	 nw=`printf '%s\n' "$$mbody" | grep -cF '.CalM = param2;'`; \
	 test "$$nc" -eq 1 && test "$$nw" -eq 1 \
	   || { echo "ERROR: ADCChanCalmSetClaimed() in $(SCPIADC_SRC) no longer calls"; \
	        echo "       AdcCalCoefficientFinite() in the exact guarded-call shape this"; \
	        echo "       guard pins, or CalM is written more/fewer than once (call-shape"; \
	        echo "       occurrences=$$nc, CalM writes=$$nw; want 1 1). Discarding the"; \
	        echo "       call's return value (e.g. '(void)Adc...; if (0) {') still calls"; \
	        echo "       the helper and still logs, but reinstates the #1154 defect"; \
	        echo "       exactly -- a non-finite coefficient reaches the runtime array"; \
	        echo "       again. A second, unguarded CalM write is the same defect by a"; \
	        echo "       different door."; \
	        exit 1; }; \
	 c=`printf '%s\n' "$$mbody" | grep -nF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALM")) {' | cut -d: -f1`; \
	 w=`printf '%s\n' "$$mbody" | grep -nF '.CalM = param2;' | cut -d: -f1`; \
	 nextline=`printf '%s\n' "$$mbody" | sed -n "$$((c+1))p"`; \
	 test "$$c" -lt "$$w" \
	   && printf '%s\n' "$$nextline" | grep -qE 'return[[:space:]]+SCPI_RES_ERR;' \
	   || { echo "ERROR: ADCChanCalmSetClaimed() in $(SCPIADC_SRC) no longer returns"; \
	        echo "       SCPI_RES_ERR; as the line right after the finite-check, before"; \
	        echo "       the CalM write (check='$$c' CalM-write='$$w', next line after"; \
	        echo "       check='$$nextline'). An emptied-out if-block still shows the"; \
	        echo "       call but falls through to the write on a non-finite value --"; \
	        echo "       this IS the #1154 defect."; \
	        exit 1; }
	@mbodycpp=`sed -n '/^static scpi_result_t ADCChanCalmSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | $(CC) -E -P -xc - 2>/dev/null`; \
	 ncc=`printf '%s\n' "$$mbodycpp" | grep -cF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALM")) {'`; \
	 test "$$ncc" -eq 1 \
	   || { echo "ERROR: ADCChanCalmSetClaimed() in $(SCPIADC_SRC): the guarded-call"; \
	        echo "       shape this guard pins survives the plain comment-stripping grep"; \
	        echo "       above (nc=1) but DISAPPEARS when the body is run through the real"; \
	        echo "       C preprocessor (occurrences=$$ncc, want 1) -- that only happens"; \
	        echo "       when it is dead text: inside a /* block comment */ whose"; \
	        echo "       delimiters sit on their own lines, or inside a '#if 0' / '#endif'"; \
	        echo "       span. Either shape still SHOWS the check to every line-oriented"; \
	        echo "       grep above while the compiler discards it -- so the check never"; \
	        echo "       runs and a non-finite CALM coefficient reaches the runtime array"; \
	        echo "       again. This IS the #1154 defect, reinstated by disabling the"; \
	        echo "       guard rather than editing it."; \
	        exit 1; }; \
	 cc=`printf '%s\n' "$$mbodycpp" | grep -nF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALM")) {' | cut -d: -f1`; \
	 nextlinec=`printf '%s\n' "$$mbodycpp" | sed -n "$$((cc+1))p"`; \
	 printf '%s\n' "$$nextlinec" | grep -qE 'return[[:space:]]+SCPI_RES_ERR;' \
	   || { echo "ERROR: ADCChanCalmSetClaimed() in $(SCPIADC_SRC): after real"; \
	        echo "       preprocessing, 'return SCPI_RES_ERR;' is no longer the line"; \
	        echo "       immediately following the finite-check (line $$cc+1 got"; \
	        echo "       '$$nextlinec') -- same defect as the plain-text adjacency check"; \
	        echo "       above, surviving because of an intervening comment or"; \
	        echo "       preprocessor directive that this preprocessed copy strips."; \
	        exit 1; }
	@bbody=`sed -n '/^static scpi_result_t ADCChanCalbSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$bbody" \
	   || { echo "ERROR: ADCChanCalbSetClaimed() was not found in $(SCPIADC_SRC),"; \
	        echo "       or no longer opens at column 0."; \
	        exit 1; }; \
	 nc=`printf '%s\n' "$$bbody" | grep -cF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALB")) {'`; \
	 nw=`printf '%s\n' "$$bbody" | grep -cF '.CalB = param2;'`; \
	 test "$$nc" -eq 1 && test "$$nw" -eq 1 \
	   || { echo "ERROR: ADCChanCalbSetClaimed() in $(SCPIADC_SRC) no longer calls"; \
	        echo "       AdcCalCoefficientFinite() in the exact guarded-call shape this"; \
	        echo "       guard pins, or CalB is written more/fewer than once (call-shape"; \
	        echo "       occurrences=$$nc, CalB writes=$$nw; want 1 1). Discarding the"; \
	        echo "       call's return value (e.g. '(void)Adc...; if (0) {') still calls"; \
	        echo "       the helper and still logs, but reinstates the #1154 defect"; \
	        echo "       exactly -- a non-finite coefficient reaches the runtime array"; \
	        echo "       again. A second, unguarded CalB write is the same defect by a"; \
	        echo "       different door."; \
	        exit 1; }; \
	 c=`printf '%s\n' "$$bbody" | grep -nF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALB")) {' | cut -d: -f1`; \
	 w=`printf '%s\n' "$$bbody" | grep -nF '.CalB = param2;' | cut -d: -f1`; \
	 nextline=`printf '%s\n' "$$bbody" | sed -n "$$((c+1))p"`; \
	 test "$$c" -lt "$$w" \
	   && printf '%s\n' "$$nextline" | grep -qE 'return[[:space:]]+SCPI_RES_ERR;' \
	   || { echo "ERROR: ADCChanCalbSetClaimed() in $(SCPIADC_SRC) no longer returns"; \
	        echo "       SCPI_RES_ERR; as the line right after the finite-check, before"; \
	        echo "       the CalB write (check='$$c' CalB-write='$$w', next line after"; \
	        echo "       check='$$nextline'). An emptied-out if-block still shows the"; \
	        echo "       call but falls through to the write on a non-finite value --"; \
	        echo "       this IS the #1154 defect."; \
	        exit 1; }
	@bbodycpp=`sed -n '/^static scpi_result_t ADCChanCalbSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | $(CC) -E -P -xc - 2>/dev/null`; \
	 nccb=`printf '%s\n' "$$bbodycpp" | grep -cF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALB")) {'`; \
	 test "$$nccb" -eq 1 \
	   || { echo "ERROR: ADCChanCalbSetClaimed() in $(SCPIADC_SRC): the guarded-call"; \
	        echo "       shape this guard pins survives the plain comment-stripping grep"; \
	        echo "       above (nc=1) but DISAPPEARS when the body is run through the real"; \
	        echo "       C preprocessor (occurrences=$$nccb, want 1) -- that only happens"; \
	        echo "       when it is dead text: inside a /* block comment */ whose"; \
	        echo "       delimiters sit on their own lines, or inside a '#if 0' / '#endif'"; \
	        echo "       span. Either shape still SHOWS the check to every line-oriented"; \
	        echo "       grep above while the compiler discards it -- so the check never"; \
	        echo "       runs and a non-finite CALB coefficient reaches the runtime array"; \
	        echo "       again. This IS the #1154 defect, reinstated by disabling the"; \
	        echo "       guard rather than editing it."; \
	        exit 1; }; \
	 ccb=`printf '%s\n' "$$bbodycpp" | grep -nF 'if (!AdcCalCoefficientFinite(context, param2, "CONF:ADC:chanCALB")) {' | cut -d: -f1`; \
	 nextlinecb=`printf '%s\n' "$$bbodycpp" | sed -n "$$((ccb+1))p"`; \
	 printf '%s\n' "$$nextlinecb" | grep -qE 'return[[:space:]]+SCPI_RES_ERR;' \
	   || { echo "ERROR: ADCChanCalbSetClaimed() in $(SCPIADC_SRC): after real"; \
	        echo "       preprocessing, 'return SCPI_RES_ERR;' is no longer the line"; \
	        echo "       immediately following the finite-check (line $$ccb+1 got"; \
	        echo "       '$$nextlinecb') -- same defect as the plain-text adjacency check"; \
	        echo "       above, surviving because of an intervening comment or"; \
	        echo "       preprocessor directive that this preprocessed copy strips."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(CALFINITE_BIN) test_1154_adc_cal_coefficient_finite.c

$(SCPIDAC_RAILGATE_BIN): test_1100_scpidac_rail_enable_gate.c test_framework.h $(SCPIDAC_SRC) $(RECIPES)
	@grep -qE '^[[:space:]]*if \(pPowerState->powerState != POWERED_UP\) \{' $(SCPIDAC_SRC) \
	  || { echo "ERROR: DAC_EnsureHardwareInitialized's top-of-function powerState gate is gone"; \
	       echo "       from $(SCPIDAC_SRC). test_1100_scpidac_rail_enable_gate.c's model assumes"; \
	       echo "       the #1100 rail-enable gate sits immediately after this exact check -- re-derive."; \
	       exit 1; }
	@test "$$(grep -c 'BOARDRUNTIME_POWER_WRITE_VARIABLES' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: BOARDRUNTIME_POWER_WRITE_VARIABLES lookup count in $(SCPIDAC_SRC) changed"; \
	       echo "       from 2 (added by #1100). The fix needs BOTH the top-of-function rail-enable"; \
	       echo "       gate AND a second, FRESH read immediately before publishing"; \
	       echo "       dacHardwareInitialized=true -- exactly the same two-checkpoint shape #980"; \
	       echo "       pass 3 established for powerState (see the sibling guard on \$$(DAC980_BIN))."; \
	       echo "       If this count is no longer 2, one checkpoint may have been removed -- re-derive."; \
	       exit 1; }
	@test "$$(grep -c 'pPowerWriteVars->EN_5_10V_Val' $(SCPIDAC_SRC))" = "2" \
	  || { echo "ERROR: pPowerWriteVars->EN_5_10V_Val reference count in $(SCPIDAC_SRC) changed"; \
	       echo "       from 2 (the top-of-function gate + the pre-publish re-check) -- #1100's"; \
	       echo "       rail-enable gate may have moved or lost a checkpoint. Re-derive the test."; \
	       exit 1; }
	@g1=`grep -nF 'pPowerWriteVars->EN_5_10V_Val' $(SCPIDAC_SRC) | head -1 | cut -d: -f1`; \
	 g2=`grep -nF 'pPowerWriteVars->EN_5_10V_Val' $(SCPIDAC_SRC) | tail -1 | cut -d: -f1`; \
	 i=`grep -nE 'DAC7718_Init\(dacInstanceId, 1\)' $(SCPIDAC_SRC) | head -1 | cut -d: -f1`; \
	 p=`grep -nF 'dacHardwareInitialized = true;' $(SCPIDAC_SRC) | cut -d: -f1`; \
	 test -n "$$g1" && test -n "$$g2" && test -n "$$i" && test -n "$$p" \
	   && test "$$g1" -lt "$$i" && test "$$i" -lt "$$g2" && test "$$g2" -lt "$$p" \
	   || { echo "ERROR: #1100's two rail-enable checkpoints are no longer ordered"; \
	        echo "       gate1 < DAC7718_Init < gate2 < publish in $(SCPIDAC_SRC)"; \
	        echo "       (found gate1='$$g1' init='$$i' gate2='$$g2' publish='$$p')."; \
	        echo "       The count guard above proves BOTH reads exist; this proves the SECOND is a"; \
	        echo "       FRESH re-read taken AFTER DAC7718_Init() returns and BEFORE the publish."; \
	        echo "       Hoisting both above Init would restore the mid-init rail-drop bug while"; \
	        echo "       leaving every occurrence count at 2 -- which is what this guard catches."; \
	        exit 1; }
	$(CC) $(CFLAGS) -o $(SCPIDAC_RAILGATE_BIN) test_1100_scpidac_rail_enable_gate.c

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

# Every suite, and every one of the TWENTY-EIGHT binaries: main's twenty-seven
# plus this PR's own #970/#991 $(BIN970). The count is DERIVED, not carried: it is
# `run`'s prerequisite count at this head, and the previous value (TWENTY-FOUR,
# true at the 2026-10-01 refresh) was already false by this one, because main
# added $(ARM_BIN), $(NAME_BIN), $(FAILNEXT_BIN) and $(FAILNEXT_REAL_BIN) and a
# hand-written total does not move with them. Re-derive it rather than trusting
# it -- the comment at the bottom of this file records both sides' counts being
# stale for the same reason once already. Resolved the way this block always is --
# every side's target rule kept INTACT, only `run` and `clean` unioned, because a
# duplicated rule here would silently shadow one of them. This branch's comment
# said "now seven binaries", true when written and false now; corrected rather
# than carried, since a stale count is simply false to the next reader. All five
# of this branch's own variables -- BIN970, STREAMING_SRC, STREAMING_HDR,
# JSON_ENC_SRC, NANOPB_ENC_SRC -- were checked to be branch-OWN additions and not
# names main has since renamed or retired, which is the trap fw#976 hit.
$(FAILNEXT_BIN): test_981_sd_failnext_hook.c test_framework.h $(SD_MANAGER_SRC) $(SD_BENCH_SRC) $(RECIPES)
	@grep -qF 'static volatile bool gFailNextWrite = false;' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: gFailNextWrite is no longer declared in $(SD_MANAGER_SRC)."; \
	       echo "       test_981_sd_failnext_hook.c models its one-shot semantics; re-read both."; \
	       exit 1; }
	@# #1013: the ARM is USB-ONLY. This hook exists to make an SD write fail,
	@# and SCPI over TCP has no authentication, so arming must cost physical
	@# presence at the bench. Nothing else in the host suite can see this: the
	@# C tests above drive sd_card_manager's arm/consume contract directly and
	@# never go through the SCPI handler, so removing the gate leaves every one
	@# of them green. Checked as a SHAPE inside the setter's own body rather
	@# than anywhere in the file -- a ContextIsTcp call in some other handler
	@# (there are two, for #598) must not satisfy this.
	@sed -n '/^scpi_result_t SCPI_StorageSDFailNextSet(/,/^}/p' $(SD_BENCH_SRC) \
	  | grep -qF 'wifi_tcp_server_ContextIsTcp(context)' \
	  || { echo "ERROR: SCPI_StorageSDFailNextSet no longer refuses the TCP transport."; \
	       echo "       #1013 gated this ARM to USB because the command's purpose is to"; \
	       echo "       cause a write failure and TCP SCPI is unauthenticated. If this is"; \
	       echo "       deliberate, the wiki row and that decision need updating together."; \
	       exit 1; }
	@# ... and that the refusal PRECEDES the parameter read, so a refused
	@# command does no work and leaves no state behind.
	@sed -n '/^scpi_result_t SCPI_StorageSDFailNextSet(/,/^}/p' $(SD_BENCH_SRC) \
	  | grep -nE 'ContextIsTcp|SCPI_ParamInt32' | head -2 \
	  | awk -F: 'NR==1 && $$2 !~ /ContextIsTcp/ { exit 1 }' \
	  || { echo "ERROR: the #1013 TCP refusal no longer precedes SCPI_ParamInt32 in"; \
	       echo "       SCPI_StorageSDFailNextSet. A command that will not be honoured"; \
	       echo "       must not consume its argument first."; \
	       exit 1; }
	@# The QUERY is deliberately NOT gated (health visibility). If a future
	@# edit gates it too, that is a decision about hiding an armed hazard from
	@# a remote operator and belongs in review, not in a silent diff.
	@sed -n '/^scpi_result_t SCPI_StorageSDFailNextGet(/,/^}/p' $(SD_BENCH_SRC) \
	  | grep -qF 'wifi_tcp_server_ContextIsTcp' \
	  && { echo "ERROR: SCPI_StorageSDFailNextGet is now transport-gated."; \
	       echo "       #1013 left the QUERY reachable on purpose: it answers 'is a fault"; \
	       echo "       injector armed on this device', which is health visibility a remote"; \
	       echo "       operator should have. Gating it hides the hazard instead of the arm."; \
	       exit 1; } || true
	@# #981 post-audit (PR #1013 BLOCKING finding 2): the four independent
	@# `grep -qF ... || ok=0` checks this replaced only proved every TOKEN was
	@# present somewhere in the block -- not that they occurred in the right
	@# ORDER or that the arm actually gets consumed. An audit mutated the real
	@# source in memory (moving taskEXIT_CRITICAL() before the armed test, and
	@# separately flipping the real `injectWriteFailure = true;` to `= false;`)
	@# and every one of the four old checks kept passing. This is a POSITIONAL
	@# check (line numbers within the bounded block, strictly increasing) and
	@# it now also checks for `injectWriteFailure = true;`, which the old
	@# version never looked for at all -- that omission is exactly what let
	@# the second mutation above through silently. Positional, not just
	@# present, also means the UNMOUNT_DISK gate token below cannot be
	@# reordered to after the clear/inject without failing this too.
	@sed -n '/bool injectWriteFailure = false;/,/if (injectWriteFailure) {/p' $(SD_MANAGER_SRC) > /tmp/.fw981_block.$$$$; \
	 enter=`grep -nF 'taskENTER_CRITICAL();' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 gatestart=`grep -nF 'if (gFailNextWrite &&' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 gatestate=`grep -nF 'SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK) {' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 clear=`grep -nF 'gFailNextWrite = false;' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 inject=`grep -nF 'injectWriteFailure = true;' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 exitc=`grep -nF 'taskEXIT_CRITICAL();' /tmp/.fw981_block.$$$$ | head -1 | cut -d: -f1`; \
	 rm -f /tmp/.fw981_block.$$$$; \
	 test -n "$$enter" && test -n "$$gatestart" && test -n "$$gatestate" \
	   && test -n "$$clear" && test -n "$$inject" && test -n "$$exitc" \
	   && test "$$enter" -lt "$$gatestart" && test "$$gatestart" -lt "$$gatestate" \
	   && test "$$gatestate" -lt "$$clear" && test "$$clear" -lt "$$inject" \
	   && test "$$inject" -lt "$$exitc" \
	  || { echo "ERROR: the FAILNext consume site in $(SD_MANAGER_SRC) is no"; \
	       echo "       longer, IN THIS ORDER: taskENTER_CRITICAL() ->"; \
	       echo "       if(gFailNextWrite && ...UNMOUNT_DISK){ clear;"; \
	       echo "       inject=true } -> taskEXIT_CRITICAL(). Token PRESENCE"; \
	       echo "       alone is not enough -- moving taskEXIT_CRITICAL before"; \
	       echo "       the test still has every token present but destroys the"; \
	       echo "       read-modify-write atomicity, and dropping the"; \
	       echo "       UNMOUNT_DISK gate re-opens the truncation this hook was"; \
	       echo "       corrected to close (enter=$$enter gatestart=$$gatestart"; \
	       echo "       gatestate=$$gatestate clear=$$clear inject=$$inject"; \
	       echo "       exit=$$exitc)."; \
	       echo "       test_981_sd_failnext_hook.c models exactly this shape;"; \
	       echo "       re-read both before changing either."; \
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

# #981 (post-audit): the BEHAVIOURAL half -- see FAILNEXT_REAL_BIN's comment
# above. Extracts the whole real SDCardWrite() verbatim, splices it between
# the checked-in head/tail scaffolding, compiles the result, and RUNS it; a
# non-zero exit fails the build exactly like any other test binary here.
# `$(SD_MANAGER_SRC)` is a prerequisite so an edit to the real snippet
# re-triggers this; the head/tail files are prerequisites so an edit to the
# assertions does too.
$(FAILNEXT_REAL_BIN): test_981_sd_failnext_real_consume.head.c test_981_sd_failnext_real_consume.tail.c \
                     $(FW_UTIL)/CRC32.h $(FW_UTIL)/CRC32.c $(SD_MANAGER_SRC) $(RECIPES)
	@sed -n '/static int SDCardWrite() {/,/^}/p' $(SD_MANAGER_SRC) > /tmp/.fw981_real_body.$$$$; \
	 lines=`wc -l < /tmp/.fw981_real_body.$$$$ | tr -d ' '`; \
	 last=`tail -1 /tmp/.fw981_real_body.$$$$ | tr -d '[:space:]'`; \
	 test "$$lines" -ge 20 && test "$$lines" -le 300 && test "$$last" = "}" \
	  || { echo "ERROR: could not extract SDCardWrite() from $(SD_MANAGER_SRC)."; \
	       echo "       The range is 'static int SDCardWrite() {' through the"; \
	       echo "       next line starting in column 0 with '}', i.e. the WHOLE"; \
	       echo "       function; got $$lines lines ending in '$$last'. Either"; \
	       echo "       the signature was reworded (0 lines), or the closing"; \
	       echo "       brace is no longer in column 0 so the range ran to EOF."; \
	       echo "       DELIBERATELY a loose sanity bound, not an exact count:"; \
	       echo "       a tight count would fail the BUILD on the very source"; \
	       echo "       mutations this binary is supposed to catch by RUNNING"; \
	       echo "       the code, turning a precise behavioural verdict back"; \
	       echo "       into a text-shape one. See the head file's round-2/3"; \
	       echo "       history for why that distinction cost two rounds."; \
	       rm -f /tmp/.fw981_real_body.$$$$; \
	       exit 1; }; \
	 cat test_981_sd_failnext_real_consume.head.c /tmp/.fw981_real_body.$$$$ test_981_sd_failnext_real_consume.tail.c > /tmp/.fw981_real_gen.$$$$.c; \
	 rm -f /tmp/.fw981_real_body.$$$$; \
	 $(CC) $(CFLAGS) -I$(FW_UTIL) -o $(FAILNEXT_REAL_BIN) /tmp/.fw981_real_gen.$$$$.c $(FW_UTIL)/CRC32.c; \
	 status=$$?; \
	 rm -f /tmp/.fw981_real_gen.$$$$.c; \
	 exit $$status

# Every suite, and every one of the eight binaries. Merge conflict (fw#961
# JSONESC_BIN vs fw#983 SUSPEND_BIN, both merging around the same time)
# resolved by keeping each side's target intact and merging only `run` and
# `clean` -- a duplicated rule here would silently shadow one of them.
# Every suite, and every one of the TWENTY-FIVE binaries: main's twenty-three plus
# this PR's own #981 $(FAILNEXT_BIN) and $(FAILNEXT_REAL_BIN). Resolved as this block
# always is -- every side's target rule kept INTACT, only `run` and `clean` unioned.
# All three of this branch's own variables (FAILNEXT_BIN, FAILNEXT_REAL_BIN,
# SD_MANAGER_SRC) were verified defined on this head and absent from main, i.e.
# genuine additions rather than names main has renamed or retired -- the fw#976 trap.

# $(RECIPES) is main's (#983) fix and applies to SPI913_BIN too, because it
# carries grep guards: a guard re-run only when its SOURCE changes is not
# re-run when the GUARD changes.
#
# This used to open with a count of the binaries and a merge ordinal, both of
# which went stale the moment the next branch landed -- it still said "seven"
# and "Third merge" while the list above `run` had reached twelve and the
# eighth. A count belongs in exactly one place, and that place is the banner
# over `run`, which is the line a resolution actually has to touch.
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
# #908 (r9): like CNT_IN_FN but anchored on the DEFINITION, not the first
# declaration. CNT_IN_FN matches "^(static )?scpi_result_t <fn>[(]" and then
# exits at the next such line -- so for a function with a FORWARD DECLARATION
# it starts at the prototype and exits immediately, counting 0. Both
# ADCChanCalm/CalbSetClaimed are forward-declared, so the generic helper
# reports 0 for a body that plainly contains the pattern.
# ⚠ A guard that expects ABSENCE would PASS vacuously on that 0, so this is
# worth knowing beyond this row.
CNT_IN_FN_DEF = awk -v fn="$(2)" -v pat='$(3)' \
	'$$0 ~ "^(static )?scpi_result_t " fn "[(]" && /{[[:space:]]*$$/ { f=1; next } \
	 f && /^(static )?scpi_result_t .*{[[:space:]]*$$/                { exit } \
	 f && index($$0, pat)                                             { n++ } \
	 END { print n+0 }' $(1)

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

$(NAME_BIN): test_958_sd_benchmark_filename_collision.c test_framework.h $(SD_BENCH_SRC) $(SD_MANAGER_SRC) $(SD_MGR_HDR) $(RECIPES)
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
	@grep -qE 'probes[[:space:]]*>=[[:space:]]*SD_CARD_MANAGER_UNIQUE_TARGET_MAX' $(SD_MANAGER_SRC) \
	  && tr -d '\r' < $(SD_MANAGER_SRC) | tr '\n' ' ' \
	     | grep -qE 'e == SYS_FS_ERROR_NO_FILE[[:space:]]*[|][|][[:space:]]*e == SYS_FS_ERROR_NO_PATH[^{]*[{][[:space:]]*scratchNameFree = true' \
	  && grep -qE '(^|[^A-Za-z0-9_])statFault([^A-Za-z0-9_]|$$)' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: $(SD_MANAGER_SRC)'s uniqueness walk no longer carries"; \
	       echo "       BOTH properties a codex audit established it needs:"; \
	       echo "       (a) a budget of MAX ADVANCES, i.e. MAX+1 candidates"; \
	       echo "           EXAMINED, so it never stops while an unexamined"; \
	       echo "           candidate is still in budget. An earlier guard"; \
	       echo "           stopped one short and TRUNCATED an occupied name"; \
	       echo "           while the next candidate was FREE -- a regression"; \
	       echo "           against the code it replaced."; \
	       echo "       (b) NO_FILE/NO_PATH treated as absence and every other"; \
	       echo "           stat error treated as a FAULT (statFault), because"; \
	       echo "           one transient DISK_ERR read as absent let the open"; \
	       echo "           truncate silently, with the fallback log skipped."; \
	       echo "       test_958 CASES 7-10 model BOTH walks, so they pass"; \
	       echo "       whatever the firmware does: a host model cannot see which"; \
	       echo "       form shipped. THIS PIN ties the model to the source."; \
	       exit 1; }
	@grep -qE 'SD_CARD_MANAGER_UNIQUE_TARGET_MAX[[:space:]]+SD_CARD_MANAGER_MAX_DIR_FILES' $(SD_MANAGER_SRC) \
	  && grep -qE 'SD_CARD_MANAGER_MAX_DIR_FILES[[:space:]]+64u' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: the candidate bound is no longer tied to the bucket"; \
	       echo "       ceiling, or MAX_DIR_FILES is no longer 64u, in"; \
	       echo "       $(SD_MANAGER_SRC). test_958 mirrors it as"; \
	       echo "       FW_UNIQUE_TARGET_MAX and its POPULATION argument -- the"; \
	       echo "       seventeenth file is what makes the defect reproduce -- is"; \
	       echo "       derived from that number. Update the mirror together with"; \
	       echo "       the constant or CASE 7 silently stops testing exhaustion."; \
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
	@if grep -qE 'if[[:space:]]*\([[:space:]]*bucketOk[[:space:]]*&&[[:space:]]*!uniqueTargetAtOpen' $(SD_MANAGER_SRC); then \
	       echo "ERROR: the bucket pre-walk in $(SD_MANAGER_SRC) is gated on"; \
	       echo "       !uniqueTargetAtOpen again. That block carries TWO things:"; \
	       echo "       the reopen-existing search (which a scratch open must NOT"; \
	       echo "       do) and the probeFsError/targetFsError refusals (which it"; \
	       echo "       MUST keep -- they exit through the #689 clean-stop BEFORE"; \
	       echo "       the open). Gating the whole block removes the refusals: a"; \
	       echo "       stat fault in that window then falls through to the"; \
	       echo "       truncating WRITE_PLUS instead of refusing. An adversarial"; \
	       echo "       audit found exactly that in BOTH legs, high/data_loss."; \
	       echo "       Gate the reopen ASSIGNMENT instead, not the walk."; \
	       exit 1; \
	 fi
	@tr -d '\n' < $(SD_MANAGER_SRC) \
	  | grep -qE 'if[[:space:]]*\([[:space:]]*!uniqueTargetAtOpen[[:space:]]*\)[[:space:]]*[{][[:space:]]*reopenExisting = true' \
	  || { echo "ERROR: the reopen-existing assignment in $(SD_MANAGER_SRC) is no"; \
	       echo "       longer guarded by !uniqueTargetAtOpen. A scratch open would"; \
	       echo "       then REOPEN an existing benchmark file, and the truncating"; \
	       echo "       WRITE_PLUS destroys it -- the #958 defect by another route."; \
	       echo "       Keep the pre-walk RUNNING (its fault refusals are load-"; \
	       echo "       bearing) and suppress only the reuse. These two pins are a"; \
	       echo "       PAIR: one refuses gating the walk, the other requires the"; \
	       echo "       assignment be gated. Neither alone states the property."; \
	       exit 1; }
	@w=`grep -nE 'if \(bucketOk && uniqueTargetAtOpen\) \{' $(SD_MANAGER_SRC) | cut -d: -f1`; \
	 c=`grep -nE '^[[:space:]]+if \(!bucketOk\) \{' $(SD_MANAGER_SRC) | cut -d: -f1`; \
	 if [ -z "$$w" ] || [ -z "$$c" ]; then \
	       echo "ERROR: cannot locate the #958 walk and the #689 clean-stop in"; \
	       echo "       $(SD_MANAGER_SRC) -- one of them was renamed or removed."; \
	       exit 1; \
	 fi; \
	 if [ "$$w" -ge "$$c" ]; then \
	       echo "ERROR: the #958 collision walk (line $$w) is no longer ABOVE the"; \
	       echo "       #689 clean-stop (line $$c) in $(SD_MANAGER_SRC)."; \
	       echo "       ORDER IS THE WHOLE MECHANISM. The walk refuses by setting"; \
	       echo "       bucketOk=false and FALLING INTO that block; below it the"; \
	       echo "       assignment is DEAD and the open truncates anyway -- a"; \
	       echo "       silent permissive failure that every existing test still"; \
	       echo "       passes, because the model cannot see statement order."; \
	       echo "       Three earlier attempts BUILT an exit here instead and"; \
	       echo "       each shipped a defect; reaching the existing one is the"; \
	       echo "       entire difference. Keep the walk above the clean-stop."; \
	       exit 1; \
	 fi
	@# The statFault branch must REFUSE. Checked WITHOUT spanning a comment:
	@# an earlier version of this pin used [^;]* between the `if` and the
	@# assignment, and a semicolon in the comment's own PROSE broke it -- a
	@# check whose correctness depended on comment punctuation. These three
	@# are code-only and state the property together.
	@# SITE-SCOPED to the walk. An unscoped version of this grep matched the
	@# PRE-WALK's identical writeRefuseReason/bucketOk pair ~150 lines above
	@# and so passed with the walk's refusal deleted -- it was vacuous, and
	@# only the A/B mutation found it. Extract the walk's own line range
	@# first; the adjacency is then unambiguous.
	@w=`grep -nE 'if \(bucketOk && uniqueTargetAtOpen\) \{' $(SD_MANAGER_SRC) | cut -d: -f1`; \
	 c=`grep -nE '^[[:space:]]+if \(!bucketOk\) \{' $(SD_MANAGER_SRC) | cut -d: -f1`; \
	 if [ -z "$$w" ] || [ -z "$$c" ] || [ "$$w" -ge "$$c" ]; then \
	       echo "ERROR: cannot delimit the #958 walk in $(SD_MANAGER_SRC)"; \
	       echo "       (walk='$$w' clean-stop='$$c'). The ordering pin below"; \
	       echo "       explains why the two must both exist, in that order."; \
	       exit 1; \
	 fi; \
	 sed -n "$$w,$$c p" $(SD_MANAGER_SRC) | tr -d '\n' \
	   | grep -qE 'writeRefuseReason[[:space:]]*=[[:space:]]*SD_REFUSE_BUCKET_UNREADABLE[[:space:]]*;[[:space:]]*bucketOk = false' \
	   || { echo "ERROR: the collision walk's statFault branch in"; \
	        echo "       $(SD_MANAGER_SRC) no longer REFUSES. Inside the walk"; \
	        echo "       itself (lines $$w-$$c) it must set writeRefuseReason"; \
	        echo "       AND bucketOk=false, adjacently, so the existing #689"; \
	        echo "       clean-stop skips the truncating open. A card that"; \
	        echo "       cannot ANSWER leaves the candidate UNKNOWN; both audit"; \
	        echo "       legs filed this high/data_loss."; \
	        exit 1; }
	@grep -qF -- '- refusing, retry or clear card' $(SD_MANAGER_SRC) \
	  || { echo "ERROR: the #958 stat-fault message in $(SD_MANAGER_SRC) no"; \
	       echo "       longer says it REFUSES. The message and the behaviour"; \
	       echo "       are checked separately on purpose: a comment or a log"; \
	       echo "       line claiming a guarantee the code does not have is the"; \
	       echo "       defect this PR shipped once already."; \
	       exit 1; }
	@if grep -qF -- '- opening it, may truncate' $(SD_MANAGER_SRC); then \
	       echo "ERROR: the pre-refusal #958 stat-fault message is back in"; \
	       echo "       $(SD_MANAGER_SRC). That wording belongs to the behaviour"; \
	       echo "       where an unverifiable candidate reached the truncating"; \
	       echo "       open. If it is back, so is the data_loss both audit legs"; \
	       echo "       filed -- fix the firmware, do not relax this guard."; \
	       exit 1; \
	 fi
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

$(LOG1039_GEN): gen_1039_log_budget.py $(LOG_BUDGET_GATE) $(FW_APP_SRC) \
                $(FW_ADC_SCPI) $(FW_WIFI_SRC) $(FW_LOG_H) $(FW_LOG_C) \
                $(RECIPES)
	# The generator's own invariants run FIRST, so a defect in the
	# extractor fails the build rather than producing a fixture that
	# measures a different string than the firmware ships (codex audit on
	# #1110: UTF-8 shrank the literal; a prefix match mis-read the ceiling;
	# backslash-newline was spliced after escapes instead of before).
	$(PYTHON) gen_1039_log_budget.py --self-test
	$(PYTHON) gen_1039_log_budget.py --out $(LOG1039_GEN)

$(LOG1039_BIN): test_1039_log_message_budget.c test_framework.h \
                $(LOG1039_GEN) $(RECIPES)
	$(CC) $(CFLAGS) -o $(LOG1039_BIN) test_1039_log_message_budget.c

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
# #1125: gLogOneShot's bit claim (LOG_x_ONCE, ISR-callable) must be an atomic
# test-and-set, not a bare `|=` -- a preemption between an unguarded |='s load
# and store lets another context (task, or a higher-priority ISR) win its own
# bit, and the resumed store writes back the stale snapshot and ERASES it,
# re-arming that call site for a second fire and defeating the "at most once"
# contract the ISR flood-guard use depends on. Logger.c is not includable on
# the host as a whole (xc.h, SYSKEY, PIC32 __builtin_* intrinsics, Harmony's
# clock_config.h -- same reason $(SDLOG_BIN) above extracts rather than
# includes), so this extracts Logger_OneShotClaim's body VERBATIM -- the real
# function, not a re-implementation -- and compiles it against mocked
# taskENTER_CRITICAL[_FROM_ISR]/taskEXIT_CRITICAL[_FROM_ISR] in
# test_1125_log_once_claim.c.
#
# A single-threaded host test cannot literally interleave two contexts inside
# one function call, so what the guard below proves is STRUCTURAL rather than
# behavioural, mirroring tools/lint/scpi_claim_path.py's own "exactly one
# opener, one closer, test+set positioned between them" reasoning for this
# identical class of property (a claim granted outside its critical section):
# exactly one task-context and one ISR-context critical-section pair, and
# both of the function's test-and-set pairs positioned strictly BETWEEN their
# matching enter/exit -- refused, not silently passed, on any other count or a
# torn positional order. That is what rules the race out: a real critical
# section makes the interleaving unrepresentable, which is a claim about
# POSITION in the source, not something a host thread can exercise.
LOGONCE_BIN := run_1125_tests
GEN_1125    := gen_1125_log_once_claim.h

$(LOGONCE_BIN): test_1125_log_once_claim.c test_framework.h $(FW_LOG_C) $(RECIPES)
	@body=`sed -n '/^bool Logger_OneShotClaim(uint32_t bit) {/,/^}/p' $(FW_LOG_C) | tr -d '\r'`; \
	 test -n "$$body" \
	   || { echo "ERROR: Logger_OneShotClaim(uint32_t bit) was not found in $(FW_LOG_C),"; \
	        echo "       or no longer opens at column 0 with that exact signature -- the"; \
	        echo "       extraction below can see nothing, and a guard that examines"; \
	        echo "       nothing must fail rather than pass vacuously."; \
	        exit 1; }; \
	 n_enter_task=`printf '%s\n' "$$body" | grep -cF 'taskENTER_CRITICAL();'`; \
	 n_exit_task=`printf '%s\n' "$$body"  | grep -cF 'taskEXIT_CRITICAL();'`; \
	 n_enter_isr=`printf '%s\n' "$$body"  | grep -cF 'taskENTER_CRITICAL_FROM_ISR();'`; \
	 n_exit_isr=`printf '%s\n' "$$body"   | grep -cF 'taskEXIT_CRITICAL_FROM_ISR(saved);'`; \
	 n_test=`printf '%s\n' "$$body" | grep -cF 'if ((gLogOneShot & mask) == 0u) {'`; \
	 n_set=`printf '%s\n' "$$body"  | grep -cF 'gLogOneShot |= mask;'`; \
	 if [ "$$n_enter_task" != "1" ] || [ "$$n_exit_task" != "1" ] || \
	    [ "$$n_enter_isr" != "1" ] || [ "$$n_exit_isr" != "1" ] || \
	    [ "$$n_test" != "2" ] || [ "$$n_set" != "2" ]; then \
	   echo "ERROR: Logger_OneShotClaim's critical-section shape has changed"; \
	   echo "       (enter_task=$$n_enter_task exit_task=$$n_exit_task"; \
	   echo "        enter_isr=$$n_enter_isr exit_isr=$$n_exit_isr"; \
	   echo "        test=$$n_test set=$$n_set -- each wants 1/1/1/1/2/2)."; \
	   echo "       test_1125_log_once_claim.c's mocks and the positional check"; \
	   echo "       below assume exactly one task-context and one ISR-context"; \
	   echo "       critical-section pair, each guarding exactly one test-and-set."; \
	   exit 1; \
	 fi; \
	 enter_isr_ln=`printf '%s\n' "$$body" | grep -nF 'taskENTER_CRITICAL_FROM_ISR();' | cut -d: -f1`; \
	 exit_isr_ln=`printf '%s\n' "$$body"  | grep -nF 'taskEXIT_CRITICAL_FROM_ISR(saved);' | cut -d: -f1`; \
	 enter_task_ln=`printf '%s\n' "$$body" | grep -nF 'taskENTER_CRITICAL();' | cut -d: -f1`; \
	 exit_task_ln=`printf '%s\n' "$$body"  | grep -nF 'taskEXIT_CRITICAL();' | cut -d: -f1`; \
	 test_lns=`printf '%s\n' "$$body" | grep -nF 'if ((gLogOneShot & mask) == 0u) {' | cut -d: -f1`; \
	 set_lns=`printf '%s\n' "$$body"  | grep -nF 'gLogOneShot |= mask;' | cut -d: -f1`; \
	 isr_test_ln=`printf '%s\n' "$$test_lns" | awk -v lo="$$enter_isr_ln" -v hi="$$exit_isr_ln" '$$1>lo && $$1<hi'`; \
	 isr_set_ln=`printf '%s\n' "$$set_lns"  | awk -v lo="$$enter_isr_ln" -v hi="$$exit_isr_ln" '$$1>lo && $$1<hi'`; \
	 task_test_ln=`printf '%s\n' "$$test_lns" | awk -v lo="$$enter_task_ln" -v hi="$$exit_task_ln" '$$1>lo && $$1<hi'`; \
	 task_set_ln=`printf '%s\n' "$$set_lns"  | awk -v lo="$$enter_task_ln" -v hi="$$exit_task_ln" '$$1>lo && $$1<hi'`; \
	 if [ -z "$$isr_test_ln" ] || [ -z "$$isr_set_ln" ] || \
	    [ -z "$$task_test_ln" ] || [ -z "$$task_set_ln" ]; then \
	   echo "ERROR: Logger_OneShotClaim's test-and-set is no longer POSITIONED"; \
	   echo "       inside its matching critical-section pair (ISR test@[$$isr_test_ln]"; \
	   echo "       set@[$$isr_set_ln] between $$enter_isr_ln/$$exit_isr_ln; task"; \
	   echo "       test@[$$task_test_ln] set@[$$task_set_ln] between"; \
	   echo "       $$enter_task_ln/$$exit_task_ln) -- a claim outside its critical"; \
	   echo "       section is exactly the race this function exists to close."; \
	   exit 1; \
	 fi; \
	 printf '/* GENERATED at build time by tests/host/Makefile ($(LOGONCE_BIN)).\n' > $(GEN_1125); \
	 printf ' * Do not edit, do not commit -- see tests/host/.gitignore.\n' >> $(GEN_1125); \
	 printf ' * Verbatim body of Logger_OneShotClaim() extracted from $(FW_LOG_C). */\n' >> $(GEN_1125); \
	 printf '#ifndef GEN_1125_LOG_ONCE_CLAIM_H\n#define GEN_1125_LOG_ONCE_CLAIM_H\n' >> $(GEN_1125); \
	 printf '%s\n' "$$body" >> $(GEN_1125); \
	 printf '#endif\n' >> $(GEN_1125)
	$(CC) $(CFLAGS) -o $(LOGONCE_BIN) test_1125_log_once_claim.c

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

# Every suite, and every one of the TWENTY-FOUR binaries. Another additive collision
# in this long-running block -- fw#958's $(NAME_BIN) against everything main
# accumulated while this branch sat parked. Resolved the same way as every one
# before it: each side's target rule kept INTACT and only `run` and `clean`
# unioned. Dropping a rule loses a suite silently; duplicating one silently
# shadows the other.
#
# THE BINARY COUNT IS DERIVED, NOT ASSERTED -- counted from the `run` recipe
# list below, which this merge did not conflict and which therefore already
# carried the union. NINE separate "every one of the N binaries" claims live in
# this file with nine different N, and all but one are stale.
#
# AND THE ORDINAL IS DELIBERATELY OMITTED, which departs from the eight
# comments above that each assert one. It CANNOT be computed correctly on a
# branch: every branch derives "next" from main, so two branches refreshed in
# parallel both claim the same ordinal and whichever merges second is wrong.
# That is happening right now -- fw#1110's refresh claims FIFTEENTH and main's
# highest is FOURTEENTH, so this branch would have had to claim FIFTEENTH too.
# The ordinal was never the load-bearing part; the convention it introduces is,
# and that is stated above without a number that cannot be kept true.
#
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
# Every suite, and every one of the FIFTEEN binaries below. A new suite
# always touches this same `run:`/`clean:` pair, so any two branches that add
# a suite around the same time collide here. History, each resolved the same
# way (keep each side's target rule INTACT, union only `run` and `clean` --
# dropping a rule loses a suite silently, duplicating one silently shadows
# the other): fw#961 JSONESC_BIN vs fw#983 SUSPEND_BIN; fw#1012 vs fw#958;
# fw#913 vs fw#1004 HELP_BIN; fw#958 vs fw#1000 SDLOG_BIN; fw#980 DAC980_BIN
# landing on main alongside fw#1004 HELP_BIN and fw#1000 SDLOG_BIN; fw#958's
# NAME_BIN vs main's DAC980_BIN; fw#913 vs fw#1004/fw#1000/fw#980/fw#985
# (SPI913_BIN, SUSPENDREASON_BIN) landing together on main; fw#958's NAME_BIN
# with SPI913_BIN, SUSPENDREASON_BIN (#985) and DAC1057_BIN (#1057); fw#999's
# SCPI999_BIN vs fw#985 and vs fw#1057 on main; fw#958's NAME_BIN vs
# fw#999's SCPI999_BIN; and now fw#958's NAME_BIN vs fw#947's SYSINFO_BIN
# landing on main. Each branch numbered these collisions on its own, so
# two of them both claimed "NINTH"; the list is the record, not an ordinal.

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
	@# ⛔⛔ #969 / PR #1020: BOTH #861 STOP PINS UNDER **ONE** CRITICAL SECTION.
	@# This is the companion the operator ruled for this row (item M = A, 2026-10-27:
	@# "A for 1020 seems right") -- a SOURCE-LEVEL host check, because a black-box
	@# SCPI test cannot reach a preemption between two adjacent instructions and
	@# would pass on both the old and the new firmware.
	@#
	@# WHAT IS BEING PROTECTED. SCPI_StartStreaming pins the stop generation and the
	@# stops-active count, and the arm later re-reads them to decide whether a stop
	@# was in flight. Each load is individually atomic (aligned 32-bit on PIC32MZ),
	@# so the section is NOT there to make a load atomic -- it is there to make the
	@# PAIR describe ONE INSTANT. As two separate loads, a stop that bumped the
	@# generation before the first load and finished between the two made BOTH
	@# signals miss it, and the arm published IsEnabled over a running stop.
	@#
	@# ⭐ WHY THIS IS ONE SEQUENCE CHECK AND NOT THREE GREPS. The property is an
	@# ORDER, so it is pinned as an order: the four markers must appear as the
	@# CONSECUTIVE token run `ENTER GEN ACTIVE EXIT`. That single assertion refuses
	@# all three ways the fix can be lost, which three presence greps would not:
	@#   - REVERTED to bare loads   -> no ENTER/EXIT at all, run absent.
	@#   - ONE READ MOVED OUT       -> `GEN ENTER ACTIVE EXIT`, not consecutive.
	@#   - SPLIT INTO TWO SECTIONS  -> `ENTER GEN EXIT ENTER ACTIVE EXIT`. Both
	@#     reads are still inside a critical section, so a per-read "is it guarded"
	@#     check PASSES while the pair no longer describes one instant. This is the
	@#     mutant a naive guard misses, and it is the whole point of the ordering.
	@#
	@# Comment-STRIPPED via $(CC) -fpreprocessed -E (which strips comments without
	@# expanding includes), because the paragraphs above this fix in the source name
	@# every one of these four tokens -- a raw grep would be satisfied by the prose
	@# that explains the fix. MEASURED on the fixed source: 0 comment lines survive
	@# inside the extracted body. Token-based rather than line-anchored, so it is
	@# indifferent to CRLF. ⛔ r3 CORRECTION: an earlier revision of this guard
	@# used awk, and awk evaluates its rules ONCE PER LINE in RULE order -- so it
	@# collapsed within-line order and dropped duplicates. A ONE-LINE split
	@# emitted 'ENTER GEN ACTIVE EXIT' and PASSED: the exact torn snapshot this
	@# guard exists to block. MEASURED both ways on that line. `grep -o` emits
	@# every match left-to-right with repeats kept, so the stream is true source
	@# order, and the comparison is EXACT equality rather than a substring --
	@# which also refuses a stray ENTER/EXIT pair anywhere in the body.
	@# ⭐ THE LESSON, because it is general: a mutant set whose LAYOUT matches the
	@# matcher's UNIT cannot test the matcher. Every original mutant here was
	@# multi-line, so none of them probed per-line granularity. The one-line
	@# arms added to pin969_mutate.sh exist for exactly that.
	@$(CC) -fpreprocessed -E $(START_CLAIMED_SRC) 2>/dev/null \
	   | awk '/static scpi_result_t SCPI_StartStreaming\(scpi_t \* context\)/{f=1} \
	          f{print} f&&/^\}/{exit}' > /tmp/.sd969_fn.$$$$; \
	 test -s /tmp/.sd969_fn.$$$$ \
	  || { echo "ERROR: could not extract SCPI_StartStreaming's body from"; \
	       echo "       $(START_CLAIMED_SRC) (#969). The awk anchor is its"; \
	       echo "       signature; if that changed this check measures NOTHING,"; \
	       echo "       which is the one way an ordering pin passes while blind."; \
	       rm -f /tmp/.sd969_fn.$$$$; exit 1; }; \
	 stream=`grep -oE 'taskENTER_CRITICAL|taskEXIT_CRITICAL|gStreamStopGen|gStreamStopsActive' \
	           /tmp/.sd969_fn.$$$$ \
	         | sed -e 's/^taskENTER_CRITICAL$$/ENTER/' -e 's/^taskEXIT_CRITICAL$$/EXIT/' \
	               -e 's/^gStreamStopGen$$/GEN/'       -e 's/^gStreamStopsActive$$/ACTIVE/' \
	         | tr '\n' ' ' | sed 's/ *$$//'`; \
	 rm -f /tmp/.sd969_fn.$$$$; \
	 test "$$stream" = "ENTER GEN ACTIVE EXIT" \
	  || { echo "ERROR: #861's two stop pins are no longer taken under ONE critical"; \
	       echo "       section in SCPI_StartStreaming (#969, PR #1020)."; \
	       echo "         marker stream: $$stream"; \
	       echo "         required     : ENTER GEN ACTIVE EXIT  (exactly)"; \
	       echo "       Each load is atomic by itself; the section exists so the"; \
	       echo "       PAIR describes one instant. Without it a stop that bumps"; \
	       echo "       the generation before the first load and completes between"; \
	       echo "       the two is invisible to BOTH signals, and the arm publishes"; \
	       echo "       IsEnabled over a stop that was still running -- silently,"; \
	       echo "       which is the shape #861 exists to eliminate."; \
	       echo "       Two sections are NOT one: a stream reading"; \
	       echo "       'ENTER GEN EXIT ENTER ACTIVE EXIT' has each read guarded"; \
	       echo "       and the pair still torn."; \
	       echo "       The match is EXACT, so an extra critical section anywhere"; \
	       echo "       in this function also fails. If one is added on purpose,"; \
	       echo "       re-scope this stream to the run containing GEN rather than"; \
	       echo "       widening the expected string."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(START_BIN) test_998_start_streaming_claimed_order.c

$(SDLOG1029_GEN): gen_1029_log_budget.py $(SD_MGR_SRC) $(SD_MGR_HDR) $(FW_UTIL)/Logger.c $(FW_UTIL)/Logger.h $(RECIPES)
	$(PYTHON) gen_1029_log_budget.py --out $(SDLOG1029_GEN)

$(SDLOG1029_BIN): test_1029_sd_card_manager_log_budget.c test_framework.h $(SDLOG1029_GEN) $(RECIPES)
	$(CC) $(CFLAGS) -o $(SDLOG1029_BIN) test_1029_sd_card_manager_log_budget.c

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

# Every suite, and every one of the binaries named in `run` below. Additive
# collisions in this block as a LINEAGE rather than an ordinal -- main replaced the
# ordinal deliberately, because each branch numbered its own collisions, the ordinals
# disagreed (#971 said EIGHTH, main said NINTH), and three stale copies of this
# banner with three different counts had accumulated:
#   fw#961 vs fw#983; fw#913 vs fw#1004/fw#1000/fw#980/fw#985; fw#999 vs
#   fw#985/fw#1057; fw#971's ARM_BIN vs each of those as it landed; fw#971 vs
#   fw#947 (SYSINFO_BIN); fw#1039 vs fw#1108 (the first where one side APPENDS while
#   the other RETIRES from the same line); and now fw#1039 vs fw#958 (fw#1027,
#   merged 4ecb59e64), which brought $(ARM_BIN) and $(NAME_BIN).
# Resolved the same way every time: keep each side's target rule INTACT and union
# only `run` and `clean`. Dropping a rule loses a suite silently; duplicating one
# silently shadows the other.
# PR #1152 (the #1148 follow-up): CONF:ADC:USECal must not hand the STORED
# settings type to daqifi_settings_SaveToNvm.
#
# DaqifiSettings is { checksum[16], type, settings } and both integrity
# checks cover `.settings` only, so `type` is whatever bytes are physically
# at the flash address. #1148 reordered ADCUseCalSetClaimed() to install the
# new calibration coefficients BEFORE persisting the selector; a corrupted
# stored type then makes the persist fail after the install, which is the
# silent-wrong-data shape test_1152_usecal_type_trust.c reproduces.
#
# Same technique and same reason as $(SCAN1112_BIN) and $(SUSPENDREASON_BIN):
# neither SCPIADC.c nor daqifi_settings.c is host-includable (Harmony's
# configuration.h/definitions.h, the WINC driver headers, the board graph),
# so the load/install/save shape is re-implemented against an in-memory NVM
# and the pre-/post-fix shapes compared on identical fixtures.
#
# The CHECKSUM is not re-implemented. $(FW_UTIL)/CRC32.c includes only its
# own header (stdint.h + stddef.h), so this target compiles and links the
# REAL one -- which is what makes "the record validates and the type is still
# wrong" a demonstration rather than an assumption. It needs no stub on the
# include path at all, so this is -I$(FW_UTIL) and not $(INCLUDES).
#
# Three guard blocks, because the model rests on three claims it cannot check
# for itself:
#
#   A. THE FIX IS STILL THERE, AND STILL IN ORDER. One normalization of
#      tmpTopLevelSettings.type, sitting between the LoadFromNvm that produced
#      the struct and the SaveToNvm that consumes it. The ordering is checked
#      by line number rather than by presence alone: a normalization that
#      drifted BELOW the install would still satisfy a bare grep while
#      reopening the window the test models, and one below the save would do
#      nothing at all.
#   B. THE PREMISE IS STILL TRUE. Both of daqifi_settings.c's validators
#      (CRC32 and the legacy MD5 fallback) still span `.settings` rather than
#      the whole struct; LoadFromNvm still copies the WHOLE record out, which
#      is what carries `type` to the caller; and SaveToNvm still dispatches on
#      settings->type with a `default: return false` that precedes any flash
#      I/O. If any of those changed, this file's entire premise would be gone
#      and the build must say so rather than pass a stale model green.
#   C. THE ENUM IS STILL WHAT THE MODEL ASSUMES. The test hardcodes 0..3 for
#      the four types and sweeps "out of enum" against that bound, so the
#      guard pins the four enumerators, their order, and that none of them
#      carries an explicit value.
#
# Two more guards were added later, after this recipe's own coverage proved
# incomplete, and are not part of the "three claims" above -- they pin
# ADCUseCalSetClaimed()'s round 9+ recovery shape instead of the original
# type-trust premise: round 9 added a per-message LOG_E byte-budget check
# (below, near the end of this recipe), and round 10 added a check that
# daqifi_settings_TopLevelMatchesCommitted's own body still compares calVals
# alone (also below) -- the model and the ordering guard above only ever
# pinned that function's CALL SITE by presence and position, never its body.
#
# Comment lines are stripped before any search in A and B, because the
# firmware's own comments at both sites name these identifiers on purpose --
# #1152's rationale block mentions daqifi_settings_SaveToNvm and
# tmpTopLevelSettings.type in prose directly above the code that does them. A
# comment restyled so its lines do not begin with `*` or `/` therefore fails
# these guards, loudly, which is the right outcome for a guard whose job is to
# notice edits to these functions. CRs are stripped too: every firmware source
# in this repo is CRLF and this Makefile is not.
USECALTYPE_BIN      := run_1152_tests
DAQIFI_SETTINGS_SRC := $(FW_SVC)/daqifi_settings.c
DAQIFI_SETTINGS_H   := $(FW_SVC)/daqifi_settings.h
CRC32_SRC           := $(FW_UTIL)/CRC32.c
CRC32_H             := $(FW_UTIL)/CRC32.h

$(USECALTYPE_BIN): test_1152_usecal_type_trust.c test_framework.h \
                   $(SCPIADC_SRC) $(DAQIFI_SETTINGS_SRC) $(DAQIFI_SETTINGS_H) \
                   $(CRC32_SRC) $(CRC32_H) $(RECIPES)
	@body=`sed -n '/^static scpi_result_t ADCUseCalSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$body" \
	   || { echo "ERROR: ADCUseCalSetClaimed() was not found in $(SCPIADC_SRC),"; \
	        echo "       or no longer opens at column 0 -- the guards below can see"; \
	        echo "       nothing, and a guard that examines nothing must fail."; \
	        exit 1; }; \
	 nnorm=`printf '%s\n' "$$body" | grep -o 'tmpTopLevelSettings\.type = DaqifiSettings_TopLevelSettings;' | wc -l | tr -d ' '`; \
	 test "$$nnorm" -eq 1 \
	   || { echo "ERROR: ADCUseCalSetClaimed() in $(SCPIADC_SRC) no longer normalizes"; \
	        echo "       the loaded settings type exactly once (found $$nnorm, want 1)."; \
	        echo "       That one line IS #1152: without it the struct handed to"; \
	        echo "       daqifi_settings_SaveToNvm carries whatever type byte NVM held,"; \
	        echo "       which the payload checksum does not cover -- and the save then"; \
	        echo "       fails, or writes another bank, AFTER the new coefficients are"; \
	        echo "       already live. test_1152_usecal_type_trust.c models the"; \
	        echo "       normalized shape, so it would pass regardless."; \
	        exit 1; }; \
	 l=`printf '%s\n' "$$body" | grep -n 'daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings,' | cut -d: -f1`; \
	 n=`printf '%s\n' "$$body" | grep -n 'tmpTopLevelSettings\.type = DaqifiSettings_TopLevelSettings;' | cut -d: -f1`; \
	 v=`printf '%s\n' "$$body" | grep -n 'daqifi_settings_ValidateADCCalBank(calType)' | cut -d: -f1`; \
	 ni=`printf '%s\n' "$$body" | grep -c 'daqifi_settings_LoadADCCalSettings(calType, pRuntimeAInChannels)'`; \
	 i=`printf '%s\n' "$$body" | grep -n 'daqifi_settings_LoadADCCalSettings(calType, pRuntimeAInChannels)' | cut -d: -f1`; \
	 s=`printf '%s\n' "$$body" | grep -n 'daqifi_settings_SaveToNvm(&tmpTopLevelSettings)' | cut -d: -f1`; \
	 r=`printf '%s\n' "$$body" | grep -n 'daqifi_settings_TopLevelMatchesCommitted(&tmpTopLevelSettings)' | cut -d: -f1`; \
	 cr=`printf '%s\n' "$$body" | grep -n 'bool committedReadable = daqifi_settings_LoadFromNvm(' | cut -d: -f1`; \
	 cb=`printf '%s\n' "$$body" | grep -n 'LOG_E("CONF:ADC:USECal %d: continuing - committed rec matches "' | cut -d: -f1`; \
	 nrc=`printf '%s\n' "$$body" | grep -c 'pRunTimeStreamConfig->RawOutputMode = false;'`; \
	 rc=`printf '%s\n' "$$body" | grep -n 'pRunTimeStreamConfig->RawOutputMode = false;' | head -1 | cut -d: -f1`; \
	 test -n "$$l" && test -n "$$n" && test -n "$$v" && test -n "$$r" \
	   && test -n "$$cr" && test -n "$$cb" && test -n "$$i" && test -n "$$s" && test -n "$$rc" \
	   && test "$$ni" -eq 1 && test "$$nrc" -eq 1 \
	   && test "$$rc" -lt "$$l" \
	   && test "$$l" -lt "$$n" && test "$$n" -lt "$$v" \
	   && test "$$v" -lt "$$s" && test "$$s" -lt "$$r" \
	   && test "$$r" -lt "$$cr" && test "$$cr" -lt "$$cb" && test "$$cb" -lt "$$i" \
	   || { echo "ERROR: ADCUseCalSetClaimed() in $(SCPIADC_SRC) no longer runs"; \
	        echo "       RAW-MODE-CLEAR -> load -> normalize -> validate -> save ->"; \
	        echo "       VERIFY-COMMITTED -> re-read-committed -> CONTINUING-fallthrough ->"; \
	        echo "       final-install, in that order"; \
	        echo "       (rawClear='$$rc' nRawClear=$$nrc load='$$l' normalize='$$n'"; \
	        echo "        validate='$$v' save='$$s' verify='$$r' reread='$$cr'"; \
	        echo "        continuing='$$cb' finalInstall='$$i' nInstall=$$ni)."; \
	        echo "       Each adjacency carries a defect if it inverts:"; \
	        echo "         the raw-mode clear anywhere but FIRST (before the load) is"; \
	        echo "           #1152 round 8's regression -- ruling 9=A requires USECal"; \
	        echo "           0/1 to leave raw mode UNCONDITIONALLY, independent of"; \
	        echo "           whether the bank load below succeeds; a clear that drifted"; \
	        echo "           back to the end of the function (the pre-round-8 shape) or"; \
	        echo "           that got duplicated (nRawClear != 1) reopens the hardware"; \
	        echo "           repro: USECal 2 -> USECal 0/1 against a blank bank -> -200,"; \
	        echo "           still raw, forever, on a board with no populated cal bank;"; \
	        echo "         normalize below the save does nothing -- SaveToNvm would see"; \
	        echo "           the type NVM held, not the one we asked for;"; \
	        echo "         install ABOVE the save is the audited #1152 r1 defect -- the"; \
	        echo "           coefficients go live and then the persist fails, leaving"; \
	        echo "           USECal? reporting a selector that disagrees with what"; \
	        echo "           acquisition is using, with NO indication (a write-protected"; \
	        echo "           page leaves the old record VALID, and any later"; \
	        echo "           CONF:VOLT:SAVE erases the invalid-record indication);"; \
	        echo "         a MISSING or misplaced verify-committed step re-opens the"; \
	        echo "           r2 finding: SaveToNvm reports failure over a write that"; \
	        echo "           LANDED (its success test is a readback comparison against"; \
	        echo "           the shared staging buffer), so giving up there leaves the"; \
	        echo "           NEW selector stored and the OLD coefficients live;"; \
	        echo "         validate below the save re-opens #1148 -- a selector would"; \
	        echo "           persist for a bank that cannot be loaded;"; \
	        echo "         a MISSING re-read of the committed record (reread not strictly"; \
	        echo "           between verify and continuing), or a second occurrence of"; \
	        echo "           the install call (nInstall != 1), reopens round 8's own"; \
	        echo "           regression: round 8 installed the validated bank on EVERY"; \
	        echo "           TopLevelMatchesCommitted mismatch, on the false premise that"; \
	        echo "           the erase already ran either way -- it had not, on a"; \
	        echo "           write-protected page the OLD valid record survives, and"; \
	        echo "           round 8's install then overwrote live coefficients that"; \
	        echo "           already agreed with that surviving OLD selector. Round 9"; \
	        echo "           reads the committed record again and installs ONLY when it"; \
	        echo "           is reached via 'continuing' (the fallthrough for a record"; \
	        echo "           that DOES carry the intended selector) -- an install line"; \
	        echo "           that drifted back inside the mismatch branch (before"; \
	        echo "           'continuing') satisfies nInstall==1 but fails the"; \
	        echo "           cb < finalInstall ordering below, which is why that ordering"; \
	        echo "           is checked and not just presence;"; \
	        echo "         a missing or misordered final-install (final install not"; \
	        echo "           strictly after verify, re-read and continuing) reopens"; \
	        echo "           round 7's finding on the TopLevelMatchesCommitted mismatch"; \
	        echo "           branch -- returning without installing on a MATCHING"; \
	        echo "           (landed) record leaves the PREVIOUS bank live while the"; \
	        echo "           NEW selector is already committed, so USECal? reports the"; \
	        echo "           new selection while acquisition still applies the old slope."; \
	        echo "       All of these still satisfy the presence check above, which is"; \
	        echo "       why this one is positional."; \
	        echo "       If the body was legitimately restructured, re-read it against"; \
	        echo "       the model in test_1152_usecal_type_trust.c."; \
	        exit 1; }
	@grep -qF 'uint32_t crc = CRC32_Compute(&(tmpSettings.settings), dataSize);' $(DAQIFI_SETTINGS_SRC) \
	  || { echo "ERROR: daqifi_settings_LoadFromNvm's CRC32 in $(DAQIFI_SETTINGS_SRC)"; \
	       echo "       no longer spans '.settings' exactly. If the checksum now covers"; \
	       echo "       the WHOLE DaqifiSettings struct, a corrupted 'type' can no"; \
	       echo "       longer reach the caller and test_1152_usecal_type_trust.c's"; \
	       echo "       premise is gone -- retire or rewrite it rather than relaxing"; \
	       echo "       this guard."; \
	       exit 1; }
	@grep -qF 'uint32_t crc = CRC32_Compute(&(settings->settings), dataSize);' $(DAQIFI_SETTINGS_SRC) \
	  || { echo "ERROR: daqifi_settings_SaveToNvm's CRC32 in $(DAQIFI_SETTINGS_SRC)"; \
	       echo "       no longer spans '.settings' exactly -- see the message above."; \
	       exit 1; }
	@grep -qF 'CRYPT_MD5_DataAdd(&md5Ctx, (const uint8_t*) &(tmpSettings.settings), dataSize);' $(DAQIFI_SETTINGS_SRC) \
	  || { echo "ERROR: the legacy MD5 fallback in $(DAQIFI_SETTINGS_SRC) no longer"; \
	       echo "       spans '.settings' exactly. It is the SECOND validator on the"; \
	       echo "       load path, so it has to exclude 'type' for the same reason the"; \
	       echo "       CRC32 does; test_1152_usecal_type_trust.c models one span for"; \
	       echo "       both (its FIDELITY 2) and that would no longer be true."; \
	       exit 1; }
	@grep -qF 'memcpy(settings, &tmpSettings, sizeof (DaqifiSettings));' $(DAQIFI_SETTINGS_SRC) \
	  || { echo "ERROR: daqifi_settings_LoadFromNvm in $(DAQIFI_SETTINGS_SRC) no longer"; \
	       echo "       copies the WHOLE record out on a valid checksum. That copy is"; \
	       echo "       what carries the unchecked 'type' field to the caller -- it is"; \
	       echo "       the defect's carrier, and the model assumes it."; \
	       exit 1; }
	@sbody=`sed -n '/^bool daqifi_settings_SaveToNvm(DaqifiSettings\* settings) {/,/^}/p' $(DAQIFI_SETTINGS_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$sbody" \
	   || { echo "ERROR: daqifi_settings_SaveToNvm() was not found in"; \
	        echo "       $(DAQIFI_SETTINGS_SRC), or no longer opens at column 0 --"; \
	        echo "       the guard below can see nothing, so it must fail."; \
	        exit 1; }; \
	 printf '%s\n' "$$sbody" | grep -qF 'switch (settings->type) {' \
	   || { echo "ERROR: daqifi_settings_SaveToNvm() in $(DAQIFI_SETTINGS_SRC) no"; \
	        echo "       longer dispatches on settings->type. That dispatch is what"; \
	        echo "       makes an unchecked type field reach flash addressing at all,"; \
	        echo "       and the whole model is built on it."; \
	        exit 1; }; \
	 d=`printf '%s\n' "$$sbody" | grep -n '^[[:space:]]*default:' | head -1 | cut -d: -f1`; \
	 c=`printf '%s\n' "$$sbody" | grep -n 'daqifi_settings_ClearNvm(settings->type)' | cut -d: -f1`; \
	 nxt=`printf '%s\n' "$$sbody" | sed -n "$$((d+1))p" | tr -d '[:space:]'`; \
	 test -n "$$d" && test -n "$$c" && test "$$d" -lt "$$c" \
	   && test "$$nxt" = "returnfalse;" \
	   || { echo "ERROR: daqifi_settings_SaveToNvm() in $(DAQIFI_SETTINGS_SRC) no"; \
	        echo "       longer rejects an unknown type with 'return false' BEFORE the"; \
	        echo "       first daqifi_settings_ClearNvm() call"; \
	        echo "       (default:='$$d' next-line='$$nxt' ClearNvm='$$c')."; \
	        echo "       test_1152_usecal_type_trust.c asserts that this failure erases"; \
	        echo "       and writes NOTHING -- that is what distinguishes it from the"; \
	        echo "       flash-I/O residual #1148 documents and accepts. If the"; \
	        echo "       rejection moved below the erase, that distinction is gone and"; \
	        echo "       the model, not this guard, is what needs fixing."; \
	        exit 1; }
	@ebody=`sed -n '/typedef enum eDaqifiSettingsType {/,/} DaqifiSettingsType;/p' $(DAQIFI_SETTINGS_H) | tr -d '\r'`; \
	 test -n "$$ebody" \
	   || { echo "ERROR: the eDaqifiSettingsType enum was not found in"; \
	        echo "       $(DAQIFI_SETTINGS_H) -- the guard below can see nothing."; \
	        exit 1; }; \
	 got=`printf '%s\n' "$$ebody" | grep -o 'DaqifiSettings_[A-Za-z]*' | tr '\n' ','`; \
	 want='DaqifiSettings_TopLevelSettings,DaqifiSettings_Wifi,DaqifiSettings_FactAInCalParams,DaqifiSettings_UserAInCalParams,'; \
	 neq=`printf '%s\n' "$$ebody" | grep -c '=' || true`; \
	 test "$$got" = "$$want" && test "$$neq" -eq 0 \
	   || { echo "ERROR: eDaqifiSettingsType in $(DAQIFI_SETTINGS_H) is no longer the"; \
	        echo "       four implicitly-numbered enumerators the model assumes."; \
	        echo "         want $$want (and no explicit values)"; \
	        echo "         got  $$got ($$neq lines carrying '=')"; \
	        echo "       test_1152_usecal_type_trust.c hardcodes 0..3 for these and"; \
	        echo "       sweeps 'out of enum' against that bound, and maps each type"; \
	        echo "       onto its NVM page by that same value -- adding, renumbering"; \
	        echo "       or reordering a type invalidates all three."; \
	        exit 1; }
# #1152 round 10 (finding a): the host model only exercises a REIMPLEMENTATION
# of daqifi_settings_TopLevelMatchesCommitted (model_TopLevelMatchesCommitted,
# test_1152_usecal_type_trust.c), and the ordering guard above only pins that
# function's CALL SITE in $(SCPIADC_SRC) by presence and position -- neither
# touches the real function's OWN body in $(DAQIFI_SETTINGS_SRC). Pin that body
# directly: it must keep comparing calVals alone, never the whole payload (the
# r5 defect -- a whole-payload compare makes every OTHER legitimate TopLevel
# writer, e.g. CONF:VOLT:SAVE / SYST:DEVICE:NAME:SAVE, a false negative; see the
# function's own doc comment in $(DAQIFI_SETTINGS_H)).
	@tlbody=`sed -n '/^bool daqifi_settings_TopLevelMatchesCommitted(const DaqifiSettings\* expected)/,/^}/p' $(DAQIFI_SETTINGS_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$tlbody" \
	   || { echo "ERROR: daqifi_settings_TopLevelMatchesCommitted() was not found in"; \
	        echo "       $(DAQIFI_SETTINGS_SRC), or no longer opens at column 0 --"; \
	        echo "       the guard below can see nothing, so it must fail."; \
	        exit 1; }; \
	 printf '%s\n' "$$tlbody" | grep -qF 'committed.settings.topLevelSettings.calVals ==' \
	   && printf '%s\n' "$$tlbody" | grep -qF 'expected->settings.topLevelSettings.calVals' \
	   || { echo "ERROR: daqifi_settings_TopLevelMatchesCommitted() in"; \
	        echo "       $(DAQIFI_SETTINGS_SRC) no longer compares calVals against"; \
	        echo "       expected->...calVals. The model and the ordering guard"; \
	        echo "       above only pin this function's CALL SITE by presence and"; \
	        echo "       position -- neither exercises the real body, so a changed"; \
	        echo "       comparison here would pass every test in this file"; \
	        echo "       silently."; \
	        exit 1; }; \
	 if printf '%s\n' "$$tlbody" | grep -q 'memcmp'; then \
	   echo "ERROR: daqifi_settings_TopLevelMatchesCommitted() in"; \
	   echo "       $(DAQIFI_SETTINGS_SRC) appears to compare more than calVals"; \
	   echo "       now (found a memcmp) -- a whole-payload comparison is the"; \
	   echo "       r5 defect this function exists to avoid."; \
	   exit 1; \
	 fi
# #1152 round 11: committedMatchesIntended (added in round 10, fixing round
# 9's own unchecked premise) is pinned only by the ordering guard's call
# sites above (reread, continuing) -- neither touches this LOCAL variable's
# own derivation, or what it gates. Round 9's actual regression was
# classifying this exact branch by readability ALONE; pin the three facts
# that regression would undo: the variable exists, it is derived from
# comparing calVals on BOTH sides (committed vs. intended) rather than
# readability alone, and that derivation -- not some other condition --
# gates the branch deciding whether to fall through to the shared
# "continuing" install.
	@body=`sed -n '/^static scpi_result_t ADCUseCalSetClaimed(scpi_t \* context) {/,/^}/p' $(SCPIADC_SRC) | tr -d '\r' | grep -v '^[[:space:]]*[*/]'`; \
	 test -n "$$body" \
	   || { echo "ERROR: ADCUseCalSetClaimed() was not found in $(SCPIADC_SRC) for"; \
	        echo "       the committedMatchesIntended guard -- it can see nothing."; \
	        exit 1; }; \
	 cmidecl=`printf '%s\n' "$$body" | grep -n 'bool committedMatchesIntended = committedReadable &&' | cut -d: -f1`; \
	 cmicommitted=`printf '%s\n' "$$body" | grep -n 'committedSettings\.settings\.topLevelSettings\.calVals ==' | cut -d: -f1`; \
	 cmiintended=`printf '%s\n' "$$body" | grep -n 'tmpTopLevelSettings\.settings\.topLevelSettings\.calVals;' | cut -d: -f1`; \
	 cmigate=`printf '%s\n' "$$body" | grep -n 'if (!committedMatchesIntended)' | cut -d: -f1`; \
	 test -n "$$cmidecl" && test -n "$$cmicommitted" && test -n "$$cmiintended" && test -n "$$cmigate" \
	   && test "$$cmicommitted" -eq "$$((cmidecl+1))" \
	   && test "$$cmiintended" -eq "$$((cmidecl+2))" \
	   && test "$$cmigate" -gt "$$cmidecl" \
	   || { echo "ERROR: ADCUseCalSetClaimed() in $(SCPIADC_SRC) no longer derives"; \
	        echo "       committedMatchesIntended from a two-sided calVals comparison"; \
	        echo "       (committedSettings...calVals == tmpTopLevelSettings...calVals)"; \
	        echo "       directly below its own declaration, with that declaration"; \
	        echo "       gating the 'if (!committedMatchesIntended)' branch below it"; \
	        echo "       (decl='$$cmidecl' committedSide='$$cmicommitted'"; \
	        echo "        intendedSide='$$cmiintended' gate='$$cmigate')."; \
	        echo "       Round 9's own regression classified this branch by"; \
	        echo "       committedReadable ALONE -- i.e. exactly this calVals"; \
	        echo "       derivation removed -- so a guard that cannot see it cannot"; \
	        echo "       see round 9's bug coming back either."; \
	        exit 1; }
# #1152 round 9: every LOG_E in ADCUseCalSetClaimed() must render at or under
# the 125-byte usable payload (LOG_MESSAGE_SIZE 128, Logger.h:361, clamped to
# LOG_MESSAGE_SIZE-3 in LogMessageFormatImpl, Logger.c:398,405 -- the tail past
# that is silently dropped). Unlike $(LOGCEIL_BIN)'s check_log_block, which
# pins an exact expected copy, this extracts the REAL text from $(SCPIADC_SRC)
# and measures IT -- a message can be edited freely as long as it still fits,
# which is the actual constraint, rather than frozen verbatim. Each call finds
# its LOG_E's opening line (asserted unique and present), walks forward to the
# statement's closing '");' or ');', and concatenates every pure
# string-literal continuation line (one whose first non-blank character is
# '"') -- deliberately skipping lines like '(param1 == 1) ? "user" :
# "factory");' whose quotes are substitution ARGUMENTS, not format text, which
# a naive one-quote-pair-per-line extraction would misread as part of the
# message. %d is always a single digit here (param1 is 0 or 1 by this point),
# so its widening is fixed at one char; %s widens to whichever named argument
# renders longest (checked against validation_fail's "factory" vs "user").
	@check_usecal_log_len() { \
	  f="$$1"; anchor="$$2"; dwide="$$3"; swide="$$4"; label="$$5"; \
	  start=$$(grep -n "$$anchor" "$$f" | tr -d '\r' | head -1 | cut -d: -f1); \
	  if [ -z "$$start" ]; then \
	    echo "ERROR: $$label's LOG_E anchor was not found in $$f -- either"; \
	    echo "       it was edited, removed, or no longer opens on its own"; \
	    echo "       line, and the byte-budget check below can measure"; \
	    echo "       nothing. anchor=[$$anchor]"; \
	    return 1; \
	  fi; \
	  end=$$(awk -v s="$$start" 'NR>=s { line=$$0; gsub(/\r$$/,"",line); if (line ~ /\);/) { print NR; exit } }' "$$f"); \
	  block=$$(sed -n "$${start},$${end}p" "$$f" | tr -d '\r'); \
	  anchorline=$$(printf '%s\n' "$$block" | head -1); \
	  piece1=$$(printf '%s' "$$anchorline" | sed -n 's/^[^"]*"\(.*\)"[^"]*$$/\1/p'); \
	  rest=$$(printf '%s\n' "$$block" | tail -n +2); \
	  contlines=$$(printf '%s\n' "$$rest" | grep '^[[:space:]]*"' || true); \
	  pieces2=""; \
	  if [ -n "$$contlines" ]; then \
	    pieces2=$$(printf '%s\n' "$$contlines" | sed -n 's/^[[:space:]]*"\(.*\)"[^"]*$$/\1/p' | tr -d '\n'); \
	  fi; \
	  text="$${piece1}$${pieces2}"; \
	  wide=$$(printf '%s' "$$text" | sed "s/%d/$$dwide/g; s/%s/$$swide/g"); \
	  len=$$(printf '%s' "$$wide" | wc -c); \
	  if [ "$$len" -gt 125 ]; then \
	    echo "ERROR: $$label's LOG_E in $$f renders to $$len usable bytes"; \
	    echo "       with the widest substitution, over the 125-byte ceiling"; \
	    echo "       (LOG_MESSAGE_SIZE 128, Logger.h:361, clamped -3 in"; \
	    echo "       LogMessageFormatImpl, Logger.c:398,405). Rendered text:"; \
	    echo "       [$$wide]"; \
	    echo "       Shorten it -- the tail past byte 125 is silently"; \
	    echo "       dropped, so whatever falls past there never reaches a"; \
	    echo "       log reader. Remedy-first wording survives truncation"; \
	    echo "       best; keep it at the front of the message."; \
	    return 1; \
	  fi; \
	}; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: top-level settings NVM read failed; "' 1 '' top_level_read_fail || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: no calibration bank for this value",' 1 '' unreachable_bank || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: %s cal bank failed validation "' 1 factory validation_fail || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: selection unchanged - prior "' 1 '' round9_case_a_selection_unchanged || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: selection unreadable - reboot "' 1 '' round9_case_c_selection_unreadable || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: continuing - committed rec matches "' 1 '' case_b_continuing || exit 1; \
	check_usecal_log_len $(SCPIADC_SRC) 'LOG_E("CONF:ADC:USECal %d: reboot or re-select - bank re-read "' 1 '' reload_fail || exit 1
	$(CC) $(CFLAGS) -I$(FW_UTIL) -o $(USECALTYPE_BIN) \
	      test_1152_usecal_type_trust.c $(CRC32_SRC)

# Every suite, and every one of the 28 binaries. Additive collisions in
# this block so far: fw#961 vs fw#983, fw#913 vs fw#1004, fw#913 vs fw#1000,
# fw#913 vs fw#980, fw#913 vs fw#985, fw#999 vs fw#985, fw#999 vs fw#1057, and
# fw#971's own ARM_BIN colliding with fw#913/fw#980/fw#1000/fw#985 and then
# fw#999/fw#1057 as each landed on main, and now fw#971 vs fw#947 (SYSINFO_BIN,
# main's independent branch) as this PR re-merges main a second time -- every
# one of them two branches appending a line to these same two lists. The
# lineage replaces an ordinal
# because each branch numbered its own collisions and the ordinals disagreed
# (#971 said EIGHTH, main said NINTH), and three stale copies of this banner
# with three different counts had accumulated here. Resolved the same way every
# time: keep each side's target rule INTACT and union only `run` and `clean`.
# Dropping a rule loses a suite silently; duplicating one silently shadows the
# other. Update the count above whenever a resolution touches `run`.
# fw#1152 adds $(USECALTYPE_BIN); the count above is the RESOLVED union, counted
# from `run` rather than carried from either side -- both sides' banners disagreed
# with each other and with the list (HEAD said TWENTY-FOUR, main said FIFTEEN).
#
# #1026 is the ticket to stop this and PR #1040 implements it, moving each
# target into tests/host/tests/<issue>-<slug>.mk so a new suite appends to an
# accumulator instead of editing a shared line. Whichever lands first, the rest
# re-express their target as a fragment -- a mechanical move of one recipe,
# NOT A PLAIN UNION, and the exception is the point. This branch's `run`/`clean`
# lists are 40 commits stale and name two binaries that NO LONGER EXIST on main:
#   $(SPI913_BIN)  -- RENAMED to $(WAITLOOP_BIN) by fw#1056.
#   $(DAC1057_BIN) -- RETIRED by fw#1108 together with run_1057_tests and
#                     test_1057_dac7718_wait_stat.c, both deleted on main.
# Unioning them in would reference two undefined variables and resurrect a
# deleted test. So main's list is taken whole and only this branch's OWN
# addition is added to it: $(ARM_BIN) := run_971_tests. TWENTY-FOUR binaries.
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
# Every suite, and every one of the TWENTY-SIX binaries. SIXTEENTH additive
# collision in this block, resolved the same way as the fifteen before it:
# each side's target rule kept INTACT and only `run` and `clean` unioned.
# This refresh unions #1110's $(LOG1039_BIN) (the log-budget lint tool) with
# #1013's $(FAILNEXT_BIN)/$(FAILNEXT_REAL_BIN), which main gained on
# 2026-10-12 while this row sat at its reviewed head.
#
# THE COUNT IS DERIVED, NOT ASSERTED -- 28 binaries in `run`, counted from
# the resolved prerequisite list below by the resolver rather than written by hand.
# Both sides' stated counts were stale for this result (ours TWENTY-SIX, main's
# TWENTY-FOUR, each right about its own side) -- the trap the thirteenth collision
# recorded and every one since has restated.
#
# ALSO FIXED HERE, and NOT a union: the `clean:` recipe's $(SDLOG1029_GEN).tmp
# line was missing its line-continuation backslash, so make parsed the
# following tab-indented line as a SECOND recipe command and tried to EXECUTE
# `run_1039_tests gen_1039_log_budget.h` instead of deleting the two files.
# `make clean` therefore failed with "No such file or directory" / Error 127 at
# the reviewed head, and the two 1039 artifacts were never removed. CI runs
# only `make run`, never `clean`, so no gate on this row could observe it.
#
# $(LOG1039_BIN)/$(LOG1039_GEN) carry the 1039 suffix so they cannot shadow
# #1029's $(SDLOG1029_BIN)/$(SDLOG1029_GEN); #1029's in turn carry 1029 so they
# cannot shadow #1000's $(SDLOG_BIN)/$(GEN_1000). main's side of THIS collision is
# $(ARM_BIN) and $(NAME_BIN), from fw#958/fw#1027.
# BOTH SIDES' COUNTS WERE STALE FOR THE RESULT -- ours said TWENTY-ONE, main's
# said TWENTY-TWO, and each was right about its own side. The union is
# TWENTY-SEVEN. Corrected here rather than carried, for the reason the
# thirteenth collision already recorded: a stale count in a comment is simply
# false to the next reader.
#
#
# FIFTEENTH additive collision in this block, resolved the same way as the
# fourteen before it: each side's target rule kept INTACT and only `run` and
# `clean` unioned. This branch adds #1100's $(SCPIDAC_RAILGATE_BIN) (the
# DAC_EnsureHardwareInitialized rail-enable gate) while main added $(ARM_BIN),
# $(NAME_BIN), $(FAILNEXT_BIN) and $(FAILNEXT_REAL_BIN) via the fw#1027 merge.
#
# COUNTS, DERIVED FROM THE LISTS RATHER THAN CARRIED: main's side had
# TWENTY-SEVEN run prerequisites and this branch's had TWENTY-FOUR, each right
# about itself. The union is TWENTY-EIGHT -- main's twenty-seven plus this
# branch's one. Verified by cross-check rather than by eye: every one of the 28
# prerequisites has a `./$(X)` recipe line and an entry in `clean` (30/30/35),
# with no duplicates -- the check that catches a dropped or doubled entry, which
# is the failure this block's own history keeps recording.
# #1029's names carry the 1029 suffix so they cannot shadow #1000's
# $(SDLOG_BIN)/$(GEN_1000).
#
# SIXTEENTH additive collision, same resolution: each side's target rule kept
# INTACT, only `run` and `clean` unioned. This branch adds #908's
# $(SEQ908_BIN) (the cal-source seqlock guard); main added #904's
# $(TEAR904_BIN) and $(FORCEGATE_BIN).
#
# COUNTS, MEASURED ON THE MERGED LISTS -- 31 run prerequisites, 31 `./$(X)`
# recipe lines, 36 `clean` entries (35 unique). Base had 28/28/33, so main's
# two and this branch's one account for all three deltas exactly. The
# parenthetical in the FIFTEENTH paragraph above is superseded: it was bumped
# by one when main's merge added two, and read 29/29/34 against a tree that
# measured 30/30/35.
#
# ⚠ WHAT THIS PARAGRAPH IS ACTUALLY FOR. My first attempt at this merge
# unioned the two conflict regions as TEXT and produced 29 prerequisites with
# ONE `./$(X)` line -- every other test's invocation deleted, while the
# prerequisite list still named them. That builds all 29 binaries and runs one,
# so `make run` EXITS 0 having executed a single test: a green suite that tests
# almost nothing. Nothing in this file would have objected.
#
# So the resolution is not complete until prerequisites and invocations are
# measured and AGREE -- as SETS, both directions, not as totals, since equal
# totals are reachable from a swapped pair. `clean` additionally holds four
# generated artifacts that are not run prerequisites ($(UUT), $(GEN_1000),
# $(GEN_1125), $(SDLOG1029_GEN)), which is why only its number differs; the
# $(SDLOG1029_GEN) duplicate predates the merge base and is a harmless
# second `rm -f`, not something this merge introduced.
# ---------------------------------------------------------------------------
# PR firmware#1048 (issue #904): the CalM/CalB 64-bit pair, BOTH halves.
#
# This row was `blocked` on an operator ruling that it is "UNVALIDATABLE on
# this station's hardware" -- every board here holds IDENTITY coefficients, and
# a torn read of 1.0 / 0.0 is indistinguishable from a clean one. The ruling
# named two exits: a calibrated board, or a host-level test injecting the pair
# directly. test_904_cal_pair_tear.c is the second, so this target is what
# discharges the block.
#
# The guards below pin the WRITE half across all three writer sites, because
# the model cannot see the firmware. #1054 shipped the read half; #1086's
# read-side comment states the rule the matrix encodes -- "neither half is
# sufficient alone".
TEAR904_BIN := run_904_tests

# ⚠ THIS DEFINITION MUST STAY ABOVE THE RULE BELOW. make expands a
# PREREQUISITE list when it READS the rule, so a variable defined later in
# the file expands to NOTHING there and the dependency silently disappears.
# It was defined below `run:` and did exactly that: daqifi_settings.c was not
# a dependency of run_904_tests, so an edit to the file the guards grep
# reused the stale binary and the guards did not run.
#
# The recipe's uses were unaffected, which is what made it invisible -- recipe
# lines expand at EXECUTION time, after the whole file is parsed, so the greps
# always had the right path. Correct recipe, absent dependency.
#
# And do not verify this by printing the variable: `make -np` reports
# SETTINGS_SRC_904 as a valid path even while broken, because that dump also
# happens after the parse. The only evidence is the EXPANDED RULE line
# (`make -np | grep '^run_904_tests:'`), which must name this file.
SETTINGS_SRC_904 := ../../firmware/src/services/daqifi_settings.c

# Comment-stripper for the order guard below. `-fpreprocessed` is what makes
# this work on a Harmony .c: it does NOT resolve the #includes (nothing on this
# station can) and does NOT expand macros, so taskENTER_CRITICAL() is seen as
# written -- it only removes comments, which is the whole requirement.
STRIP904 := $(CC) -fpreprocessed -E -P

# ⚠ cal_order_904.awk IS A PREREQUISITE BELOW because the guards READ it.
# A guard that reads a file it does not depend on does not re-run when that
# file changes -- make reuses the binary and the check is silently skipped.
# That is the defect this row was pulled up on one round earlier; it applies
# to the checker itself exactly as it applied to the sources it checks.
$(TEAR904_BIN): test_904_cal_pair_tear.c test_framework.h cal_order_904.awk cal_order_904_cases.c $(SETTINGS_SRC_904) $(SCPIADC_SRC) $(RECIPES)
	@for spec in "daqifi_settings_LoadADCCalSettings:$(SETTINGS_SRC_904):EMBX" \
	             "daqifi_settings_SaveADCCalSettings:$(SETTINGS_SRC_904):EMBX" \
	             "ADCChanCalmSetClaimed:$(SCPIADC_SRC):EMX" \
	             "ADCChanCalbSetClaimed:$(SCPIADC_SRC):EBX"; do \
	   fn=`echo $$spec | cut -d: -f1`; src=`echo $$spec | cut -d: -f2`; \
	   want=`echo $$spec | cut -d: -f3`; \
	   got=`$(STRIP904) $$src 2>/dev/null | awk -v fn=$$fn -f cal_order_904.awk`; \
	   test "$$got" = "$$want" \
	     || { echo "ERROR: $$fn in $$src no longer brackets its 64-bit cal"; \
	          echo "       access in a critical section IN ORDER."; \
	          echo "       event sequence: got '$$got', want '$$want'"; \
	          echo "         E=taskENTER_CRITICAL  M=.CalM  B=.CalB  X=taskEXIT_CRITICAL"; \
	          echo "       A missing letter is a missing (or COMMENTED-OUT) call;"; \
	          echo "       a reordered one means a store sits OUTSIDE the section"; \
	          echo "       -- and that case has the same token COUNTS as a correct"; \
	          echo "       one, which is why this checks order, not counts."; \
	          echo "       #1054 guards the READERS, so an unguarded WRITER does"; \
	          echo "       not leave a symmetric gap -- it hands a protected"; \
	          echo "       reader a TORN pair, which the critical section then"; \
	          echo "       makes it confident in. See the 2x2 matrix in"; \
	          echo "       test_904_cal_pair_tear.c: neither half suffices alone."; \
	          exit 1; }; \
	 done
#	⚠ THE GUARD'S OWN ARMS -- it proves it can ACCEPT and REJECT, every build.
#	The guard this replaced passed with both critical-section calls commented
#	out, and nothing in the build would ever have said so. A positive check
#	alone cannot distinguish "the code is correct" from "the check is inert",
#	so both directions are asserted here rather than claimed in a commit
#	message, against the named shapes in cal_order_904_cases.c.
#
#	The two ACCEPT rows are load-bearing, not padding: a checker tightened
#	until it rejects everything is not a checker, and `oneline_correct` exists
#	because the fix for the one-line defect could easily have become "reject
#	anything with several events on one line".
	@fail=0; \
	 for case in case_multiline_correct:EMBX \
	             case_oneline_correct:EMBX \
	             case_oneline_store_outside:MEBX \
	             case_enter_commented:MBX \
	             case_missing_exit:EMB \
	             case_reordered_multiline:MEBX \
	             case_access_after_exit:EMBXM; do \
	   fn=`echo $$case | cut -d: -f1`; want=`echo $$case | cut -d: -f2`; \
	   got=`$(STRIP904) -x c cal_order_904_cases.c 2>/dev/null \
	          | awk -v fn=$$fn -f cal_order_904.awk`; \
	   test "$$got" = "$$want" || { \
	     echo "ERROR: the #904 order guard's own arm '$$fn' is wrong:"; \
	     echo "       got '$$got', want '$$want'."; \
	     echo "       The guard above cannot be trusted until this passes --"; \
	     echo "       an arm that no longer behaves means the checker, not the"; \
	     echo "       firmware, has changed. See cal_order_904_cases.c."; \
	     fail=1; }; \
	 done; \
	 test "$$fail" -eq 0
	$(CC) $(CFLAGS) -o $(TEAR904_BIN) test_904_cal_pair_tear.c

# CNT_IN_FN_DEF_904 lived here and is GONE: the order guard replaced its only
# two call sites, so it was a definition nothing called. Its lesson -- that the
# shared CNT_IN_FN returns 0 for a FORWARD-DECLARED function, because it
# anchors on the prototype and exits at the next match, so a guard expecting
# ABSENCE passes VACUOUSLY on that false 0 -- is kept in cal_order_904.awk's
# header, where the definition-vs-prototype rule is actually implemented.

# ⛔ #1110 r8 review LOW: THE SPELLED-OUT COUNTS ABOVE ARE HISTORICAL, AND
# NONE OF THEM IS A CLAIM ABOUT THIS LIST. Each belongs to one past merge and
# was true at that merge -- TWENTY-FOUR, TWENTY-FIVE, TWENTY-SIX, TWENTY-SEVEN
# are a lineage, not a running total, and the block already records twice that
# both sides' counts were stale for the result. Read as a current total the
# most recent one is simply false: `make -n run` expands to 31 today.
#
# So the live number is DERIVED ON DEMAND and deliberately written nowhere:
#
#     make -n run | grep -c '^\./run_'
#
# and the ordinals are superseded for the reason this block gives further up --
# each branch numbered its own collisions, so two merges claimed the same
# ordinal and whichever landed second was wrong. The LIST is the record.
# Do not add another count here; it is stale the next time anyone merges.
#
# MERGE (fw#1152 round 9 x fw#1048/#904): the two `run:` prerequisite lists
# are UNIONED rather than picked -- main's 30 (this TEAR904_BIN block plus 29
# others) union this branch's 30 (the same 29 plus USECALTYPE_BIN) is 31,
# verified by cross-check against the `./$(X)` invocation list and `clean:`
# below, which both already carried the full union untouched by this conflict.
# ---------------------------------------------------------------------------
# PR firmware#1077 round 8 (issue #908): identity.cal.source composite
# coherence. The reported source is a pair of facts -- "are the live
# coefficients identity" and "which bank did they come from" -- and
# daqifi_settings_LoadADCCalSettings changes them in that order, publishing
# the bank LAST (round 3's fix, so the bank cannot name a load that did not
# land). A reader pre-empting between the two therefore sees the OLD bank
# while scanning the NEW coefficients.
#
# Round 6 compared the bank before and after the scan, which catches a
# COMPLETED load and is blind to one IN PROGRESS. Round 8 brackets the whole
# composite update with a generation counter and the reader takes a seqlock
# read. test_908_cal_source_seqlock.c models all three reader shapes and
# asserts the middle one's blindness, so the file fails if the seqlock is
# weakened back to a bank comparison.
#
# The model cannot see the FIRMWARE, so the guards below pin the real thing:
# two increments in the writer with the first BEFORE the copy, and both arms
# of the seqlock in the reader. An order check, not a presence check -- a
# presence check passes if someone moves the pre-copy increment after the loop,
# which is precisely the mutation that reopens the defect.
SETTINGS_SRC := ../../firmware/src/services/daqifi_settings.c
SEQ908_BIN := run_908_tests

# ⚠ BOTH sources are prerequisites because BOTH are read by the guards below:
# SETTINGS_SRC for the load-order and reader-yield checks, SCPIADC_SRC for the
# setter-bracket check. A guard that reads a file it does not depend on does
# not run when that file changes -- make reuses the binary and the check is
# silently skipped, which is a guard that cannot fail rather than a guard.
$(SEQ908_BIN): test_908_cal_source_seqlock.c test_framework.h $(SETTINGS_SRC) $(SCPIADC_SRC) $(SCPI_IFACE_SRC) cal_compose_908.awk check_seq908_order.py seq908_order_fixture.c check_comment_integrity.py $(RECIPES)
	@sed -n '/^bool daqifi_settings_LoadADCCalSettings/,/^}/p' $(SETTINGS_SRC) \
	   > /tmp/.fw908_load.$$$$; \
	 n=`grep -cE 'daqifi_settings_(Begin|End)CalWrite' /tmp/.fw908_load.$$$$`; \
	 gen1=`grep -n 'daqifi_settings_BeginCalWrite' /tmp/.fw908_load.$$$$ | head -1 | cut -d: -f1`; \
	 cpy=`grep -n '\.CalM = calArray' /tmp/.fw908_load.$$$$ | head -1 | cut -d: -f1`; \
	 bank=`grep -n 'gActiveCalBank = type' /tmp/.fw908_load.$$$$ | head -1 | cut -d: -f1`; \
	 gen2=`grep -n 'daqifi_settings_EndCalWrite' /tmp/.fw908_load.$$$$ | tail -1 | cut -d: -f1`; \
	 rm -f /tmp/.fw908_load.$$$$; \
	 test "$$n" = "2" && test -n "$$gen1" && test -n "$$cpy" && test -n "$$bank" \
	   && test "$$gen1" -lt "$$cpy" && test "$$gen2" -gt "$$bank" \
	  || { echo "ERROR: daqifi_settings_LoadADCCalSettings in $(SETTINGS_SRC) no"; \
	       echo "       longer brackets its composite update with exactly one"; \
	       echo "       daqifi_settings_BeginCalWrite BEFORE the coefficient copy"; \
	       echo "       and one EndCalWrite AFTER gActiveCalBank is published."; \
	       echo "       (found n=$$n gen1=$$gen1 copy=$$cpy bank=$$bank gen2=$$gen2)"; \
	       echo "       Without the PRE-copy increment the generation never goes"; \
	       echo "       odd, so a reader cannot see an update IN PROGRESS and"; \
	       echo "       reports the old bank against the new coefficients --"; \
	       echo "       the SHAPE_BANKCMP row of test_908_cal_source_seqlock.c."; \
	       exit 1; }
	@sed -n '/^DaqifiCalSource daqifi_settings_GetCalSource/,/^}/p' $(SETTINGS_SRC) \
	   > /tmp/.fw908_rd.$$$$; \
	 ok=1; \
	 grep -qF '& 1u' /tmp/.fw908_rd.$$$$ || ok=0; \
	 grep -qF 'gCalGeneration != genBefore' /tmp/.fw908_rd.$$$$ || ok=0; \
	 rm -f /tmp/.fw908_rd.$$$$; \
	 test "$$ok" = 1 \
	  || { echo "ERROR: daqifi_settings_GetCalSource in $(SETTINGS_SRC) is missing"; \
	       echo "       an arm of its seqlock read: it must REFUSE an odd"; \
	       echo "       generation (& 1u) AND require the generation unchanged"; \
	       echo "       across the scan. Either arm alone is insufficient --"; \
	       echo "       the odd check misses a load that starts and finishes"; \
	       echo "       during the scan, and the unchanged check misses one"; \
	       echo "       already in flight when the scan begins."; \
	       exit 1; }
	@sed -n '/^DaqifiCalSource daqifi_settings_GetCalSource/,/^}/p' $(SETTINGS_SRC) \
	   > /tmp/.fw908_yield.$$$$; \
	 y=`grep -c 'vTaskDelay' /tmp/.fw908_yield.$$$$`; \
	 rm -f /tmp/.fw908_yield.$$$$; \
	 test "$$y" -ge 1 \
	  || { echo "ERROR: daqifi_settings_GetCalSource in $(SETTINGS_SRC) retries"; \
	       echo "       without yielding. It runs on a SCPI transport task at"; \
	       echo "       priority 7; a bank load runs at priority 2, so spinning is"; \
	       echo "       a priority inversion -- the writer it waits for cannot be"; \
	       echo "       scheduled, every attempt sees the same odd generation, and"; \
	       echo "       a suspended load turns real calibration into a permanent"; \
	       echo "       source:\"none\". Yield (bounded) between attempts."; \
	       exit 1; }
	@for fn in ADCChanCalmSetClaimed ADCChanCalbSetClaimed; do \
	   b=`$(call CNT_IN_FN_DEF,$(SCPIADC_SRC),$$fn,daqifi_settings_BeginCalWrite)`; \
	   e=`$(call CNT_IN_FN_DEF,$(SCPIADC_SRC),$$fn,daqifi_settings_EndCalWrite)`; \
	   test "$$b" = "1" && test "$$e" = "1" \
	     || { echo "ERROR: $$fn in $(SCPIADC_SRC) does not bracket its CalM/CalB"; \
	          echo "       store with daqifi_settings_BeginCalWrite/EndCalWrite"; \
	          echo "       (found begin=$$b end=$$e, want 1 and 1)."; \
	          echo "       ONE setter alone yields a genuine before-or-after, so"; \
	          echo "       this looks exempt and is not: TWO setters let an"; \
	          echo "       early-exiting identity scan read channel 0 as identity,"; \
	          echo "       then one clears a later channel while the other sets"; \
	          echo "       channel 0, and the scan concludes ALL identity -- a view"; \
	          echo "       that held at no instant. See test_908's two-setter case."; \
	          exit 1; }; \
	 done
	@sed -n '/^DaqifiCalSource daqifi_settings_GetCalSource/,/^}/p' $(SETTINGS_SRC) \
	   > /tmp/.fw908_unk.$$$$; \
	 nu=`grep -c 'DAQIFI_CAL_SOURCE_UNKNOWN' /tmp/.fw908_unk.$$$$`; \
	 lastret=`grep -n 'return ' /tmp/.fw908_unk.$$$$ | tail -1 | cut -d: -f2-`; \
	 rm -f /tmp/.fw908_unk.$$$$; \
	 case "$$lastret" in *DAQIFI_CAL_SOURCE_UNKNOWN*) ok=1;; *) ok=0;; esac; \
	 test "$$ok" = 1 && test "$$nu" = "1" \
	  || { echo "ERROR: daqifi_settings_GetCalSource in $(SETTINGS_SRC) does not"; \
	       echo "       end by returning DAQIFI_CAL_SOURCE_UNKNOWN."; \
	       echo "       (last return was:$$lastret ; UNKNOWN mentions=$$nu, want 1)"; \
	       echo "       That final return is the RETRIES-EXHAUSTED path: every"; \
	       echo "       attempt saw a load in flight, so no coherent scan"; \
	       echo "       completed and nothing is known about the coefficients."; \
	       echo "       Returning DAQIFI_CAL_SOURCE_NONE there -- which is what"; \
	       echo "       it did before r10 -- is not a cautious understatement,"; \
	       echo "       it is the POSITIVE claim \"every channel is identity,"; \
	       echo "       readings are unscaled\", asserted by a reader that never"; \
	       echo "       looked. The state is reachable exactly while a bank is"; \
	       echo "       LOADING, i.e. when real coefficients are most likely"; \
	       echo "       present, so the worst answer was reachable in the case"; \
	       echo "       that mattered most."; \
	       echo "       Exactly ONE mention is required so UNKNOWN stays"; \
	       echo "       unreachable from the scan-completed paths above: if it"; \
	       echo "       can also be returned after a successful scan, then"; \
	       echo "       'unknown' on the wire stops meaning 'the read did not"; \
	       echo "       complete' and the value is no longer diagnostic."; \
	       exit 1; }
	@sed -n '/switch (daqifi_settings_GetCalSource/,/^    }/p' $(SCPI_IFACE_SRC) \
	   > /tmp/.fw908_sw.$$$$; \
	 nc=`grep -c 'case DAQIFI_CAL_SOURCE_' /tmp/.fw908_sw.$$$$`; \
	 dflt=`grep 'default:' /tmp/.fw908_sw.$$$$`; \
	 rm -f /tmp/.fw908_sw.$$$$; \
	 case "$$dflt" in *unknown*) dok=1;; *) dok=0;; esac; \
	 test "$$nc" = "4" && test "$$dok" = 1 \
	  || { echo "ERROR: the identity.cal.source switch in $(SCPI_IFACE_SRC) must"; \
	       echo "       name all FOUR DaqifiCalSource values explicitly and its"; \
	       echo "       default arm must answer \"unknown\"."; \
	       echo "       (found cases=$$nc want 4; default arm:$$dflt)"; \
	       echo "       ⚠ THIS GUARD EXISTS BECAUSE -Wswitch CANNOT CATCH IT. A"; \
	       echo "       switch with a default arm is exhaustive by definition, so"; \
	       echo "       adding a value to the enum produces no warning -- the new"; \
	       echo "       value is absorbed SILENTLY by whatever default says. The"; \
	       echo "       pre-r10 form was exactly that: FACTORY, USER, and"; \
	       echo "       default->\"none\", which would have reported the new"; \
	       echo "       UNKNOWN state as \"none\" and reintroduced the very"; \
	       echo "       collapse the fourth value was added to remove, while a"; \
	       echo "       reviewer saw a switch that handled every case."; \
	       echo "       The default must say \"unknown\" and not \"none\" because"; \
	       echo "       that is the fail-safe direction: an enum value this code"; \
	       echo "       cannot interpret is something it knows nothing about."; \
	       exit 1; }
	@$(CC) -fpreprocessed -E -P -x c - < $(SCPIADC_SRC) > /tmp/.fw908_fp.$$$$ 2>/dev/null; \
	 tot=`grep -cE 'daqifi_settings_(Clear|Mark)FactoryCalMissing\(' /tmp/.fw908_fp.$$$$`; \
	 inref=`sed -n '/^static void RefreshFactoryCalPresence/,/^}/p' /tmp/.fw908_fp.$$$$ \
	        | grep -cE 'daqifi_settings_(Clear|Mark)FactoryCalMissing\('`; \
	 val=`sed -n '/^static void RefreshFactoryCalPresence/,/^}/p' /tmp/.fw908_fp.$$$$ \
	      | grep -c 'daqifi_settings_ValidateADCCalBank('`; \
	 sav=`sed -n '/^static scpi_result_t CalSaveCommon/,/^}/p' /tmp/.fw908_fp.$$$$ \
	      | grep -c 'RefreshFactoryCalPresence()'`; \
	 lod=`sed -n '/^static scpi_result_t CalLoadCommon/,/^}/p' /tmp/.fw908_fp.$$$$ \
	      | grep -c 'RefreshFactoryCalPresence()'`; \
	 rm -f /tmp/.fw908_fp.$$$$; \
	 test "$$tot" = "2" && test "$$inref" = "2" && test "$$val" = "1" \
	   && test "$$sav" = "1" && test "$$lod" = "1" \
	  || { echo "ERROR: factory_present is no longer derived from the PAGE by a"; \
	       echo "       single writer in $(SCPIADC_SRC)."; \
	       echo "       (Clear/Mark calls: $$tot in the file, $$inref inside"; \
	       echo "        RefreshFactoryCalPresence; ValidateADCCalBank in it:"; \
	       echo "        $$val; RefreshFactoryCalPresence calls -- save: $$sav"; \
	       echo "        load: $$lod. Want 2 / 2 / 1 / 1 / 1.)"; \
	       echo "       ⛔ EQUAL COUNTS ARE THE POINT: every Clear/Mark in the"; \
	       echo "       file must be INSIDE the one helper. factory_present is a"; \
	       echo "       claim about what is ON THE PAGE, and any other call site"; \
	       echo "       would be deriving it from something else -- which is the"; \
	       echo "       r13 defect: it came from SaveADCCalSettings's RETURN"; \
	       echo "       VALUE, a claim about the WRITE. #1153's race (LAN:SAVE"; \
	       echo "       takes no claim and shares gTempFflashBuffer) makes that"; \
	       echo "       return true over a corrupt slot or false over an intact"; \
	       echo "       one, and the flag then stays wrong in BOTH directions."; \
	       echo "       The LOAD call site is pinned too: before r13 that path"; \
	       echo "       touched the flag at all, so a stale 'missing' survived a"; \
	       echo "       successful LOADFcal -- the one operation that proves the"; \
	       echo "       page is valid."; \
	       exit 1; }
	@$(CC) -fpreprocessed -E -P -x c - < $(SCPIADC_SRC) > /tmp/.fw908_cmp.$$$$ 2>/dev/null; \
	 bad=; \
	 for fn in ADCChanCalmSetClaimed ADCChanCalbSetClaimed; do \
	   seq=`awk -v fn=$$fn -f cal_compose_908.awk /tmp/.fw908_cmp.$$$$`; \
	   test "$$seq" = "GEVXH" || bad="$$bad $$fn=[$$seq]"; \
	 done; \
	 rm -f /tmp/.fw908_cmp.$$$$; \
	 test -z "$$bad" \
	  || { echo "ERROR: a calibration setter in $(SCPIADC_SRC) does not COMPOSE"; \
	       echo "       the generation bracket with the critical section as"; \
	       echo "       G E V X H (begin / enter / store / exit / end)."; \
	       echo "       Offending:$$bad"; \
	       echo "       Read as: G=BeginCalWrite E=taskENTER_CRITICAL"; \
	       echo "       V=the .CalM/.CalB store X=taskEXIT_CRITICAL"; \
	       echo "       H=EndCalWrite, in SOURCE order, adjacent repeats"; \
	       echo "       collapsed. An EMPTY sequence means the function"; \
	       echo "       DEFINITION was not found, not that it is clean."; \
	       echo "       ⚠ NEITHER EXISTING GUARD SET CAN SEE THIS, and the gap"; \
	       echo "       was authored by the #1048 merge rather than by an edit,"; \
	       echo "       so no diff line reveals it: cal_order_904.awk emits no"; \
	       echo "       token for BeginCalWrite, so EVX scores the same whether"; \
	       echo "       the bracket is outside the section, inside it, or gone;"; \
	       echo "       and the 908 bracket check above counts begin/end without"; \
	       echo "       regard to position. Each set is satisfied by the"; \
	       echo "       arrangement the other was meant to exclude."; \
	       echo "       Both orderings are load-bearing: the generation must"; \
	       echo "       span the WHOLE write, or a reader samples an even"; \
	       echo "       generation mid-write and believes it; and the critical"; \
	       echo "       section must be INNERMOST around the store, or the"; \
	       echo "       64-bit CalM/CalB pair can tear (#904)."; \
	       exit 1; }
	@$(PYTHON) check_comment_integrity.py $(SETTINGS_SRC) $(SCPIADC_SRC) $(SCPI_IFACE_SRC) \
	  || { echo "ERROR: a firmware file this target's guards READ no longer has"; \
	       echo "       intact comment structure, so it almost certainly does not"; \
	       echo "       compile. See the line(s) above."; \
	       echo "       ⚠ WHY THIS GUARD EXISTS: every other #908 source check"; \
	       echo "       below reads these files with sed/grep/awk, and NOTHING in"; \
	       echo "       tests/host COMPILES them. On 2026-10-27 a #define inserted"; \
	       echo "       into the middle of a block comment closed it early and"; \
	       echo "       stranded 13 lines at file scope; the firmware did not"; \
	       echo "       build, and this suite reported 31 binaries / 318 tests /"; \
	       echo "       0 failed with every position pin PASSING, because a pin"; \
	       echo "       over grep line numbers is happy with invalid C."; \
	       echo "       ⛔ A pass here is NOT a compile. Only a build is."; \
	       exit 1; }
	@$(PYTHON) check_seq908_order.py $(CC) seq908_order_fixture.c \
	  || { echo "ERROR: the cal-source seqlock's ORDERING property failed."; \
	       echo "       See the two lines above: the guard is differential, and"; \
	       echo "       WHICH line failed decides what is wrong. A failure on"; \
	       echo "       reader_barriered means the firmware property is broken;"; \
	       echo "       a failure on reader_unbarriered means this compiler did"; \
	       echo "       not reorder, so the other line proves nothing."; \
	       exit 1; }
	@sed -n '/^DaqifiCalSource daqifi_settings_GetCalSource/,/^}/p' $(SETTINGS_SRC) \
	   > /tmp/.fw908_bar.$$$$; \
	 g1=`grep -n 'genBefore = gCalGeneration' /tmp/.fw908_bar.$$$$ | head -1 | cut -d: -f1`; \
	 b1=`grep -n 'CAL_BARRIER' /tmp/.fw908_bar.$$$$ | head -1 | cut -d: -f1`; \
	 sc=`grep -n 'daqifi_settings_CalIsIdentity' /tmp/.fw908_bar.$$$$ | head -1 | cut -d: -f1`; \
	 bk=`grep -n 'bank = gActiveCalBank' /tmp/.fw908_bar.$$$$ | head -1 | cut -d: -f1`; \
	 b2=`grep -n 'CAL_BARRIER' /tmp/.fw908_bar.$$$$ | tail -1 | cut -d: -f1`; \
	 g2=`grep -n 'gCalGeneration != genBefore' /tmp/.fw908_bar.$$$$ | head -1 | cut -d: -f1`; \
	 nb=`grep -c 'CAL_BARRIER' /tmp/.fw908_bar.$$$$`; \
	 rm -f /tmp/.fw908_bar.$$$$; \
	 test "$$nb" = "2" && test -n "$$g1" && test -n "$$bk" && test -n "$$g2" \
	   && test "$$g1" -lt "$$b1" && test "$$b1" -lt "$$sc" \
	   && test "$$sc" -lt "$$bk" && test "$$bk" -lt "$$b2" \
	   && test "$$b2" -lt "$$g2" \
	  || { echo "ERROR: daqifi_settings_GetCalSource in $(SETTINGS_SRC) no longer"; \
	       echo "       brackets its snapshot with exactly two CAL_BARRIER()s in"; \
	       echo "       the required POSITIONS."; \
	       echo "       (barriers=$$nb want 2; order read=$$g1 bar=$$b1 scan=$$sc"; \
	       echo "        bank=$$bk bar=$$b2 check=$$g2, each must increase)"; \
	       echo "       ⚠ THE FIXTURE GUARD ABOVE CANNOT CATCH THIS. It compiles"; \
	       echo "       seq908_order_fixture.c, which is a MODEL -- it would keep"; \
	       echo "       passing with the firmware's barriers deleted. Position is"; \
	       echo "       pinned rather than presence because two barriers in the"; \
	       echo "       wrong places constrain nothing: the second one is what"; \
	       echo "       keeps the bank load above the closing generation read,"; \
	       echo "       and a barrier after that read is free of charge."; \
	       exit 1; }
	@bb=`sed -n '/^void daqifi_settings_BeginCalWrite/,/^}/p' $(SETTINGS_SRC) \
	      | grep -n 'gCalGeneration++\|CAL_BARRIER' | head -2 | cut -d: -f2- | tr -d ' \t' | paste -sd,`; \
	 ee=`sed -n '/^void daqifi_settings_EndCalWrite/,/^}/p' $(SETTINGS_SRC) \
	      | grep -n 'gCalGeneration++\|CAL_BARRIER' | head -2 | cut -d: -f2- | tr -d ' \t' | paste -sd,`; \
	 case "$$bb" in gCalGeneration++*,CAL_BARRIER*) ok=1;; *) ok=0;; esac; \
	 case "$$ee" in CAL_BARRIER*,gCalGeneration++*) ;; *) ok=0;; esac; \
	 test "$$ok" = 1 \
	  || { echo "ERROR: the generation bracket's own barriers are missing or"; \
	       echo "       mis-ordered in $(SETTINGS_SRC)."; \
	       echo "       BeginCalWrite must be increment-then-barrier (saw: $$bb)"; \
	       echo "       EndCalWrite   must be barrier-then-increment (saw: $$ee)"; \
	       echo "       They are MIRROR IMAGES, and swapping either one makes the"; \
	       echo "       mark useless without making it absent: nothing may sink"; \
	       echo "       below the ODD increment and nothing may rise above the"; \
	       echo "       EVEN one, or a reader that correctly observes an even"; \
	       echo "       generation can still see a partially-applied write."; \
	       echo "       Marking an update does not help if the optimiser may"; \
	       echo "       move the update out of the marks."; \
	       exit 1; }
	$(CC) $(CFLAGS) -o $(SEQ908_BIN) test_908_cal_source_seqlock.c

run: $(SEQ908_BIN) $(TEAR904_BIN) $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) \
     $(HELP_BIN) $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(LOGCEIL_BIN) \
     $(SCAN1112_BIN) $(STARTARM_BIN) $(START_BIN) $(REPLYBUF_BIN) $(HEXWIN_BIN) $(LOGONCE_BIN) \
     $(DACREINIT_BIN) $(CALFINITE_BIN) $(SDLOG1029_BIN) $(USECALTYPE_BIN) $(ARM_BIN) $(NAME_BIN) \
     $(FAILNEXT_BIN) $(FAILNEXT_REAL_BIN) $(SCPIDAC_RAILGATE_BIN) $(FORCEGATE_BIN) $(LOG1039_BIN) $(MANIFEST_BIN) $(GATE1124_BIN) $(BIN970)
	./$(SEQ908_BIN)
	./$(TEAR904_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(ARM_BIN)
	./$(FORCEGATE_BIN)
	./$(JSONESC_BIN)
	./$(SYSINFO_BIN)
	./$(SUSPEND_BIN)
	./$(DAC980_BIN)
	./$(HELP_BIN)
	./$(NAME_BIN)
	./$(WAITLOOP_BIN)
	./$(SDLOG_BIN)
	./$(SCPI999_BIN)
	./$(SUSPENDREASON_BIN)
	./$(LOGCEIL_BIN)
	./$(SCAN1112_BIN)
	./$(STARTARM_BIN)
	./$(START_BIN)
	./$(MANIFEST_BIN)
	./$(GATE1124_BIN)
	./$(REPLYBUF_BIN)
	./$(HEXWIN_BIN)
	./$(LOGONCE_BIN)
	./$(USECALTYPE_BIN)
	./$(DACREINIT_BIN)
	./$(CALFINITE_BIN)
	./$(SDLOG1029_BIN)
	./$(LOG1039_BIN)
	./$(BIN970)
	./$(FAILNEXT_BIN)
	./$(FAILNEXT_REAL_BIN)
	./$(SCPIDAC_RAILGATE_BIN)

clean:
	rm -f $(SEQ908_BIN)
	rm -f $(TEAR904_BIN)
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SYSINFO_BIN) $(SUSPEND_BIN) $(DAC980_BIN) $(HELP_BIN) \
	      $(WAITLOOP_BIN) $(SDLOG_BIN) $(SCPI999_BIN) $(SUSPENDREASON_BIN) $(GEN_1000) $(LOGCEIL_BIN) \
	      $(SCAN1112_BIN) $(STARTARM_BIN) $(START_BIN) $(REPLYBUF_BIN) $(HEXWIN_BIN) $(LOGONCE_BIN) $(GEN_1125) \
	      $(DACREINIT_BIN) $(CALFINITE_BIN) $(SDLOG1029_BIN) $(SDLOG1029_GEN) $(SDLOG1029_GEN).tmp \
	      $(USECALTYPE_BIN) $(NAME_BIN) $(FAILNEXT_BIN) $(FAILNEXT_REAL_BIN) $(ARM_BIN) $(SCPIDAC_RAILGATE_BIN) \
	      $(FORCEGATE_BIN) $(LOG1039_BIN) $(LOG1039_GEN) $(MANIFEST_BIN) $(GATE1124_BIN) $(BIN970)

.PHONY: run clean
