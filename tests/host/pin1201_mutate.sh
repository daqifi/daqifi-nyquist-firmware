#!/usr/bin/env bash
# #1201 -- mutation harness for the StopRequestedSince() gate.
#
# WHY THIS EXISTS, AND WHY IT IS A DIFFERENT TOOL FROM THE PIN'S --self-test.
# check_1201_predicate_wiring.py --self-test proves the WIRING pin is effective
# by feeding it synthetic sources. It can say nothing about
# test_1201_stop_requested_since.c, because that binary compiles the REAL
# predicate spliced out of SCPIInterface.c -- so the only way to prove it
# catches a broken predicate is to break the real one and watch it fail.
#
# ⭐ WHAT THE FIRST VERSION OF THIS HARNESS GOT WRONG, recorded because the
# lesson is the point. It asserted, for every mutant, that the OTHER tests
# still passed -- and it parsed the pass lines with awk `$3`, which on
# `[ OK ] name` is the closing bracket, not the name. So the "others still
# pass" check compared against `]=OK` and could never match, and SIX arms
# reported the test ineffective when the test was fine. The instrument was
# broken, not the subject. Two consequences are now built in:
#   - the parser's field choice is asserted by a SELF-CHECK below, before any
#     mutant runs, so a mis-parse fails loudly as itself instead of as a
#     verdict about the firmware;
#   - "others still pass" is asserted ONLY where isolation is actually the
#     claim (M1). For a wrong operator or an unwritten out-param, breaking
#     several tests is CORRECT, and demanding otherwise was the harness
#     inventing a requirement the test never made.
#
# ⭐ AND WHAT IT REVEALED, which is now the harness's structure: the two
# instruments catch different mutants, and one arm proves neither is redundant.
#   - M2/M3 drop a GLOBAL READ. The wiring pin catches those before the binary
#     is even built, because it requires the predicate to read both globals.
#   - M1 drops the pinnedActive PARAMETER. The pin cannot see that -- it is not
#     a global -- so the behavioural test is the ONLY thing that catches it.
#     That arm is why the behavioural test has to exist alongside the pin.
#   - M4/M5/M6 keep both reads and break the logic. The pin passes them; the
#     behavioural test catches them.
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
# identical. With the trap installed first, a failed backup becomes SOURCE
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

# sub <literal-find> <literal-replace> -- exit non-zero if the anchor is not
# unique or the replacement did not land.
#
# ⛔ NEWLINE-AGNOSTIC ON PURPOSE. This repo's .c files are CRLF in the working
# tree (.gitattributes mandates it), so a literal written with LF matches
# NOTHING. Worse than failing: an un-applied mutant that nobody checks for
# reads as "the mutant SURVIVED", i.e. as a finding against the test. Normalise
# for MATCHING, restore the file's own endings on write, and verify the
# replacement is present before returning success.
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

# parse_results -- turn the test binary's output into `name=OK` / `name=FAIL`.
# `[ OK ] name` puts the name in $4 (the brackets are separate fields);
# `[FAIL] name` puts it in $2. Getting this wrong is the bug in the header.
parse_results() {
    awk '/^\[ OK \]/ { print $4 "=OK" }
         /^\[FAIL\]/ { print $2 "=FAIL" }'
}

echo "== self-check: the RESULT PARSER, before it is used to judge anything. =="
_probe=$(printf '[ OK ] alpha_case\n[FAIL] beta_case\n' | parse_results | tr '\n' ' ')
if [ "$_probe" != "alpha_case=OK beta_case=FAIL " ]; then
    echo "FATAL: the result parser is broken -- got '$_probe'."
    echo "       Every verdict below would be about the parser rather than"
    echo "       about the firmware. This check exists because exactly that"
    echo "       happened on this harness's first run."
    exit 1
fi
echo "  OK   parser maps both line shapes to their test names"

