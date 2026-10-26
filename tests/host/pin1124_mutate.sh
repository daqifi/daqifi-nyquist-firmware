#!/usr/bin/env bash
# #1124 -- mutation harness for the path-canonicalisation pin in this Makefile.
#
# WHY THIS EXISTS. The pin is not one check but several arms, and a conflict
# resolution that UNIONS two versions of this Makefile can drop an arm while
# every surviving arm still passes on clean source. `make run` then goes green
# and the loss is invisible to the only check anyone runs after a merge.
# PRESENT and EFFECTIVE are different properties, and only a mutant separates
# them. Run this after resolving any conflict in tests/host/Makefile.
#
# Usage:  bash tests/host/pin1124_mutate.sh [WORKTREE]
#         (WORKTREE defaults to the git tree containing $PWD)
#
# ---------------------------------------------------------------------------
# THREE ISOLATIONS THAT ARE NOT OPTIONAL. Each one was measured to be
# load-bearing while this harness was being written, and each initially gave a
# FALSE PASS -- the harness said the pin was effective when the arm was gone:
#
#  1. ASSERT THE DIAGNOSTIC, NOT "an error appeared". #1029's
#     gen_1029_log_budget.py independently pins sd_BuildBucketPath's bucket==0
#     line and runs as a BUILD PREREQUISITE, so it aborts before this pin's
#     recipe is reached. A harness accepting any ^ERROR reported M5 as caught
#     with this pin's arm fully excised -- it was reading another ticket's
#     diagnostic.
#  2. RELAX #1029 FOR M5, so the build actually reaches this pin.
#  3. FORCE THE BINARY TO REBUILD. If a pin aborts the build, a stale
#     run_*_tests from the previous run is still on disk and reports 0 failed.
#
# And the guard in expect(): a pattern that is empty, or starts/ends with an
# escaped alternation, has an EMPTY ALTERNATIVE and therefore matches every
# string -- a clean-bill generator. That exact mistake produced false pass
# number three here, so the pattern is now validated before it is used.
#
# R5 was REMOVED at r10. Its subject -- sd_OpenSessionManifest's own name
# checks -- was deleted by r10 finding 2, because validating the stripped
# intermediate refused three legal configurations (.csv, sub/.csv, noext/) while
# MISSING log.csv/, the case it was written for. ⭐ The harness reported
# "anchor absent" rather than passing, which is the behaviour to preserve: a
# mutant whose target no longer exists must fail loudly, not silently succeed.
#
# And false pass number four, in the other direction: each expected substring
# must appear on the FIRST ^ERROR line, because that is all first_error()
# captures. A substring taken from the diagnostic's second line never matches
# and the arm reads as broken when it is fine.
# ---------------------------------------------------------------------------
set -u

W="${1:-$(git rev-parse --show-toplevel 2>/dev/null || true)}"
[ -n "$W" ] && [ -d "$W/tests/host" ] || { echo "FATAL: '$W' is not a worktree with tests/host."; exit 2; }
W="$(cd "$W" && pwd -P)"
F="$W/firmware/src/services/sd_card_services/sd_card_manager.c"
G="$W/tests/host/gen_1029_log_budget.py"
M="$W/tests/host/Makefile"
S="$W/firmware/src/services/SCPI/SCPIStorageSD.c"
# #1124 r11 finding 4 moved SD_CARD_MANAGER_BUCKET_PREFIX here, and the
# Makefile reads it out of this header, so one arm below mutates it.
H="$W/firmware/src/services/sd_card_services/sd_card_manager.h"
for f in "$F" "$G" "$M" "$S" "$H"; do
    [ -f "$f" ] || { echo "FATAL: $f missing."; exit 2; }
done

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)  branch $(git -C "$W" rev-parse --abbrev-ref HEAD 2>/dev/null)"
echo "################################################################"
if [ -n "$(git -C "$W" status --porcelain -- "$F" "$G" "$M" "$S" "$H" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in the files this mutates. Commit or set them"
    echo "       aside first -- restore here is a byte copy and will not arbitrate."
    exit 2
fi

BF=$(mktemp); BG=$(mktemp); BS=$(mktemp); BH=$(mktemp)
cp "$F" "$BF"; cp "$G" "$BG"; cp "$S" "$BS"; cp "$H" "$BH"
restore() { cp "$BF" "$F"; cp "$BG" "$G"; cp "$BS" "$S"; cp "$BH" "$H"; }
trap 'restore; rm -f "$BF" "$BG" "$BS" "$BH"' EXIT INT TERM
cd "$W/tests/host" || exit 2

pass=0; fail=0

# A pattern that could match anything is worse than no check.
pattern_is_unsafe() {
    [ -z "$1" ] && return 0
    case "$1" in
        *"
"*)        return 0 ;;   # embedded newline: grep treats it as several patterns
        '\|'*|*'\|') return 0 ;;   # leading/trailing alternation: empty branch
    esac
    return 1
}

