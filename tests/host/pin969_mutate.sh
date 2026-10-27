#!/usr/bin/env bash
# #969 / PR #1020 -- mutation harness for the one-critical-section ordering pin.
#
# WHY THIS EXISTS. The pin asserts an ORDER, and an order pin is the shape that
# most easily passes while blind: the four markers can all be PRESENT with the
# property gone. Three mutants separate PRESENT from EFFECTIVE, and the third
# is the one a naive guard misses -- two critical sections, each read guarded,
# the pair still torn.
#
# Usage:  bash tests/host/pin969_mutate.sh [WORKTREE]
#         (WORKTREE defaults to the git tree containing $PWD)
set -u

W="${1:-$(git rev-parse --show-toplevel)}"
S="$W/firmware/src/services/SCPI/SCPIInterface.c"
M="$W/tests/host/Makefile"

for f in "$S" "$M"; do
    [ -f "$f" ] || { echo "FATAL: $f missing."; exit 2; }
done

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)"
echo "################################################################"

if [ -n "$(git -C "$W" status --porcelain -- "$S" "$M" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in the files this mutates. Commit or set"
    echo "       them aside first -- restore here is a byte copy and will not"
    echo "       arbitrate between your edits and the mutants'."
    exit 2
fi

# ⛔ EVERY mktemp AND cp CHECKED **BEFORE** THE TRAP IS INSTALLED, and each
# backup verified byte-identical. The #1124 harness had this wrong: unchecked
# backups with the EXIT trap installed afterwards turn a failed backup into
# SOURCE DESTRUCTION, because restore runs on every exit path including the
# clean one. Nothing is trapped until the backup is proven good, so an abort
# here leaves the tree untouched by definition.
BS=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing mutated and no trap installed; the worktree is"
    echo "       untouched. Fix the backup location and re-run."
    rm -f "$BS"
    exit 2
}
BS=$(mktemp 2>/dev/null) || backup_failed "mktemp failed"
[ -n "$BS" ] && [ -f "$BS" ] || backup_failed "mktemp returned no usable path"
cp "$S" "$BS" || backup_failed "cp of $S failed"
cmp -s "$S" "$BS" || backup_failed "backup of $S differs -- short or partial copy"

restore() { cp "$BS" "$S"; }
cleanup() { restore; rm -f "$BS"; }
# The signal handlers EXIT explicitly: `trap '...' EXIT INT TERM` is NOT
# equivalent, because on INT/TERM bash runs the handler and then RESUMES, so
# the tree is restored and then re-mutated. The EXIT trap -- which `exit`
# itself fires -- is the single place cleanup happens.
trap cleanup EXIT
trap 'printf "\n** INTERRUPTED -- restoring **\n" >&2; exit 130' INT
trap 'printf "\n** TERMINATED -- restoring **\n" >&2; exit 143' TERM

cd "$W/tests/host" || exit 2
pass=0; fail=0

run_guard() {   # prints the guard's first ERROR line, or <<NO ERROR>>
    out=$(make -s run_998_tests 2>&1)
    rc=$?
    line=$(printf '%s\n' "$out" | grep -m1 '^ERROR:')
    if [ "$rc" -eq 0 ]; then printf '<<NO ERROR>>'; else
        printf '%s' "${line:-<<FAILED, no ERROR: line>>}"
    fi
}

expect() {      # expect <label> <PATTERN|CLEAN>
    label="$1"; want="$2"
    got=$(run_guard)
    if [ "$want" = "CLEAN" ]; then
        if [ "$got" = "<<NO ERROR>>" ]; then
            echo "  OK   $label (ran, and clean)"; pass=$((pass+1))
        else
            echo "  FAIL $label expected CLEAN, got: $got"; fail=$((fail+1))
        fi
        return
    fi
    # A pattern that could match anything is worse than no check.
    case "$want" in ""|" "*|*" ") echo "  FAIL $label: unsafe pattern"; fail=$((fail+1)); return;; esac
    case "$got" in
        *"$want"*) echo "  OK   $label (this pin's own diagnostic)"; pass=$((pass+1));;
        *) echo "  FAIL $label did NOT come from this pin."
           echo "         wanted substring: $want"
           echo "         got             : $got"; fail=$((fail+1));;
    esac
}

