#!/usr/bin/env bash
# #1084 / #1092 -- mutation harness for the button-power-off guards.
#
# WHY THIS EXISTS. test_1092_button_poweroff_latch.c is a MODEL: PowerApi.c is
# not host-includable, so the model's premises are pinned against the real
# source in $(BUTTONLATCH_BIN)'s recipe. A model whose pins cannot fire is a
# model that agrees with itself, so every pin gets a mutant here -- including
# the two pins whose only job is to protect the model's own CORRECTION (that
# the request clear is not reachable as the auto-promote defect, because both
# posters set the suppression latch).
#
# Usage:  bash tests/host/pin1092_mutate.sh [WORKTREE]
set -u

W="${1:-$(git rev-parse --show-toplevel)}"
P="$W/firmware/src/HAL/Power/PowerApi.c"
U="$W/firmware/src/HAL/UI/UI.c"
S="$W/firmware/src/services/SCPI/SCPIInterface.c"

for f in "$P" "$U" "$S"; do
    [ -f "$f" ] || { echo "FATAL: $f missing."; exit 2; }
done

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)"
echo "################################################################"

if [ -n "$(git -C "$W" status --porcelain -- "$P" "$U" "$S" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in the files this mutates. Commit or set"
    echo "       them aside first -- restore here is a byte copy and cannot"
    echo "       arbitrate between your edits and the mutants'."
    exit 2
fi

# Every mktemp and cp CHECKED, and each backup verified byte-identical, BEFORE
# the EXIT trap is installed. Unchecked backups plus a restore that runs on
# every exit path turn a failed backup into source destruction.
BP=""; BU=""; BS=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing mutated and no trap installed; the worktree is"
    echo "       untouched."
    rm -f "$BP" "$BU" "$BS"
    exit 2
}
for v in BP BU BS; do
    tmp=$(mktemp 2>/dev/null) || backup_failed "mktemp failed for $v"
    [ -n "$tmp" ] && [ -f "$tmp" ] || backup_failed "mktemp gave no usable path for $v"
    eval "$v=\$tmp"
done
back_up_one() {
    cp "$1" "$2" || backup_failed "cp of $1 failed"
    cmp -s "$1" "$2" || backup_failed "backup of $1 differs -- short copy"
}
back_up_one "$P" "$BP"
back_up_one "$U" "$BU"
back_up_one "$S" "$BS"

restore() { cp "$BP" "$P"; cp "$BU" "$U"; cp "$BS" "$S"; }
cleanup() { restore; rm -f "$BP" "$BU" "$BS"; }
# The signal handlers exit EXPLICITLY: on INT/TERM bash runs the handler and
# then RESUMES, so a plain `trap ... EXIT INT TERM` restores and re-mutates.
trap cleanup EXIT
trap 'printf "\n** INTERRUPTED -- restoring **\n" >&2; exit 130' INT
trap 'printf "\n** TERMINATED -- restoring **\n" >&2; exit 143' TERM

cd "$W/tests/host" || exit 2
pass=0; fail=0

run_guard() {
    out=$(make -s run_1092_tests 2>&1); rc=$?
    line=$(printf '%s\n' "$out" | grep -m1 '^ERROR:')
    if [ "$rc" -eq 0 ]; then printf '<<NO ERROR>>'; else
        printf '%s' "${line:-<<FAILED, no ERROR: line>>}"
    fi
}

expect() {
    label="$1"; want="$2"
    rm -f run_1092_tests
    got=$(run_guard)
    if [ "$want" = "CLEAN" ]; then
        if [ "$got" = "<<NO ERROR>>" ]; then
            echo "  OK   $label (ran, and clean)"; pass=$((pass+1))
        else
            echo "  FAIL $label expected CLEAN, got: $got"; fail=$((fail+1))
        fi
        return
    fi
    case "$want" in ""|" "*|*" ") echo "  FAIL $label: unsafe pattern"; fail=$((fail+1)); return;; esac
    case "$got" in
        *"$want"*) echo "  OK   $label (this pin's own diagnostic)"; pass=$((pass+1));;
        *) echo "  FAIL $label did NOT come from this pin."
           echo "         wanted: $want"
           echo "         got   : $got"; fail=$((fail+1));;
    esac
}