# Force a rebuild so a stale binary can never answer for a mutant (isolation 3),
# and report THREE outcomes, never two.
#
# ⛔ ISOLATION 4, and the fifth false pass this harness produced. The previous
# version was `make run | grep -m1 '^ERROR' || echo "<<NO ERROR>>"`. If make
# failed any OTHER way -- a compile error, the timeout, a crash -- there is no
# ^ERROR line, so it emitted "<<NO ERROR>>" and the CLEAN arm PASSED. "Did not
# run" collapsed into "ran clean", which is the same two-valued defect that
# makes a -fsyntax-only check print ok over an aborted compile. It only
# surfaces when something unrelated breaks, which is why the four earlier
# false passes never exposed it. The pipeline's exit status is the missing
# third state, so take it from PIPESTATUS before anything else touches it.
first_error() {
    local out rc err
    rm -f run_924_tests
    out=$(timeout 600 make run 2>&1); rc=$?
    err=$(printf '%s\n' "$out" | grep -m1 '^ERROR')
    if [ -n "$err" ]; then
        printf '%s' "$err"
    elif [ "$rc" -ne 0 ]; then
        # No diagnostic of ours, but the build did not succeed: that is NOT a
        # clean run and must never satisfy a CLEAN expectation.
        printf '<<DID NOT RUN: make exited %s with no ERROR line>>' "$rc"
    else
        printf '<<NO ERROR>>'
    fi
}

expect() { # expect <label> <PATTERN|CLEAN>
    local label="$1" want="$2" got
    got="$(first_error)"
    if [ "$want" = CLEAN ]; then
        # EXACT match: "<<DID NOT RUN ...>>" must never satisfy CLEAN.
        if [ "$got" = "<<NO ERROR>>" ]; then
            echo "  OK   $label (ran, and clean)"; pass=$((pass+1))
        else
            echo "  FAIL $label expected a clean RUN, got: $got"; fail=$((fail+1))
        fi
    elif pattern_is_unsafe "$want"; then
        echo "  FAIL $label: the expect pattern itself would match anything."
        fail=$((fail+1))
    elif printf '%s' "$got" | grep -qF -- "$want"; then
        echo "  OK   $label (this pin's own diagnostic)"; pass=$((pass+1))
    else
        echo "  FAIL $label did NOT come from this pin."
        echo "         wanted substring: $want"
        echo "         got:              $got"
        fail=$((fail+1))
    fi
}

sub() { # sub <file> <literal-find> <literal-replace>   (exit 9 if absent)
    python3 -c '
import sys
p, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
t = open(p).read()
if a not in t:
    sys.exit(9)
open(p, "w").write(t.replace(a, b, 1))' "$1" "$2" "$3"
}

echo "== setup: the baseline must be clean, or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

echo "== M1: revert one join to a raw \"%s/, operand on the CONTINUATION line =="
echo "==     (the shape that defeated an operand-keyed enumeration: 2 of 8)  =="
if sub "$F" 'snprintf(path, sizeof(path), "%.*s/%s" SD_MANIFEST_EXT,
                           (int)SdManifest_CanonicalDirLen(gpSDCardSettings->directory),
                           gpSDCardSettings->directory,
                           gSDCardData.baseFilename)' 'snprintf(path, sizeof(path), "%s/%s" SD_MANIFEST_EXT,
                           gpSDCardSettings->directory,
                           gSDCardData.baseFilename)'; then
    expect "M1 count arm" "path construction is no longer"
else
    echo "  FAIL M1 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== M5: revert ONLY sd_BuildBucketPath's bucket==0 arm. raw=0, canon=8,  =="
echo "==     accessor=8 and the function still CALLS the accessor, so every   =="
echo "==     count arm is blind and only the per-arm exact form can see it.   =="
if sub "$G" '"%\.\*s"\s*,\s*dirLen\s*,\s*dir\s*\)\s*;' '"%\.?\*?s"\s*,\s*(?:dirLen\s*,\s*)?dir\s*\)\s*;' \
   && sub "$F" 'written = snprintf(out, outLen, "%.*s", dirLen, dir);' \
               'written = snprintf(out, outLen, "%s", dir);'; then
    expect "M5 per-arm exact-form arm" "sd_BuildBucketPath() in"