# run_gate -- rebuild from the (mutated) source. Three distinguishable
# outcomes, because they mean different things:
#   PIN_REJECTED:<text>  the wiring pin refused; the binary was never built
#   BUILD_FAILED         the mutant is not valid C -- a HARNESS defect
#   <result stream>      the binary ran
run_gate() {
    rm -f run_1201_tests gen_1201_stop_predicate.h
    out=$(make run_1201_tests 2>&1)
    if [ $? -ne 0 ]; then
        if printf '%s' "$out" | grep -q "predicate wiring is broken"; then
            printf 'PIN_REJECTED:%s' "$(printf '%s' "$out" | grep -m1 '^  - ')"
        else
            echo "BUILD_FAILED"
        fi
        return
    fi
    ./run_1201_tests 2>&1 | parse_results
}

# expect_test_catch <label> <test-that-must-FAIL> [tests-that-must-still-PASS...]
expect_test_catch() {
    label="$1"; must_fail="$2"; shift 2
    out=$(run_gate)
    case "$out" in
        BUILD_FAILED)
            echo "  FAIL $label: the mutant is not valid C, so this arm proves"
            echo "         nothing. A build break is not a caught mutant."
            fail=$((fail+1)); return;;
        PIN_REJECTED:*)
            echo "  FAIL $label: the WIRING PIN rejected this, so the"
            echo "         behavioural test never ran and this arm says nothing"
            echo "         about it. Use expect_pin_catch for pin-caught"
            echo "         mutants. ${out#PIN_REJECTED:}"
            fail=$((fail+1)); return;;
    esac
    case "$out" in
        *"$must_fail=FAIL"*) ;;
        *) echo "  FAIL $label: $must_fail did NOT fail -- the mutant SURVIVED,"
           echo "         so that test does not pin what it claims to."
           echo "         stream: $(echo "$out" | tr '\n' ' ')"
           fail=$((fail+1)); return;;
    esac
    for still in "$@"; do
        case "$out" in
            *"$still=OK"*) ;;
            *) echo "  FAIL $label: $still also failed, but should not have --"
               echo "         this arm's claim is that the term is ISOLATED."
               echo "         stream: $(echo "$out" | tr '\n' ' ')"
               fail=$((fail+1)); return;;
        esac
    done
    if [ "$#" -gt 0 ]; then
        echo "  OK   $label (caught by $must_fail; $# other term test(s) unaffected)"
    else
        echo "  OK   $label (caught by $must_fail)"
    fi
    pass=$((pass+1))
}

# expect_pin_catch <label> <substring the pin's finding must contain>
expect_pin_catch() {
    label="$1"; want="$2"
    out=$(run_gate)
    case "$out" in
        PIN_REJECTED*"$want"*)
            echo "  OK   $label (caught by the wiring pin, before the build)"
            pass=$((pass+1));;
        PIN_REJECTED:*)
            echo "  FAIL $label: the pin rejected it, but for another reason."
            echo "         wanted substring: $want"
            echo "         got: ${out#PIN_REJECTED:}"
            fail=$((fail+1));;
        *)  echo "  FAIL $label: the wiring pin did NOT reject this."
            echo "         stream: $(echo "$out" | tr '\n' ' ')"
            fail=$((fail+1));;
    esac
}

expect_clean() {
    label="$1"
    out=$(run_gate)
    case "$out" in
        *FAIL*|BUILD_FAILED|PIN_REJECTED*)
            echo "  FAIL $label: expected a CLEAN run, got: $(echo "$out" | tr '\n' ' ')"
            fail=$((fail+1));;
        *)  echo "  OK   $label (ran, and clean)"; pass=$((pass+1));;
    esac
}

T1=term1_pinned_active_alone_is_a_stop
T2=term2_live_count_alone_is_a_stop
T3=term3_generation_alone_is_a_stop_but_not_in_flight
TOUT=the_in_flight_out_param_is_written_on_every_path

echo "== setup: the baseline must be clean, or every result below is noise  =="
expect_clean "baseline"
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

INFLIGHT='    const bool inFlight = pinnedActive || (gStreamStopsActive != 0u);'
RETURN='    return inFlight || (gStreamStopGen != pinnedGen);'
WRITE='    *outStopInFlight = inFlight;'

