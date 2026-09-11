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
#
# ---------------------------------------------------------------------------
# #1026: each test's binary/recipe/guards used to live directly in this file,
# with `run:` and `clean:` naming every binary on one shared line each. Every
# PR adding a new host test therefore edited those same two lines, so two
# independent, non-overlapping test additions still collided on `run:`/
# `clean:` — measured 2026-09-11 at 4 of 4 host-test PRs CONFLICTING on this
# file, 0 of 2 that didn't touch it. Resolving the conflict moves the PR's
# head, which re-triggers `/agentic_review` and voids the SHA-bound
# adversarial audit — a converged, audited PR had to buy both again just
# because a sibling PR merged first.
#
# Fix: each test now lives in its own fragment under tests/*.mk (relative to
# this file, i.e. tests/host/tests/*.mk), which this file `include`s. A
# fragment declares its own binary variable, its own recipe (guards
# unchanged), and appends itself to the shared $(TESTS) / $(CLEAN_EXTRA)
# accumulators — it does NOT edit a shared line. Two branches each adding a
# different fragment file merge with zero conflict: git sees two adds to two
# different paths. See this directory's README.md, "Adding another module"
# section, for the convention new tests follow.
# ---------------------------------------------------------------------------

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O0 -g
FW_UTIL  := ../../firmware/src/Util
FW_ADC   := ../../firmware/src/HAL/ADC
FW_SVC   := ../../firmware/src/services
INCLUDES := -Istubs -I$(FW_UTIL)

# Shared firmware source paths a fragment's guards/recipes may grep or
# compile against. Add here (not in a fragment) when more than one fragment
# needs the same path — SD_BENCH_SRC and SCPI_IFACE_SRC each already do.
SD_BENCH_SRC  := ../../firmware/src/services/SCPI/SCPIStorageSD.c
SCPI_IFACE_SRC := ../../firmware/src/services/SCPI/SCPIInterface.c
FW_RTOS_CFG   := ../../firmware/src/config/default/FreeRTOSConfig.h
FW_LOG_H      := $(FW_UTIL)/Logger.h
FW_LOG_C      := $(FW_UTIL)/Logger.c

# Every binary depends on this file as well as on its sources. Some fragments
# carry GUARDS in their recipes -- greps that fail the build when the
# firmware text they model has moved -- and a guard that is only re-run when
# a source changes is not re-run when the GUARD itself changes. CI is
# unaffected either way (Actions checks out clean, so `make run` always
# builds from nothing); this is for the local incremental build, where the
# binary outliving an edited recipe is exactly how a guard goes quiet. Qodo,
# PR #983. Each fragment ALSO lists itself (via $(lastword $(MAKEFILE_LIST)))
# as a prerequisite of its own binary, so editing that one fragment's guard
# invalidates only that binary rather than none of them.
RECIPES := Makefile

# Accumulators every fragment appends to. Must be defined (even empty)
# BEFORE the include below, so `+=` has something to append to regardless of
# include order.
TESTS       :=
CLEAN_EXTRA :=

# GNU Make's default goal (what a bare `make` with no target builds) is the
# first real target it reads, and the fragment `include` below happens
# BEFORE `run:` is textually defined — so without this, a bare `make` would
# silently build just one fragment's binary (whichever tests/*.mk sorts
# first) and run nothing, exit 0. Verified 2026-09-11: pre-fix, bare `make`
# built only run_1000_tests and executed no tests. Pinning the default goal
# here, before the include, makes a bare `make` == `make run` regardless of
# fragment sort order. Qodo, PR #1040.
.DEFAULT_GOAL := run

include $(sort $(wildcard tests/*.mk))

# Every suite, in whatever order $(sort $(wildcard tests/*.mk)) included the
# fragments (lexicographic by filename — see this directory's README.md).
# `make run` builds every binary in $(TESTS) as a prerequisite (a failed
# build, e.g. a tripped guard, fails here before any test runs), then
# executes each in turn, stopping at the first failure the same way a
# literal per-binary recipe line would.
run: $(TESTS)
	@for t in $(TESTS); do echo "./$$t"; ./$$t || exit 1; done

clean:
	rm -f $(TESTS) $(CLEAN_EXTRA)

.PHONY: run clean
