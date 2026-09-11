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

# #164: JSON_StringEscape.h is header-only and dependency-free for the same
# reason AD7609Scale.h is -- the pure byte-escaping logic was split out of
# JSON_Encoder.c, which is unusable on a host (it pulls in
# state/board/BoardConfig.h -> Harmony's configuration.h/definitions.h and the
# whole board/driver graph). No UUT copy, no stubs, and NOT $(INCLUDES).
JSONESC_BIN := run_json_escape_tests

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

# Every binary depends on this file as well as on its sources. Two of the
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

SCPI999_WIRING_GUARD := check_999_createscpicontext_wiring.sh
SCPI999_ARG_EXTRACTOR := extract_call_args.pl

$(SCPI999_BIN): test_999_scpi_context_storage_isolation.c test_framework.h \
                $(LIBSCPI_SRCS) $(LIBSCPI_HDRS) $(SCPI_INTERFACE_H) $(SCPI_INTERFACE_C) \
                $(USBCDC_SRC) $(WIFI_TCP_SRC) $(SCPI999_WIRING_GUARD) \
                $(SCPI999_ARG_EXTRACTOR) $(RECIPES)
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

# Every suite, and every one of the six binaries. Merge conflict (fw#961
# JSONESC_BIN vs fw#983 SUSPEND_BIN, both merging around the same time)
# resolved by keeping each side's target intact and merging only `run` and
# `clean` -- a duplicated rule here would silently shadow one of them.
run: $(BIN) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(SCPI999_BIN)
	./$(BIN)
	./$(FMT_BIN)
	./$(AD7609_BIN)
	./$(BENCH_BIN)
	./$(JSONESC_BIN)
	./$(SUSPEND_BIN)
	./$(SCPI999_BIN)

clean:
	rm -f $(BIN) $(UUT) $(FMT_BIN) $(AD7609_BIN) $(BENCH_BIN) $(JSONESC_BIN) $(SUSPEND_BIN) $(SCPI999_BIN) *.o

.PHONY: run clean
