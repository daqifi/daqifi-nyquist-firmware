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
TEST_SOURCES_CLAIMED :=

# GNU Make's default goal (what a bare `make` with no target builds) is the
# first real target it reads, and the fragment `include` below happens
# BEFORE `run:` is textually defined — so without this, a bare `make` would
# silently build just one fragment's binary (whichever tests/*.mk sorts
# first) and run nothing, exit 0. Verified 2026-09-11: pre-fix, bare `make`
# built only run_1000_tests and executed no tests. Pinning the default goal
# here, before the include, makes a bare `make` == `make run` regardless of
# fragment sort order. Qodo, PR #1040.
.DEFAULT_GOAL := run

FRAGMENTS := $(sort $(wildcard tests/*.mk))
include $(FRAGMENTS)

# `make clean` MUST WORK IN AN INCONSISTENT TREE, and the guards below are
# parse-time $(error)s, which abort every goal including clean.
#
# That matters here specifically, because the scenario this PR exists to survive
# IS the inconsistent tree: a fragment dropped or renamed by a merge. Before
# this, `make clean` in that state failed with the same error as `make run`, so
# the orphaned binary could only be removed by hand -- a recovery path broken in
# exactly the case the recoverability is for. The monolithic Makefile had no
# $(error) at all, so this was a regression against it. Found by the #1040
# pre-merge audit, which was asked to look at it.
#
# CLEAN_ONLY is set only when the goals are EXACTLY clean (one or more times).
# A bare `make` has no goals and must NOT bypass anything -- .DEFAULT_GOAL is
# run. `make clean run` must not bypass either, which is why this filters the
# whole goal list rather than testing for clean's presence.
CLEAN_ONLY := $(if $(MAKECMDGOALS),$(if $(filter-out clean,$(MAKECMDGOALS)),,yes),)

ifeq ($(CLEAN_ONLY),)

# REFUSE A SUITE THAT CANNOT FAIL.
#
# `run:` takes its prerequisites from $(TESTS), which the fragments append to.
# If the wildcard above matches nothing -- a renamed or absent tests/ directory,
# a bad checkout, a .gitignore that swallows *.mk -- then nothing is included,
# $(TESTS) is empty, and `make run` exits 0 having built nothing and executed
# nothing. Verified by execution on this branch before this guard existed:
# `mv tests tests-hidden && make run` printed nothing, built 0 binaries and
# exited 0, against 8 suites on the same tree with the directory in place.
#
# The monolithic Makefile this PR replaces could not do that: `run:` named all
# eight binaries literally, so a missing test source failed the build loudly.
# Moving to a wildcard is what introduces the failure mode, so the guard ships
# with it.
#
# FOUR conditions. The first two catch losing ALL the fragments; the third and
# fourth catch losing or breaking ONE, which is likelier and just as silent:
#   * at least one fragment must have been included at all;
#   * $(TESTS) must be non-empty after the include;
#   * every included fragment must have contributed a binary, so a fragment
#     that forgets or misspells its `TESTS +=` is caught;
#   * there must be one fragment per test SOURCE, so a fragment file that is
#     simply deleted or renamed is caught by the test_*.c it orphans.
#
# The fourth is the one that needed a second attempt. The first version of this
# guard compared $(TESTS) against $(FRAGMENTS) only, and a mutation removing one
# fragment FILE passed it: seven fragments registering seven binaries is
# self-consistent. Comparing against the test sources instead is what makes an
# absent fragment visible, because adding a suite adds both files and removing
# one leaves the other behind.
#
# Deliberately NOT a hard expected-count constant. That number drifts with
# every added suite, and in this same file it drifted twice before this PR --
# "Two of the targets carry GUARDS" while four did, and "Seven greps" while
# there were eight. Deriving the bound from $(FRAGMENTS) keeps it correct
# without anyone remembering to bump it.
ifeq ($(strip $(FRAGMENTS)),)
$(error no test fragments found: tests/*.mk matched nothing, so `make run` \
would build nothing and pass. Run this from tests/host/, and check the \
tests/ directory exists and its *.mk files are not ignored)
endif
ifeq ($(strip $(TESTS)),)
$(error $(words $(FRAGMENTS)) fragment(s) were included but $$(TESTS) is \
empty, so `make run` would build nothing and pass. Every fragment must end \
with `TESTS += $$(ITS_BIN)`)
endif
ifneq ($(words $(TESTS)),$(words $(FRAGMENTS)))
$(error $(words $(FRAGMENTS)) fragment(s) included but $(words $(TESTS)) \
binar(ies) registered -- one fragment is not contributing its binary to \
$$(TESTS). Fragments: $(FRAGMENTS). Registered: $(TESTS))
endif

# One fragment per test source, BY IDENTITY rather than by count.
#
# A count comparison was the first version of this guard and Qodo's review of
# PR #1040 broke it: delete tests/943-bench-stall-bound.mk and copy
# tests/953-bench-suspend-diagnosis.mk into its place, and there are still 8
# fragments, 8 sources and 8 registered binaries -- every count balances --
# while `make run` runs run_953_tests TWICE and never runs run_943_tests.
# Reproduced before changing this: exit 0, eight lines of output,
# run_943_tests never executed, no guard fired. `clean` stops removing the
# orphan too, because it only removes $(TESTS).
#
# So each fragment CLAIMS the source it compiles, beside its TESTS += line, and
# the two SETS are compared. Claiming is explicit rather than derived from the
# fragment filename because that mapping is not uniform -- 124-circularbuffer.mk
# builds test_circularbuffer.c, with no 124 in it -- and deriving it would be
# the pattern-matching this repo has already had to retract elsewhere (#1032).
#
# $(sort) deduplicates, which is what makes the duplicate case fall out: two
# fragments claiming one source collapse to a single entry, leaving the orphaned
# source unmatched and named in the error.
TEST_SOURCES := $(sort $(wildcard test_*.c))
TEST_CLAIMED := $(sort $(TEST_SOURCES_CLAIMED))
ifneq ($(TEST_CLAIMED),$(TEST_SOURCES))
$(error test sources and fragment claims disagree, so a suite is silently not \
running. On disk: $(TEST_SOURCES). Claimed: $(TEST_CLAIMED). UNCLAIMED: \
$(filter-out $(TEST_CLAIMED),$(TEST_SOURCES)). Claimed but absent: \
$(filter-out $(TEST_SOURCES),$(TEST_CLAIMED)))
endif

# And no binary may be registered twice: `run` would execute it twice and show a
# suite count that looks right while another suite is missing.
ifneq ($(words $(TESTS)),$(words $(sort $(TESTS))))
$(error $$(TESTS) registers a binary more than once, so `make run` would run it \
twice and mask a missing suite. Registered: $(TESTS))
endif

endif   # CLEAN_ONLY

# Every suite, in whatever order $(sort $(wildcard tests/*.mk)) included the
# fragments (lexicographic by filename — see this directory's README.md).
# `make run` builds every binary in $(TESTS) as a prerequisite (a failed
# build, e.g. a tripped guard, fails here before any test runs), then
# executes each in turn, stopping at the first failure the same way a
# literal per-binary recipe line would.
run: $(TESTS)
	@test -n "$(strip $(TESTS))" || { \
	    echo "ERROR: \$$(TESTS) is empty at recipe time -- refusing to report"; \
	    echo "       success for a run that executed nothing."; exit 1; }
	@for t in $(TESTS); do echo "./$$t"; ./$$t || exit 1; done

# The second rm sweeps any binary no fragment claims any more. $(TESTS) is built
# from the fragments, so a dropped fragment's binary is invisible to the first
# one -- and that is the state this target has to be able to clean. `run_*` is
# exactly the suite binaries by convention and is what .gitignore lists; nothing
# else in this directory matches it.
#
# These lines are make comments, not recipe lines. A `#` INSIDE a recipe is
# handed to the shell, so make expands it first and echoes it: the first draft
# put this text in the recipe and `make clean` printed the whole comment with
# $(TESTS) expanded into the middle of a sentence. Caught by reading the output
# of the mutation proof rather than the diff.
clean:
	rm -f $(TESTS) $(CLEAN_EXTRA)
	rm -f run_*

.PHONY: run clean
