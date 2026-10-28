#!/usr/bin/env bash
# #1060 / #1078 -- mutation harness for the AP->STA teardown guards.
#
# WHY THIS EXISTS. test_1060_apsta_disconnect_retry.c is a MODEL: wifi_manager.c
# is not host-includable, so the model's premises are pinned against the real
# source in $(APSTA1060_BIN)'s recipe. A pin that cannot fire is a pin that
# agrees with itself, and this row has now produced three separate instances of
# exactly that:
#
#   - PIN 5 fired TWICE on CORRECT code, because its key spanned a
#     string-literal continuation and `cc -fpreprocessed -E` preserves geometry;
#   - PIN 8's first version fired on correct code too, on the LEGITIMATE
#     STA_CONNECTED writer, because a file-wide negative pin cannot express a
#     positional property;
#   - PIN 10's first version reported all four writes unprotected -- including
#     ones just wrapped -- because a depth counter is the wrong model for a file
#     whose critical sections have multiple exit paths (27 ENTER vs 31 EXIT).
#
# So every pin r3 and r4 added gets a mutant here. The pins from r1/r2 were
# verified by differential against origin/main when written; these five had no
# such baseline, which is the gap this closes.
#
# Usage:  bash tests/host/pin1060_mutate.sh [WORKTREE]
set -u

W="${1:-$(git rev-parse --show-toplevel)}"
F="$W/firmware/src/services/wifi_services/wifi_manager.c"

[ -f "$F" ] || { echo "FATAL: $F missing."; exit 2; }

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)"
echo "################################################################"

if [ -n "$(git -C "$W" status --porcelain -- "$F" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in $F. Commit or set them aside --"
    echo "       restore here is a byte copy and cannot arbitrate between"
    echo "       your edits and the mutants'."
    exit 2
fi

# The backup is checked and verified byte-identical BEFORE the EXIT trap is
# installed. An unchecked backup plus a restore that runs on every exit path
# turns a failed backup into source destruction.
BK=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing mutated and no trap installed; the worktree is"
    echo "       untouched."
    [ -n "$BK" ] && rm -f "$BK"
    exit 2
}
BK=$(mktemp 2>/dev/null) || backup_failed "mktemp failed"
[ -n "$BK" ] && [ -f "$BK" ] || backup_failed "mktemp gave no usable path"
cp "$F" "$BK" || backup_failed "cp of $F failed"
cmp -s "$F" "$BK" || backup_failed "backup differs -- short copy"

restore() { cp "$BK" "$F"; }
cleanup() { restore; rm -f "$BK"; }
# The signal handlers exit EXPLICITLY: on INT/TERM bash runs the handler and
# then RESUMES, so a plain `trap ... EXIT INT TERM` restores and re-mutates.
trap cleanup EXIT
trap 'printf "\n** INTERRUPTED -- restoring **\n" >&2; exit 130' INT
trap 'printf "\n** TERMINATED -- restoring **\n" >&2; exit 143' TERM

cd "$W/tests/host" || exit 2
pass=0; fail=0

first_error() {
    local out rc line
    out=$(make -s run_1060_tests 2>&1); rc=$?
    line=$(printf '%s\n' "$out" | grep -m1 '^ERROR:')
    if [ "$rc" -eq 0 ]; then printf '<<NO ERROR>>'
    else printf '%s' "${line:-<<FAILED, no ERROR: line>>}"; fi
}

expect() { # expect <label> <PATTERN|CLEAN>
    local label="$1" want="$2" got
    rm -f run_1060_tests
    got="$(first_error)"
    if [ "$want" = CLEAN ]; then
        if [ "$got" = "<<NO ERROR>>" ]; then
            echo "  OK   $label (ran, and clean)"; pass=$((pass+1))
        else
            echo "  FAIL $label expected a clean RUN, got: $got"; fail=$((fail+1))
        fi
        return
    fi
    case "$want" in ""|" "*|*" ") echo "  FAIL $label: unsafe pattern"; fail=$((fail+1)); return;; esac
    if printf '%s' "$got" | grep -qF -- "$want"; then
        echo "  OK   $label (this pin's own diagnostic)"; pass=$((pass+1))
    else
        echo "  FAIL $label did NOT come from this pin."
        echo "         wanted substring: $want"
        echo "         got:              $got"
        fail=$((fail+1))
    fi
}