sub() {         # sub <file> <literal-find> <literal-replace>  (exit 9 if absent)
    # ⛔ NEWLINE-AGNOSTIC ON PURPOSE. This repo's .c files are CRLF in the
    # working tree, so a literal written with LF newlines matches NOTHING and
    # every arm reports "anchor absent" -- which is what happened on the first
    # run of this harness. Normalise for MATCHING, then restore the file's own
    # endings on write, so the literals below stay readable and the file is
    # byte-faithful apart from the mutation.
    python3 -c '
import sys
p, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(p, newline="").read()
crlf = "\r\n" in raw
t = raw.replace("\r\n", "\n") if crlf else raw
if a not in t:
    sys.exit(9)
t = t.replace(a, b, 1)
if crlf:
    t = t.replace("\n", "\r\n")
open(p, "w", newline="").write(t)' "$1" "$2" "$3"
}

PINNED='    uint32_t stopGenPinned;
    bool stopActivePinned;
    taskENTER_CRITICAL();
    stopGenPinned = gStreamStopGen;
    stopActivePinned = (gStreamStopsActive != 0u);
    taskEXIT_CRITICAL();'

echo "== setup: the baseline must be clean, or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

echo "== M1 REVERT: back to two bare loads, the pre-#969 shape.             =="
if sub "$S" "$PINNED" '    const uint32_t stopGenPinned = gStreamStopGen;
    const bool stopActivePinned = (gStreamStopsActive != 0u);'; then
    expect "M1 revert to bare loads" "no longer taken under ONE critical"
else
    echo "  FAIL M1 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== M2 ORDER: one read moved OUT of the section. Both markers present,  =="
echo "==           a section present, and the pair still torn.               =="
if sub "$S" "$PINNED" '    uint32_t stopGenPinned;
    bool stopActivePinned;
    stopGenPinned = gStreamStopGen;
    taskENTER_CRITICAL();
    stopActivePinned = (gStreamStopsActive != 0u);
    taskEXIT_CRITICAL();'; then
    expect "M2 one read outside" "no longer taken under ONE critical"
else
    echo "  FAIL M2 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== M3 SPLIT: TWO critical sections. ⭐ THE MUTANT A NAIVE GUARD MISSES  =="
echo "==           -- each read IS guarded, so a per-read check passes, and   =="
echo "==           the pair no longer describes one instant.                  =="
if sub "$S" "$PINNED" '    uint32_t stopGenPinned;
    bool stopActivePinned;
    taskENTER_CRITICAL();
    stopGenPinned = gStreamStopGen;
    taskEXIT_CRITICAL();
    taskENTER_CRITICAL();
    stopActivePinned = (gStreamStopsActive != 0u);
    taskEXIT_CRITICAL();'; then
    expect "M3 split into two sections" "no longer taken under ONE critical"
else
    echo "  FAIL M3 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== M4 NEGATIVE CONTROL: the fix REVERTED, but a COMMENT naming all four =="
echo "==     markers in the right order. The guard must still FAIL -- a pin   =="
echo "==     satisfiable by prose is the failure mode this file has hit most. =="
if sub "$S" "$PINNED" '    /* taskENTER_CRITICAL(); gStreamStopGen; gStreamStopsActive;
     * taskEXIT_CRITICAL() -- this comment names the whole run in order. */
    const uint32_t stopGenPinned = gStreamStopGen;
    const bool stopActivePinned = (gStreamStopsActive != 0u);'; then
    expect "M4 prose does not satisfy the pin" "no longer taken under ONE critical"
else
    echo "  FAIL M4 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== restored =="
expect "post-restore baseline" CLEAN

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE PIN IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
    exit 1
fi
exit 0