echo
echo "== M1 ⭐ DROP TERM 1, the pinned-active term -- THE ARM THAT PROVES THE  =="
echo "==    BEHAVIOURAL TEST IS NOT REDUNDANT WITH THE WIRING PIN. pinnedActive=="
echo "==    is a PARAMETER, not a global, so the pin cannot see it go. And it  =="
echo "==    is the term the other two provably cannot cover: the live count is =="
echo "==    back to zero and the generation was bumped BEFORE the pin, so it   =="
echo "==    compares equal. Only running the code catches this.                =="
if sub "$INFLIGHT" '    const bool inFlight = (gStreamStopsActive != 0u);'; then
    expect_test_catch "M1 pinned-active term dropped" "$T1" "$T2" "$T3"
else
    echo "  FAIL M1 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M2 DROP TERM 2, the live count -- a stop running RIGHT NOW. This one  =="
echo "==    removes a GLOBAL READ, so the wiring pin stops it before the build.=="
if sub "$INFLIGHT" '    const bool inFlight = pinnedActive;'; then
    expect_pin_catch "M2 live-count term dropped" "does not read gStreamStopsActive"
else
    echo "  FAIL M2 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M3 DROP TERM 3, the generation -- a stop that began AND ended inside  =="
echo "==    the window, the original #861 case. Also a global read, also the   =="
echo "==    pin's catch.                                                      =="
if sub "$RETURN" '    return inFlight;'; then
    expect_pin_catch "M3 generation term dropped" "does not read gStreamStopGen"
else
    echo "  FAIL M3 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M4 COLLAPSE THE TWO OUTPUTS: inFlight made equal to requested. Every  =="
echo "==    arm is still REFUSED correctly, so no refusal test notices -- and  =="
echo "==    every log message distinguishing 'still in flight' from 'issued    =="
echo "==    and completed' is now wrong. Both globals are still read, so the   =="
echo "==    pin passes it. ⭐ The mutant only a behavioural test sees.          =="
echo "==    No isolation claim here: two tests check that pairing, and both    =="
echo "==    SHOULD fail.                                                       =="
if sub "$WRITE" '    *outStopInFlight = inFlight || (gStreamStopGen != pinnedGen);'; then
    expect_test_catch "M4 inFlight collapsed into requested" "$T3"
else
    echo "  FAIL M4 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M5 WRONG OPERATOR: && for ||, so a stop is reported only when BOTH    =="
echo "==    in-flight terms fire at once. A broad break, and correctly so --   =="
echo "==    no isolation claim.                                                =="
if sub "$INFLIGHT" '    const bool inFlight = pinnedActive && (gStreamStopsActive != 0u);'; then
    expect_test_catch "M5 && instead of ||" "$T1"
else
    echo "  FAIL M5 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M6 OUT-PARAM LEFT UNWRITTEN on the quiet path: the caller then reads   =="
echo "==    whatever it held before. The firmware's callers declare the         =="
echo "==    variable once and reuse it across a sweep's many steps, so 'stale'  =="
echo "==    is the realistic shape, not 'uninitialised'.                        =="
if sub "$WRITE" '    if (inFlight) { *outStopInFlight = inFlight; }'; then
    expect_test_catch "M6 out-param not always written" "$TOUT"
else
    echo "  FAIL M6 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== M7 NEGATIVE CONTROL: a COMMENT-ONLY change inside the predicate. The  =="
echo "==    gate must still PASS -- it pins BEHAVIOUR, not text. Without this  =="
echo "==    arm, a 'test' that merely hashed the function would look perfect   =="
echo "==    on M1-M6 and fire on every harmless edit.                          =="
if sub "$WRITE" '    /* reworded: publish the refinement the log message uses */
    *outStopInFlight = inFlight;'; then
    expect_clean "M7 comment-only edit still passes"
else
    echo "  FAIL M7 anchor absent or mutation did not land -- source drifted."; fail=$((fail+1))
fi
restore

echo
echo "== restored =="
expect_clean "post-restore baseline"

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE GATE IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
    exit 1
fi
exit 0