# Newline-agnostic for MATCHING, byte-faithful on WRITE: these .c files are
# CRLF in the working tree and the literals below are LF.
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

echo "== setup: baseline must be clean or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline not clean; mutants meaningless."; exit 1
fi

echo "== M1: restore the clear in the common DO_POWER_DOWN handler.           =="
if sub "$P" '    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;' \
          '    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;
        pData->requestedPowerState = NO_CHANGE;'; then
    expect "M1 early clear restored" "handler clears"
else
    echo "  FAIL M1 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M2: remove the clear from Power_Down(), the EXECUTOR. With M1's early =="
echo "==     clear forbidden this is the only one left, so the request would   =="
echo "==     stay DO_POWER_DOWN forever.                                      =="
if sub "$P" '    pData->powerState = STANDBY;
    pData->requestedPowerState = NO_CHANGE;
}' '    pData->powerState = STANDBY;
}'; then
    expect "M2 executor clear removed" "Power_Down() no longer clears"
else
    echo "  FAIL M2 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M3: drop the latch term from the auto-promote gate. ⭐ THIS PIN GUARDS =="
echo "==     THE MODEL'S OWN CORRECTION -- without that term the clear WOULD   =="
echo "==     be the root cause and test_1092's DEFECT A section is wrong.      =="
if sub "$P" '        if (vbusPresent && !pData->autoPromotedThisVbusSession) {' \
            '        if (vbusPresent) {'; then
    expect "M3 latch term in the gate" "no longer consults the"
else
    echo "  FAIL M3 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M4: drop the VBUS-absent re-arm. ⭐ ALSO GUARDS AN ARGUMENT, not just  =="
echo "==     code: the re-arm is the ONLY reason setting the latch             =="
echo "==     unconditionally is harmless on battery.                           =="
if sub "$P" '    if (!pData->BQ24297Data.status.pgStat) {
        pData->autoPromotedThisVbusSession = false;
    }' \
            '    /* mutant: re-arm removed entirely */'; then
    expect "M4 standby re-arm" "no longer re-arms the latch"
else
    echo "  FAIL M4 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M5: re-gate the button latch on the cached pgStat -- the r2 residual.  =="
if sub "$U" '                    gpPowerData->autoPromotedThisVbusSession = true;' \
            '                    if (gpPowerData->BQ24297Data.status.pgStat) {
                        gpPowerData->autoPromotedThisVbusSession = true;
                    }'; then
    expect "M5 latch re-gated on pgStat" "gates the latch on the cached pgStat"
else
    echo "  FAIL M5 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M6: assign pgStat to the latch -- the r1 defect, which also writes     =="
echo "==     FALSE and cancels a valid suppression.                             =="
if sub "$U" '                    gpPowerData->autoPromotedThisVbusSession = true;' \
            '                    gpPowerData->autoPromotedThisVbusSession = gpPowerData->BQ24297Data.status.pgStat;'; then
    expect "M6 non-constant latch write" "writes a NON-CONSTANT"
else
    echo "  FAIL M6 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M7: remove the latch set from SYSTem:POWer:STATe 0, the OTHER poster.  =="
if sub "$S" '            pPowerData->autoPromotedThisVbusSession = true;' \
            '            /* mutant: latch set removed */'; then
    expect "M7 the second poster's latch" "no longer sets the AUTOOn"
else
    echo "  FAIL M7 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M8 NEGATIVE CONTROL: the fix intact, plus a COMMENT naming every       =="
