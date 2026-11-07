#!/usr/bin/env bash
# #1201 -- mutation harness for StopRequestedSince()'s behavioural test.
#
# WHY THIS EXISTS, AND WHY IT IS A DIFFERENT TOOL FROM THE PIN'S --self-test.
# check_1201_predicate_wiring.py --self-test proves the WIRING pin is effective
# by feeding it synthetic sources. It cannot say anything about
# test_1201_stop_requested_since.c, because that binary compiles the REAL
# predicate spliced out of SCPIInterface.c -- so the only way to prove it
# catches a broken predicate is to break the real one and watch it fail.
#
# ⭐ EACH ARM NAMES THE TEST THAT MUST FAIL, not merely "something failed". A
# mutant that fails for an unrelated reason -- a compile error, a different
# assertion -- would otherwise count as caught while the property it targets is
# unguarded. That is the same trap the Python self-test's `must_mention` arms
# close, and it is the reason the three single-term arms below each assert that
# the OTHER two terms' tests still PASS: if every mutant failed everything, the
# isolation tests would not be isolating anything.
#
# Usage:  bash tests/host/pin1201_mutate.sh [WORKTREE]
set -u

W="${1:-$(git rev-parse --show-toplevel)}"
S="$W/firmware/src/services/SCPI/SCPIInterface.c"
T="$W/tests/host"

[ -f "$S" ] || { echo "FATAL: $S missing."; exit 2; }

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)"
echo "################################################################"

if [ -n "$(git -C "$W" status --porcelain -- "$S" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in $S. Commit or set them aside first --"
    echo "       restore here is a byte copy and cannot arbitrate between your"
    echo "       edits and the mutants'."
    exit 2
fi

# ⛔ BACKUP CHECKED **BEFORE** THE TRAP IS INSTALLED, and verified byte-
# identical. With the trap installed first, a failed backup turns into SOURCE
# DESTRUCTION, because restore runs on every exit path including the clean one.
# Nothing is trapped until the backup is proven good, so an abort above or here
# leaves the tree untouched by definition. (The #1124 harness had this wrong.)
BS=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing mutated and no trap installed; the worktree is"
    echo "       untouched."
    rm -f "$BS"
    exit 2
}
BS=$(mktemp 2>/dev/null) || backup_failed "mktemp failed"
[ -n "$BS" ] && [ -f "$BS" ] || backup_failed "mktemp returned no usable path"
cp "$S" "$BS" || backup_failed "cp of $S failed"
cmp -s "$S" "$BS" || backup_failed "backup of $S differs -- short or partial copy"

restore() { cp "$BS" "$S"; }
cleanup() { restore; rm -f "$BS" "$T/run_1201_tests" "$T/gen_1201_stop_predicate.h"; }
# The signal handlers exit EXPLICITLY: `trap '...' EXIT INT TERM` is NOT
# equivalent, because on INT/TERM bash runs the handler and then RESUMES, so
# the tree is restored and then re-mutated.
trap cleanup EXIT
trap 'printf "\n** INTERRUPTED -- restoring **\n" >&2; exit 130' INT
trap 'printf "\n** TERMINATED -- restoring **\n" >&2; exit 143' TERM

cd "$T" || exit 2
pass=0; fail=0

# sub <literal-find> <literal-replace> -- exit 9 if the anchor is absent.
#
# ⛔ NEWLINE-AGNOSTIC ON PURPOSE. This repo's .c files are CRLF in the working
# tree (.gitattributes mandates it), so a literal written with LF matches
# NOTHING and every arm reports "anchor absent" -- which, worse, can read as
# "the mutant survived" if the caller is not checking. Normalise for MATCHING,
# restore the file's own endings on write.
sub() {
    python3 -c '
import sys
p, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(p, newline="").read()
crlf = "\r\n" in raw
t = raw.replace("\r\n", "\n") if crlf else raw
if t.count(a) != 1:
    sys.exit(9)
t = t.replace(a, b, 1)
if b not in t:
    sys.exit(8)
open(p, "w", newline="").write(t.replace("\n", "\r\n") if crlf else t)' "$S" "$1" "$2"
}

# run_test -- rebuild the binary from the (mutated) source and print a stream of
# "name=OK" / "name=FAIL" lines, or BUILD_FAILED.
run_test() {
    rm -f run_1201_tests gen_1201_stop_predicate.h
    if ! make -s run_1201_tests >/dev/null 2>&1; then
        echo "BUILD_FAILED"; return
    fi
    ./run_1201_tests 2>&1 | awk '
        /^\[ OK \]/   { print $3 "=OK" }
        /^\[FAIL\]/   { print $2 "=FAIL" }'
}