else
    echo "  FAIL M5 anchors absent -- source or #1029's generator drifted."; fail=$((fail+1))
fi
restore

echo "== M3: NEGATIVE CONTROL. A raw \"%s/ join inside a COMMENT must NOT fire =="
echo "==     -- a pin that trips on prose is as useless as one that is blind. =="
if sub "$F" '/* #1124: canonicalise the walk ROOT.' \
            '/* old form: snprintf(p, n, "%s/%s", dir, f);
     * #1124: canonicalise the walk ROOT.'; then
    expect "M3 negative control" CLEAN
else
    echo "  FAIL M3 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R1: drop the PROCESS_STATE_OPEN_FILE name guard (arm 1, the NVM arm) =="
if sub "$F" 'if (!SdManifest_NameIsUsable(nameOperand)) {' 'if (false) {'; then
    expect "R1 name-guard count arm" "calls SdManifest_NameIsUsable"
else
    echo "  FAIL R1 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R3: a FILE setter site loses its isFileName flag (role table) =="
if sub "$S" 'SD_ValidatePathParam(pBuff, fileLen, true)' \
            'SD_ValidatePathParam(pBuff, fileLen, false)'; then
    expect "R3 role-table arm" "role table is violated"
else
    echo "  FAIL R3 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R4: the LIST operand's flag flipped back to file (r10 finding 1) =========="
echo "==     SCPI_StorageSDListDir fills opDirectory while its length variable  =="
echo "==     is called fileLen. r9 classified by that NAME and refused          =="
echo "==     LISt? \"DAQiFi/\". The role table keys on the callback, not a count. =="
if sub "$S" 'SD_ValidatePathParam(pBuff, fileLen, false)) {   /* #612 */' \
            'SD_ValidatePathParam(pBuff, fileLen, true)) {   /* #612 */'; then
    expect "R4 role table, directory site" "role table is violated"
else
    echo "  FAIL R4 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R6: THE MUTANT THAT PASSES THE COUNT ARM (r10 finding 3). Keeps THREE  =="
echo "==     fileRecordIsPrefix assignments but moves one OUT of its failure    =="
echo "==     branch, so the unmount record is no longer marked when its bytes   =="
echo "==     are in doubt. Only the POSITION arm sees it.                       =="
if sub "$F" '                         * unmount below can discard them. */
                        gSDCardData.fileRecordIsPrefix = true;
                    }' '                         * unmount below can discard them. */
                    }
                    gSDCardData.fileRecordIsPrefix = true;'; then
    expect "R6 finding-3 position arm" "finding 3 --"
else
    echo "  FAIL R6 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R7: THE DEFECT ITSELF (r11 finding 3). The #924 manifest gate reads the =="
echo "==     LIVE gWriteSessionIsStreamingLog instead of the critical-section    =="
echo "==     snapshot, so a priority-7 SYST:STR:STOP landing after the #782      =="
echo "==     section silently costs a started session its whole manifest.        =="
if sub "$F" 'if (openIsStreamingLog' 'if (gWriteSessionIsStreamingLog'; then
    expect "R7 gate reads the live global" "no longer reads the"
else
    echo "  FAIL R7 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R8: the snapshot is still taken, and still read, but OUTSIDE the #782  =="
echo "==     critical section -- which reintroduces the race wholesale. Count   =="
echo "==     and exact-form arms both pass; only the ORDER arm sees it.         =="
if sub "$F" '                bool openIsStreamingLog = gWriteSessionIsStreamingLog;
                if (!openAborted) {
                    gSdRotating = false;
                }
                taskEXIT_CRITICAL();' '                if (!openAborted) {
                    gSdRotating = false;
                }
                taskEXIT_CRITICAL();
                bool openIsStreamingLog = gWriteSessionIsStreamingLog;'; then
    expect "R8 snapshot outside the critical section" "taken OUTSIDE the #782 critical"
else
    echo "  FAIL R8 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R9: THE THIRD STATE. The #782 anchor is renamed away, so the guard      =="
echo "==     CANNOT evaluate its invariant. It must FAIL, not pass: 'could not   =="
echo "==     evaluate' collapsing into 'holds' is the defect that made four      =="
echo "==     earlier false passes in this very harness invisible.                =="
if sub "$F" 'openAborted = (gpSDCardSettings->mode' 'openWasAborted = (gpSDCardSettings->mode'; then
    expect "R9 anchor gone, invariant unevaluated" "NOT EVALUATED"
else
    echo "  FAIL R9 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R10: THE MUTANT THAT SURVIVES COUNT, ORDER AND THE STALE SWEEP.         =="