# ⚠ expect() matches the FIRST `^ERROR:` line ONLY. These pins print multi-line
# diagnostics, so key `want` on words from the diagnostic's FIRST line.
#
# Newline-agnostic for MATCHING, byte-faithful on WRITE: this .c file is CRLF in
# the working tree and the literals below are LF.
sub() {
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

echo "== setup: the baseline must be clean, or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

echo "== M1 (PIN 6): drop the REINIT-side re-attempt. The bounded retry in the =="
echo "==            AP->STA branch survives, so every other pin passes -- only  =="
echo "==            PIN 6 sees that a fault outlasting the window is permanent. =="
if sub "$F" '            if (GetEventFlagStatus(pInstance->eventFlags,
                                   WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING)) {' \
            '            if (0) {'; then
    expect "M1 REINIT-side re-attempt" "no REINIT-side re-attempt"
else
    echo "  FAIL M1 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M2 (PIN 7): never SET the marker ANYWHERE, so the re-attempt can      =="
echo "==            never run. ⚠ BOTH set sites must go: PIN 7 is a PRESENCE    =="
echo "==            check, and the ENTRY restore is also a set -- removing only =="
echo "==            the exhaustion site leaves PIN 7 satisfied and lets PIN     =="
echo "==            10's COUNT answer instead. Measured, not assumed.          =="
if sub "$F" '                            SetEventFlag(&pInstance->eventFlags,
                                    WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);' \
            '                            (void)0;' \
   && sub "$F" '                SetEventFlag(&pInstance->eventFlags,
                             WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);' \
               '                (void)0;'; then
    expect "M2 marker is set somewhere" "nothing SETS the #1060 teardown"
else
    echo "  FAIL M2 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M3 (PIN 8): re-set STA_CONNECTED to buy the retry back -- the tempting =="
echo "==            fix, which reinstates the stale link-state report. A COUNT, =="
echo "==            because the association handler is a LEGITIMATE writer.     =="
if sub "$F" '            if (teardownPending) {
                taskENTER_CRITICAL();' \
            '            if (teardownPending) {
                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
                taskENTER_CRITICAL();'; then
    expect "M3 exactly one STA_CONNECTED writer" "writer(s) of STA_CONNECTED, expected exactly 1"
else
    echo "  FAIL M3 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M4 (PIN 9): stop preserving the marker across MainState ENTRY -- the   =="
echo "==            r3 HIGH. ⭐ EVERY OTHER PIN STILL PASSES: the flag is still  =="
echo "==            set, cleared and re-attempted; it is simply erased by the   =="
echo "==            memset one transition later, which is why r3 shipped.       =="
if sub "$F" '            bool teardownPending = GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);' \
            '            bool teardownPending = false;'; then
    expect "M4 marker survives ENTRY" "does not preserve the #1060 teardown"
else
    echo "  FAIL M4 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M5 (PIN 10): strip the critical section from the REINIT-site write.    =="
echo "==             The write is still present and correct in isolation; only  =="
echo "==             the nearest-marker check sees the unprotected RMW.         =="
if sub "$F" '                    taskENTER_CRITICAL();
                    ResetEventFlag(&pInstance->eventFlags,
                            WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);
                    taskEXIT_CRITICAL();' \
            '                    ResetEventFlag(&pInstance->eventFlags,
                            WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);'; then
    expect "M5 every marker write is locked" "are OUTSIDE"
else
    echo "  FAIL M5 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M6 (PIN 10, the COUNT arm): delete one marker write entirely. The      =="
echo "==             remaining three are all locked, so the lock arm is clean;  =="
echo "==             only the count notices the pin is no longer looking at the =="
echo "==             writes it was written for.                                 =="
if sub "$F" '                            ResetEventFlag(&pInstance->eventFlags,
                                    WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING);
                        } else {' \
            '                        } else {'; then
    expect "M6 marker write count" "write(s) of the #1060 teardown marker"
else
    echo "  FAIL M6 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M7 NEGATIVE CONTROL: the fix intact, plus a COMMENT naming every       =="
echo "==     forbidden shape. The guards must stay CLEAN -- they read           =="
echo "==     comment-stripped output precisely because the prose explaining     =="
echo "==     this fix names all of these tokens.                                =="
if sub "$F" '            if (teardownPending) {
                taskENTER_CRITICAL();' \
            '            /* Forbidden shapes, named in prose:
             *   SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
             *   bool teardownPending = false;
             * and a marker write with no taskENTER_CRITICAL around it. */
            if (teardownPending) {
                taskENTER_CRITICAL();'; then
    expect "M7 prose does not trip the guards" CLEAN
else
    echo "  FAIL M7 anchor absent."; fail=$((fail+1))
fi
restore

echo "== restored =="
expect "post-restore baseline" CLEAN

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE PINS ARE PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
    exit 1
fi
exit 0