echo "==     forbidden shape. The guards must stay CLEAN -- they are            =="
echo "==     comment-stripped precisely because the prose that explains this    =="
echo "==     fix names all of these tokens.                                     =="
if sub "$U" '                    gpPowerData->autoPromotedThisVbusSession = true;' \
            '                    /* if (gpPowerData->BQ24297Data.status.pgStat) {
                     *     gpPowerData->autoPromotedThisVbusSession =
                     *         gpPowerData->BQ24297Data.status.pgStat;
                     * } -- every forbidden shape, named in prose. */
                    gpPowerData->autoPromotedThisVbusSession = true;'; then
    expect "M8 prose does not trip the guards" CLEAN
else
    echo "  FAIL M8 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M9: put the re-arm back INSIDE the request gate -- the exact r3 shape. =="
echo "==     THIS IS THE ONE PIN 3 COULD NOT SEE: the = false write is still =="
echo "==     and still a single occurrence, so only its POSITION differs.       =="
if sub "$P" '    if (!pData->BQ24297Data.status.pgStat) {
        pData->autoPromotedThisVbusSession = false;
    }
    if (pData->autoPowerOnUsb && pData->requestedPowerState == NO_CHANGE) {
        bool vbusPresent = pData->BQ24297Data.status.pgStat;
        if (vbusPresent && !pData->autoPromotedThisVbusSession) {' \
          '    if (pData->autoPowerOnUsb && pData->requestedPowerState == NO_CHANGE) {
        bool vbusPresent = pData->BQ24297Data.status.pgStat;
        if (!vbusPresent) {
            pData->autoPromotedThisVbusSession = false;
        } else if (!pData->autoPromotedThisVbusSession) {'; then
    expect "M9 re-arm hoisted out of the gate" "is INSIDE the auto-promote"
else
    echo "  FAIL M9 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M10: drop the ENTRY VBUS refresh, leaving only the pre-power-up one.   =="
echo "==      The re-arm then decides on a CACHED pgStat, where a stale FALSE   =="
echo "==      and a truthful FALSE are indistinguishable.                       =="
if sub "$P" '    Power_RefreshVbusFromPhy();

    /* #454: Auto-power-up on VBUS-present.' \
          '    /* mutant: entry refresh removed */

    /* #454: Auto-power-up on VBUS-present.'; then
    expect "M10 entry refresh present and first" "entry refresh"
else
    echo "  FAIL M10 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M11: ADD a second re-arm inside the gate, keeping the hoisted one. An  =="
echo "==      order check reading only the FIRST match would pass this; the     =="
echo "==      exactly-one count is what catches it.                             =="
if sub "$P" '        if (vbusPresent && !pData->autoPromotedThisVbusSession) {' \
          '        if (!vbusPresent) {
            pData->autoPromotedThisVbusSession = false;
        }
        if (vbusPresent && !pData->autoPromotedThisVbusSession) {'; then
    expect "M11 exactly one re-arm" "expected exactly ONE re-arm"
else
    echo "  FAIL M11 anchor absent."; fail=$((fail+1))
fi
restore

echo "== M12: add the FULL status refresh back at the handler entry, KEEPING   =="
echo "==      the PHY-only one. PIN 6 still passes (the entry refresh is       =="
echo "==      present and correctly ordered), so only PIN 7 can catch this --  =="
echo "==      which is the point: the hazard is the CALLEE, not the position.   =="
echo "==      The renamed-wrapper variant is covered by the pin's own          =="
echo "==      self-test, which cannot be expressed as a one-line sed here.     =="
if sub "$P" 'CALLEES transitively, so renaming a wrapper cannot defeat it. */
    Power_RefreshVbusFromPhy();' \
          'CALLEES transitively, so renaming a wrapper cannot defeat it. */
    Power_RefreshVbusFromPhy();
    Power_UpdateStatusFromGPIO();'; then
    expect "M12 no blocking call before Power_Down" "transitively reaches a blocking sink"
else
    echo "  FAIL M12 anchor absent."; fail=$((fail+1))
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