# expect_fail <label> <test-that-must-FAIL> [tests-that-must-still-PASS...]
expect_fail() {
    label="$1"; must_fail="$2"; shift 2
    out=$(run_test)
    if [ "$out" = "BUILD_FAILED" ]; then
        echo "  FAIL $label: the mutant did not COMPILE, so this arm proves"
        echo "         nothing about the test. A build break is not a caught"
        echo "         mutant -- rewrite the mutation to be valid C."
        fail=$((fail+1)); return
    fi
    case "$out" in
        *"$must_fail=FAIL"*) ;;
        *) echo "  FAIL $label: $must_fail did NOT fail. The mutant SURVIVED,"
           echo "         so that test does not actually pin its term."
           echo "         stream: $(echo "$out" | tr '\n' ' ')"
           fail=$((fail+1)); return;;
    esac
    for still in "$@"; do
        case "$out" in
            *"$still=OK"*) ;;
            *) echo "  FAIL $label: $still also failed, but should not have."
               echo "         If every mutant breaks every test, the isolation"
               echo "         tests are not isolating a term."
               echo "         stream: $(echo "$out" | tr '\n' ' ')"
               fail=$((fail+1)); return;;
        esac
    done
    echo "  OK   $label (caught by $must_fail, others unaffected)"
    pass=$((pass+1))
}

expect_clean() {
    label="$1"
    out=$(run_test)
    case "$out" in
        *FAIL*|BUILD_FAILED)
            echo "  FAIL $label: expected a CLEAN run, got: $(echo "$out" | tr '\n' ' ')"
            fail=$((fail+1));;
        *)  echo "  OK   $label (ran, and clean)"; pass=$((pass+1));;
    esac
}

T1=term1_pinned_active_alone_is_a_stop
T2=term2_live_count_alone_is_a_stop
T3=term3_generation_alone_is_a_stop_but_not_in_flight
TTT=the_whole_truth_table
TOUT=the_in_flight_out_param_is_written_on_every_path

echo "== setup: the baseline must be clean, or every result below is noise =="
expect_clean "baseline"
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

INFLIGHT='    const bool inFlight = pinnedActive || (gStreamStopsActive != 0u);'
RETURN='    return inFlight || (gStreamStopGen != pinnedGen);'
WRITE='    *outStopInFlight = inFlight;'

echo "== M1 DROP TERM 1: the pinned-active term, which the other two CANNOT  =="
echo "==    see -- the count is back to zero and the generation was bumped   =="
echo "==    BEFORE the pin, so it compares equal.                            =="
if sub "$INFLIGHT" '    const bool inFlight = (gStreamStopsActive != 0u);'; then
    expect_fail "M1 pinned-active term dropped" "$T1" "$T2" "$T3"
else
    echo "  FAIL M1 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M2 DROP TERM 2: the live count -- a stop running RIGHT NOW.          =="
if sub "$INFLIGHT" '    const bool inFlight = pinnedActive;'; then
    expect_fail "M2 live-count term dropped" "$T2" "$T1" "$T3"
else
    echo "  FAIL M2 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M3 DROP TERM 3: the generation -- a stop that began AND ended inside =="
echo "==    the window, the original #861 case.                              =="
if sub "$RETURN" '    return inFlight;'; then
    expect_fail "M3 generation term dropped" "$T3" "$T1" "$T2"
else
    echo "  FAIL M3 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M4 COLLAPSE THE TWO OUTPUTS: inFlight made equal to requested. Every =="
echo "==    arm would still be REFUSED correctly, and every log message that  =="
echo "==    distinguishes 'still in flight' from 'issued and completed' would =="
echo "==    be wrong. ⭐ The mutant a refusal-only test cannot see.            =="
if sub "$WRITE" '    *outStopInFlight = inFlight || (gStreamStopGen != pinnedGen);'; then
    expect_fail "M4 inFlight collapsed into requested" "$T3" "$T1" "$T2"
else
    echo "  FAIL M4 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M5 WRONG OPERATOR: && for ||, so a stop is reported only when ALL    =="
echo "==    three terms fire at once -- i.e. essentially never.               =="
if sub "$INFLIGHT" '    const bool inFlight = pinnedActive && (gStreamStopsActive != 0u);'; then
    expect_fail "M5 && instead of ||" "$T1" "$T3"
else
    echo "  FAIL M5 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M6 OUT-PARAM LEFT UNWRITTEN on the quiet path: the caller then reads =="
echo "==    whatever it had before, and the firmware's callers reuse one      =="
echo "==    variable across a sweep's many steps, so 'stale' is the realistic =="
echo "==    shape rather than 'uninitialised'.                                =="
if sub "$WRITE" '    if (inFlight) { *outStopInFlight = inFlight; }'; then
    expect_fail "M6 out-param not always written" "$TOUT" "$T1" "$T2"
else
    echo "  FAIL M6 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== M7 NEGATIVE CONTROL: a COMMENT-ONLY change inside the predicate. The =="
echo "==    test must still PASS -- it pins BEHAVIOUR, not text. Without this =="
echo "==    arm, a 'test' that merely hashed the function would look perfect  =="
echo "==    on M1-M6 and fire on every harmless edit.                         =="
if sub "$WRITE" '    /* reworded: publish the refinement the log message uses */
    *outStopInFlight = inFlight;'; then
    expect_clean "M7 comment-only edit still passes"
else
    echo "  FAIL M7 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo "== restored =="
expect_clean "post-restore baseline"

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE TEST IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
    exit 1
fi
exit 0