echo "==      The snapshot is in the right place and the gate reads it -- but it =="
echo "==      snapshots openNeedsStreamHeader, which is ALSO gated on            =="
echo "==      gSdRotating (false for the session's FIRST file), so no manifest   =="
echo "==      would EVER open. Measured: this passed until the pin was keyed on  =="
echo "==      BOTH SIDES of the assignment. A pin cannot see which value it      =="
echo "==      counted.                                                          =="
if sub "$F" 'bool openIsStreamingLog = gWriteSessionIsStreamingLog;' \
            'bool openIsStreamingLog = openNeedsStreamHeader;'; then
    expect "R10 snapshot of the wrong value" "missing or not in its exact form"
else
    echo "  FAIL R10 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R11: a live read creeps back INTO the success arm while the snapshot    =="
echo "==      stays correct. Order and exact-form arms pass; the stale sweep is  =="
echo "==      the only arm that sees it.                                         =="
if sub "$F" '                    if (openIsStreamingLog' \
            '                    if (gWriteSessionIsStreamingLog) { (void)0; }
                    if (openIsStreamingLog'; then
    expect "R11 live read inside the success arm" "LIVE gWriteSessionIsStreamingLog read is back"
else
    echo "  FAIL R11 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R12: NEGATIVE CONTROL for the r11 arms. A read of the latch placed      =="
echo "==      BEFORE the #782 critical section is outside the guarded window and =="
echo "==      must NOT fire. Without this arm the stale sweep could be a plain   =="
echo "==      identifier-presence detector -- which would also 'catch' every     =="
echo "==      mutant above, for the wrong reason.                                =="
if sub "$F" '                bool openAborted;' \
            '                (void)gWriteSessionIsStreamingLog;
                bool openAborted;'; then
    expect "R12 read outside the guarded window (control)" CLEAN
else
    echo "  FAIL R12 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R13: the SD:DIR setter stops refusing a bucket-shaped directory ======="
echo "==      (r11 finding 4: SD:DIR \"P001\" then accepted, and every manifest  =="
echo "==      name a bucketed session records retrieves a DIFFERENT file)       =="
if sub "$S" 'SdManifest_DirShadowsBucketName(dirCandidate,' \
            'SdManifest_DirShadowsBucketNameXX(dirCandidate,'; then
    expect "R13 setter enforcement site" "SD:DIR setter no longer refuses"
else
    echo "  FAIL R13 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R14: the MANIFEST-OPEN check goes, leaving the setter alone to guard. =="
echo "==      Not equivalent: the setter never sees a directory ALREADY in NVM, =="
echo "==      which is precisely the population that would write a silently     =="
echo "==      misdirecting manifest.                                            =="
echo "==      RENAMED, not deleted, on purpose: the pin keys on the CALL form  =="
echo "==      '...Name(' -- a bare-name grep would still match '...NameXX'   =="
echo "==      and pass. That prefix trap has bitten this row before.          =="
if sub "$F" 'if (SdManifest_DirShadowsBucketName(gpSDCardSettings->directory,' \
            'if (SdManifest_DirShadowsBucketNameXX(gpSDCardSettings->directory,'; then
    expect "R14 manifest-open enforcement site" "no longer refuses to open a manifest"
else
    echo "  FAIL R14 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R15: SD_StripConfiguredDir's prefix compare drifts. The test MODELS    =="
echo "==      that function (SCPIStorageSD.c is not host-compilable), so a      =="
echo "==      silent drift would leave the round-trip test measuring a shape    =="
echo "==      the firmware no longer has -- green, and meaningless.             =="
if sub "$S" 'strncmp(p, directory, dirLen) != 0 || p[dirLen] !=' \
            'memcmp(p, directory, dirLen) != 0 || p[dirLen] !='; then
    expect "R15 strip-model fidelity" "changed shape"
else
    echo "  FAIL R15 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R16: FAIL-CLOSED EXTRACTION. The prefix macro is renamed in the header, =="
echo "==      so the Makefile cannot read it. The test must NOT fall back to a   =="
echo "==      default: a bucket-name guard checked against the wrong string      =="
echo "==      passes while covering nothing.                                     =="
if sub "$H" '#define SD_CARD_MANAGER_BUCKET_PREFIX ' \
            '#define SD_CARD_MANAGER_BUCKET_PREFIX_RENAMED '; then
    expect "R16 prefix extraction fails closed" "could not read SD_CARD_MANAGER_BUCKET_PREFIX"
else
    echo "  FAIL R16 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== restored =="
expect "post-restore baseline" CLEAN

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE PIN IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
fi
[ "$fail" -eq 0 ]
