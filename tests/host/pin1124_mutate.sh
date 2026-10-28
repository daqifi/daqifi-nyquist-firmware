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
# #1124 r23: SdManifest.h is a mutation target now -- PIN 27 reads it.
U="$W/firmware/src/Util/SdManifest.h"
# #1124 r21: PIN 13 re-reads FF_FS_REENTRANT out of this header, because the
# foreign-record scan's TASK placement is a consequence of that setting rather
# than a style choice. An arm that reads a value must be mutable here, or the
# arm is pinned by nothing but its own prose.
C="$W/firmware/src/config/default/system/fs/fat_fs/file_system/ffconf.h"
for f in "$F" "$G" "$M" "$S" "$H" "$C" "$U"; do
    [ -f "$f" ] || { echo "FATAL: $f missing."; exit 2; }
done

echo "################################################################"
echo "# OPERATING ON: $W"
echo "#   HEAD $(git -C "$W" rev-parse HEAD 2>/dev/null | cut -c1-9)  branch $(git -C "$W" rev-parse --abbrev-ref HEAD 2>/dev/null)"
echo "################################################################"
if [ -n "$(git -C "$W" status --porcelain -- "$F" "$G" "$M" "$S" "$H" "$C" "$U" 2>/dev/null)" ]; then
    echo "FATAL: uncommitted changes in the files this mutates. Commit or set them"
    echo "       aside first -- restore here is a byte copy and will not arbitrate."
    exit 2
fi

# ⛔ #1124 r16 MEDIUM: EVERY mktemp AND EVERY cp IS CHECKED, **BEFORE** THE
# EXIT TRAP IS INSTALLED, AND EACH BACKUP IS VERIFIED BYTE-IDENTICAL.
#
# The previous shape was `BF=$(mktemp); ...` then `cp "$F" "$BF"; ...` with no
# status checked, and `trap cleanup EXIT` installed AFTERWARDS. That ordering
# turns a failed backup into source destruction rather than an aborted run: a
# full /tmp, a quota, or a cp that writes partially leaves BF empty or
# truncated, the trap is then installed over it, and the first `restore` --
# which runs on EVERY exit path including the clean one -- copies that
# truncated file OVER the real firmware source. The harness exists to protect
# these files from its own mutants; losing them to its own BACKUP step is the
# one failure it must not have.
#
# cmp is not belt-and-braces: cp can exit 0 having written short on some
# filesystems, and a silently short backup is exactly the case that survives
# until restore. Nothing is trapped until all four are proven good, so an
# abort here leaves the tree untouched by definition.
BF=""; BG=""; BS=""; BH=""; BC=""; BU=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing has been mutated and no trap is installed, so the"
    echo "       worktree is untouched. Fix the backup location and re-run."
    rm -f "$BF" "$BG" "$BS" "$BH" "$BC" "$BU"
    exit 2
}
for v in BF BG BS BH BC BU; do
    tmp=$(mktemp 2>/dev/null) \
      || backup_failed "mktemp failed while creating backup $v"
    [ -n "$tmp" ] && [ -f "$tmp" ] \
      || backup_failed "mktemp returned no usable path for backup $v"
    eval "$v=\$tmp"
done
back_up_one() {  # back_up_one <source> <destination>
    cp "$1" "$2" || backup_failed "cp of $1 failed"
    cmp -s "$1" "$2" \
      || backup_failed "backup of $1 differs from it -- a short or partial copy"
}
back_up_one "$F" "$BF"
back_up_one "$G" "$BG"
back_up_one "$S" "$BS"
back_up_one "$H" "$BH"
back_up_one "$C" "$BC"
back_up_one "$U" "$BU"
restore() { cp "$BF" "$F"; cp "$BG" "$G"; cp "$BS" "$S"; cp "$BH" "$H"; \
            cp "$BC" "$C"; cp "$BU" "$U"; }
cleanup() { restore; rm -f "$BF" "$BG" "$BS" "$BH" "$BC" "$BU"; }
# ⛔ #1124 r12 finding 3. `trap '...' EXIT INT TERM` was NOT equivalent to
# this. On INT or TERM bash runs the handler and then RESUMES the script --
# it does not exit for you -- so the handler restored the tree and the run
# then carried on and re-mutated it. Interrupting this harness (the obvious
# thing to do when it looks stuck mid-mutant) therefore left a MUTATED
# firmware source in the worktree, which is the one outcome a harness that
# edits real sources must never produce. A later build or commit would have
# picked it up.
#
# So the signal handlers exit explicitly, and the EXIT trap -- which `exit`
# itself fires -- is the single place cleanup happens.
trap cleanup EXIT
trap 'printf "\n** INTERRUPTED -- restoring the tree **\n" >&2; exit 130' INT
trap 'printf "\n** TERMINATED -- restoring the tree **\n" >&2; exit 143' TERM
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

GATE_LINE="                if (!sd_TargetSafeToOpenForWrite(gSDCardData.filePath)) {"

echo "== setup: the baseline must be clean, or every result below is noise =="
expect "baseline" CLEAN
if [ "$fail" -ne 0 ]; then
    echo "ABORT: baseline is not clean; mutants would be meaningless."
    exit 1
fi

echo "== M1: revert one join to a raw \"%s/, operand on the CONTINUATION line =="
echo "==     (the shape that defeated an operand-keyed enumeration: 2 of 8)  =="
echo "==     r20: re-pointed at sd_SessionManifestPath, the ONE derivation the  =="
echo "==     gate and creation now share -- the three separate joins this used  =="
echo "==     to mutate were collapsed into it.                                  =="
echo "==     r21: re-pointed again. That derivation moved into                   =="
echo "==     sd_ManifestPathFor(), which takes dir/file as PARAMETERS so the     =="
echo "==     arm-time authorisation snapshot can resolve from pSettings before   =="
echo "==     it reaches the globals. Same single construction, new operands.     =="
if sub "$F" 'snprintf(out, outLen, "%.*s/%s" SD_MANIFEST_EXT,
                           (int)SdManifest_CanonicalDirLen(dir), dir, file);' \
            'snprintf(out, outLen, "%s/%s" SD_MANIFEST_EXT, dir, file);'; then
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

echo "== R21 (#1124 r18): drop the invalidation call, keeping the open. Counts  =="
echo "==        are unchanged and the manifest still opens fine; only PIN A sees =="
echo "==        that a truncated data file keeps a previous session's record.    =="
if sub "$F" "$GATE_LINE" '                if (false) {'; then
    expect "R21 invalidation present" "is not invalidated BEFORE the data file"
else
    echo "  FAIL R21 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R22: MOVE the invalidation AFTER the truncating open. Still present,    =="
echo "==      still reached, still keyed on the target -- a presence check reads =="
echo "==      it as healthy. It would delete the record of data just written, so =="
echo "==      only the ORDER arm of PIN A can see it.                            =="
if sub "$F" "$GATE_LINE" '                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_WRITE_PLUS));
                if (!sd_TargetSafeToOpenForWrite(gSDCardData.filePath)) {'; then
    expect "R22 invalidation ORDER" "is not invalidated BEFORE the data file"
else
    echo "  FAIL R22 anchors absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R26: keep the invalidation but DROP THE REFUSAL -- the r17 fail-OPEN.   =="
echo "==      A .mfst that cannot be removed is logged and the data truncated    =="
echo "==      anyway. The call is present and correctly ordered, so PIN A and    =="
echo "==      PIN A2 both pass; only PIN F sees it.                              =="
if sub "$F" '                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                } else {' '                    /* mutant: refusal discarded */
                }
                {'; then
    expect "R26 refusal on failure" "no longer refuses the"
else
    echo "  FAIL R26 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R24: put a FIRST-OPEN GATE back on the invalidation. That is exactly    =="
echo "==      r17's defect: rotation reaches the same open, and its target can    =="
echo "==      be a previous session's first file with a manifest of its own.      =="
echo "==      Order and presence both still pass; only PIN A2 sees the gate.      =="
if sub "$F" '                if (!sd_TargetSafeToOpenForWrite(gSDCardData.filePath)) {' \
            '                if (isSessionFirstOpen)
                if (!sd_TargetSafeToOpenForWrite(gSDCardData.filePath)) {'; then
    expect "R24 no first-open gate" "is not keyed on the TARGET path"
else
    echo "  FAIL R24 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R27: drop the FatFs-normalisation rejection from the setter. 'exp.csv.'  =="
echo "==      then truncates exp.csv while the manifest name keeps the dot, so    =="
echo "==      exp.csv's real record is never invalidated. Only PIN G sees it.     =="
if sub "$S" "            if (segLen > 0u && (p[i - 1u] == '.' || p[i - 1u] == ' ')) {
                return false;       /* FatFs would normalise this away */
            }" ''; then
    expect "R27 FatFs-normalised names" "no longer rejects a path segment"
else
    echo "  FAIL R27 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R23: unlink WITHOUT the create path's own predicate. Count-safe:       =="
echo "==      sd_ManifestPathSafeToOverwrite drops 3 -> 2 and its own '>= 3'    =="
echo "==      pin DOES fire too, so this arm asserts PIN C's message            =="
echo "==      specifically rather than merely that something failed.            =="
echo "==     r20: the predicate call gained a FAULT out-param.                  =="
echo "==     r21: and lost it again -- the predicate RETURNS sd_InspectResult.   =="
echo "==     \u26d4 THE MUTATION HAD TO CHANGE SHAPE, not just names. Removing the    =="
echo "==     CALL (my first r21 attempt) drops the count 3 -> 2, and the '>= 3'  =="
echo "==     pin fires FIRST with its own diagnostic -- so the arm reported      =="
echo "==     'did not come from this pin' and proved nothing about PIN C. This   =="
echo "==     instead KEEPS the call, and deletes only the FAULT arm's refusal,   =="
echo "==     so the count is untouched and a fault falls through to the remove.  =="
echo "==     That is r19's :2249 defect exactly: a refusal become permission.    =="
if sub "$F" '            LOG_E("[SD1124] unparsable inspect");
            return SD_INSPECT_FAULT;' \
            '            LOG_E("[SD1124] unparsable inspect");
            break;'; then
    expect "R23 unlink-predicate arm" "no longer gates its unlink"
else
    echo "  FAIL R23 anchor absent -- source drifted from this harness."; fail=$((fail+1))
fi
restore

echo "== R25: move the DIRECTORY refusal BELOW the zero-size shortcut. Both      =="
echo "==      checks are still present and a presence grep passes; a directory   =="
echo "==      stats with fsize == 0, so it takes the shortcut and the refusal    =="
echo "==      below is dead. ORDER is the whole property, so only PIN E sees it. =="
if sub "$F" '    if ((st.fattrib & SYS_FS_ATTR_DIR) != 0) {
        LOG_E("[SD] #924 the manifest path is a DIRECTORY - refusing to touch it");
        return SD_INSPECT_HAZARD;
    }
    if (st.fsize == 0u) {
        return SD_INSPECT_CLEAR;    /* nothing there to be destroyed either way */
    }' '    if (st.fsize == 0u) {
        return SD_INSPECT_CLEAR;    /* nothing there to be destroyed either way */
    }
    if ((st.fattrib & SYS_FS_ATTR_DIR) != 0) {
        LOG_E("[SD] #924 the manifest path is a DIRECTORY - refusing to touch it");
        return SD_INSPECT_HAZARD;
    }'; then
    expect "R25 directory-check ORDER" "does not refuse a DIRECTORY"
else
    echo "  FAIL R25 anchor absent -- source drifted from this harness."; fail=$((fail+1))
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

echo "== R13: the manifest records a RELATIVE name again, not the full path. ==="
echo "==      Operator item 8 = B made the full path the record, which is what  =="
echo "==      retired the P###-folder refusal: retrieval also strips SD:DIR, so =="
echo "==      a relative record is stripped TWICE and opens a different file.   =="
echo "==      Mutated to gpSDCardSettings->file -- the realistic wrong choice,  =="
echo "==      recording the bare name -- and chosen because NO pin counts       =="
echo "==      ->file reads, so the ->directory COUNT pin cannot fire first and  =="
echo "==      mask this one. That masking is exactly what happened to me at r14. =="
if sub "$F" '                                        gSDCardData.filePath,' \
            '                                        gpSDCardSettings->file,'; then
    expect "R13 full-path recording" "does not record the FULL path"
else
    echo "  FAIL R13 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R14: the manifest-open delimiter guard stops scanning the DIRECTORY.  =="
echo "==      r13 put the directory into every recorded name, and              =="
echo "==      SD_ValidatePathParam permits a comma, so SD:DIR \"trial,1\" yields  =="
echo "==      a four-field line that PARSES wrongly rather than failing.       =="
echo "==      COUNT-PRESERVING on purpose: the read of ->directory stays, only =="
echo "==      the strpbrk scan goes. Deleting the whole arm drops a            =="
echo "==      ->directory read, which fires the COUNT pin first and aborts the =="
echo "==      recipe before this pin runs -- measured at r14, and the reason   =="
echo "==      this mutant is shaped the way it is.                             =="
if sub "$F" '    if (strpbrk(gpSDCardSettings->directory, ",\r\n") != NULL) {' \
            '    if (gpSDCardSettings->directory[0] == 0x01) {'; then
    expect "R14 directory-delimiter guard" "no longer scans"
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
    expect "R15 strip-model fidelity" "prefix compare in"
else
    echo "  FAIL R15 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R16: NEGATIVE CONTROL for the prefix extraction. An INDENTED #define  =="
echo "==      must still extract. The Makefile\'s sed and #1029\'s defines()      =="
echo "==      parser read the same macro out of the same header; if only one of  =="
echo "==      them tolerated leading whitespace, a harmless reformat would fail  =="
echo "==      the build blaming the prefix. (The fail-CLOSED direction is not    =="
echo "==      provable here: #1029 reads that macro too and aborts first on a    =="
echo "==      rename, so this harness cannot make our own guard speak. Said      =="
echo "==      plainly in the Makefile rather than dressed up as a proven arm.)   =="
if sub "$H" '#define SD_CARD_MANAGER_BUCKET_PREFIX ' \
            '    #define SD_CARD_MANAGER_BUCKET_PREFIX '; then
    expect "R16 indented #define still extracts (control)" CLEAN
else
    echo "  FAIL R16 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R17: the manifest OCCUPANCY CEILING is removed (r12 finding 1). Every =="
echo "==      session then adds a manifest to the parent with no ceiling, and  =="
echo "==      the #689 O(directory size) create wedge comes back.              =="
if sub "$F" 'CountDirEntries(gpSDCardSettings->directory,' \
            'CountDirEntriesXX(gpSDCardSettings->directory,'; then
    expect "R17 occupancy ceiling present" "no longer bounds manifest creation"
else
    echo "  FAIL R17 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R18: THE MUTANT THAT STILL COMPILES AND LOOKS LIKE A WORKING GUARD.   =="
echo "==      filesOnly true -> false. It then counts bucket SUBDIRECTORIES as  =="
echo "==      well, and bucket 0 IS the parent, so the count is already        =="
echo "==      ~MAX_DIR_FILES before the first bucket exists -- the guard        =="
echo "==      refuses EVERY manifest from the moment bucketing began. Not a     =="
echo "==      ceiling: the feature switched off, with a plausible log line.     =="
if sub "$F" 'SD_CARD_MANAGER_MAX_DIR_FILES, true,
                            &parentFsError);' 'SD_CARD_MANAGER_MAX_DIR_FILES, false,
                            &parentFsError);'; then
    expect "R18 filesOnly must be true" "no longer bounds manifest creation"
else
    echo "  FAIL R18 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R19: CountDirEntries loses the filesOnly parameter, so there is no     =="
echo "==      files-only count to ask for and the parent's two budget terms     =="
echo "==      cannot be separated.                                              =="
if sub "$F" 'static uint32_t CountDirEntries(const char* dirPath, uint32_t cap,
                                bool filesOnly, bool* pFsError) {' 'static uint32_t CountDirEntries(const char* dirPath, uint32_t cap,
                                int filesOnly, bool* pFsError) {'; then
    expect "R19 filesOnly stays in the signature" "signature in"
else
    echo "  FAIL R19 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R20: the HEADER-write arm stops marking the record a PREFIX (r12       =="
echo "==      review leg). On an error Harmony has already destroyed FatFs's    =="
echo "==      partial count, so the line would assert the CRC covers a file     =="
echo "==      that may begin with bytes the CRC never saw.                      =="
if sub "$F" '                            gSDCardData.fileRecordIsPrefix = true;
                        }
                        if (hdrWritten > 0) {' '                        }
                        if (hdrWritten > 0) {'; then
    expect "R20 header arm marks the record" "r10 finding 3 --"
else
    echo "  FAIL R20 anchor absent -- source drifted."; fail=$((fail+1))
fi
restore

echo "== R28: drop the DISARM from the refusal branch -- the wedge conv-ts     =="
echo "==      asked about. The refusal is still reported, so every other arm   =="
echo "==      passes; only PIN 6 can see that the card is left BUSY for ever.  =="
if sub "$F" '                    gSDCardData.lastOperationSuccess = false;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;' \
            '                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;'; then
    expect "R28 refusal disarms the session" "no longer disarms the session"
else
    echo "  FAIL R28 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R29: make the gate ignore the opt-in, so it refuses unconditionally.  =="
echo "==      ⭐ GUARDS THE RULING, not just the code: C without a way through =="
echo "==      is A2, which the operator REJECTED because default.bin has no    =="
echo "==      uniquifier and every second recording would be refused.          =="
echo "==     r20 / ruling V=A: the gate reads the SESSION copy now, so that is   =="
echo "==     what this neuters.                                                  =="
if sub "$F" '    overwriteAsked = gSDCardData.overwriteAuthorizedForSession;' \
            '    overwriteAsked = false;'; then
    expect "R29 gate reads the session's opt-in" "no longer reads the SESSION"
else
    echo "  FAIL R29 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R30: read ONE of the two inspection faults as an absence -- r18        =="
echo "==      finding 3, reintroduced on the manifest side only. The count arm  =="
echo "==      is what catches it: the data-file guard is still there.           =="
if sub "$F" '        if (e != SYS_FS_ERROR_NO_PATH && e != SYS_FS_ERROR_NO_FILE) {
            gSDCardData.writeRefuseReason = SD_REFUSE_TARGET_UNREADABLE;
            /* log_budget: max=88 */
            LOG_E("[SD1124] unreadable mfst e=%d: '"'"'%s'"'"'", (int)e, target);
            return false;
        }
        return true;                    /* no manifest at that name */' \
            '        return true;                    /* mutant: fault read as absence */'; then
    expect "R30 both inspections fail closed" "fail-closed inspection guard"
else
    echo "  FAIL R30 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R31: move the [SD1124] tag off the FRONT of a refusal message. The     =="
echo "==      logger truncates the TAIL, so a tag that is not first is what     =="
echo "==      gets lost on the longest paths -- and it is the only thing        =="
echo "==      telling this refusal from any other -200 in SYSTem:LOG?.          =="
if sub "$F" 'LOG_E("[SD1124] exists' \
            'LOG_E("exists [SD1124]'; then
    expect "R31 the tag leads every message" "do not START with the"
else
    echo "  FAIL R31 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R32: delete one refuse reason's case from the text function. It then   =="
echo "==      falls into default: and reports \"no refusal recorded\" about a     =="
echo "==      refusal that DID happen, and no compiler can warn.               =="
if sub "$F" '        case SD_REFUSE_TARGET_EXISTS:
            return "the target file already exists - it is not overwritten "
                   "unless SYSTem:STORage:SD:FILE asked for it";' \
            ''; then
    expect "R32 every reason has a case" "with no case in"
else
    echo "  FAIL R32 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R33: default the overwrite parameter to 1. A one-argument client would =="
echo "==      then inherit an authorisation it never asked for.                 =="
if sub "$S" '    int overwriteParam = 0;' \
            '    int overwriteParam = 1;'; then
    expect "R33 the opt-in defaults to withdrawn" "no longer defaults to 0"
else
    echo "  FAIL R33 anchor absent."; fail=$((fail+1))
fi
restore

# ===========================================================================
# r21 arms. Each pin added or changed in r21 gets a mutant, for the reason
# this whole file exists: PRESENT and EFFECTIVE are different properties.
# r21 itself is the evidence -- it found a live fail-open in a function r20
# wrote while fixing that exact class, and r20's pins all passed over it.
# ===========================================================================

echo "== R34 (PIN 8 call arm): pass something else to the recorded-part       =="
echo "==      predicate. The refusal CONDITION is untouched and still reads    =="
echo "==      != SD_INSPECT_CLEAR, so only the call arm can see this.          =="
if sub "$F" 'sd_NameIsRotationPartOfExistingManifest(gpSDCardSettings->file);' \
            'sd_NameIsRotationPartOfExistingManifest(dataPath);'; then
    expect "R34 the predicate is consulted on the configured name" \
           "does not consult the recorded-part"
else
    echo "  FAIL R34 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R35 (PIN 8 refusal arm): THE r21 REGRESSION, as one token. Narrow the =="
echo "==      refusal from != CLEAR to == HAZARD. The check is still present,  =="
echo "==      still unconditional, still refuses a real recorded part -- it    =="
echo "==      simply stops refusing on a FAULT, which is precisely the r20     =="
echo "==      fail-open r21 removed. Every other arm passes.                   =="
if sub "$F" 'if (partOfOther != SD_INSPECT_CLEAR) {' \
            'if (partOfOther == SD_INSPECT_HAZARD) {'; then
    expect "R35 a FAULT still refuses" "does not refuse a recorded set"
else
    echo "  FAIL R35 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R36 (PIN 10): split the consume into two statements with a read       =="
echo "==      between them -- the #1182 read-modify-write race.                =="
if sub "$F" 'const bool granted = pSettings->overwriteExisting;
            gSDCardData.overwriteAuthorizedForSession = granted;
            pSettings->overwriteExisting = false;' \
            'const bool granted = pSettings->overwriteExisting;
            gSDCardData.overwriteAuthorizedForSession = granted;
            gSDCardData.foreignManifestsPresent = false;
            pSettings->overwriteExisting = false;'; then
    expect "R36 the consume is one transition" "is not consumed as one"
else
    echo "  FAIL R36 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R37 (PIN 11 subject): drop the ELSE arm, so a grant whose subject     =="
echo "==      could not be formed keeps the PREVIOUS session path instead of   =="
echo "==      clearing it. The if-arm is intact, so the happy path is fine and =="
echo "==      only the fail-closed case is lost.                               =="
if sub "$F" ' else {
                gSDCardData.overwriteAuthorizedManifest[0] = '"'"'\0'"'"';
            }' \
            ''; then
    expect "R37 an unformed subject is cleared" "the overwrite grant no longer records"
else
    echo "  FAIL R37 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R38 (PIN 11 tail): move the scan flag OUT of the critical section.    =="
echo "==      Both writes still happen, in the same order, with the same        =="
echo "==      values -- just no longer atomically with the grant.              =="
if sub "$F" '            gSDCardData.foreignScanPending = true;' \
            '        }
        taskEXIT_CRITICAL();
        if ((arm == SD_WRITE_ARM_STREAMING) || (arm == SD_WRITE_ARM_PLAIN)) {
            gSDCardData.foreignScanPending = true;'; then
    expect "R38 the arm block ends under the lock" \
           "the grant, its subject and the scan flag are"
else
    echo "  FAIL R38 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R39 (PIN 12): compare the grant subject with strcmp instead of the    =="
echo "==      FatFs-equivalence comparator. Still a compare, still refuses a   =="
echo "==      moved path -- and now refuses a LEGAL session whose config        =="
echo "==      differs only in case, the #924 self-collision mistake again.     =="
if sub "$F" '|| !SdManifest_PathsEqualCaseInsensitive(
                        authNow, gSDCardData.overwriteAuthorizedManifest)' \
            '|| (strcmp(authNow, gSDCardData.overwriteAuthorizedManifest) != 0)'; then
    expect "R39 the subject compare is case-insensitive" \
           "the open no longer refuses when the"
else
    echo "  FAIL R39 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R40 (PIN 13 count): add a THIRD call site for the scan, in the ARM    =="
echo "==      path -- i.e. FatFs driven from the SCPI task while the SD task   =="
echo "==      may be inside it. FF_FS_REENTRANT is 0, so this is a real data   =="
echo "==      race, and NOTHING else in this suite can see it.                 =="
if sub "$F" '            gSDCardData.foreignScanPending = true;' \
            '            (void)sd_ScanForeignManifests(NULL);
            gSDCardData.foreignScanPending = true;'; then
    expect "R40 the scan has exactly two call sites" "expected 3 mentions of"
else
    echo "  FAIL R40 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R41 (PIN 13 premise): turn FatFs reentrancy ON. The firmware is       =="
echo "==      unchanged and correct for the OLD premise; the pin must notice    =="
echo "==      that the premise behind the scan placement has moved, rather     =="
echo "==      than keep passing because the code still looks the same.         =="
if sub "$C" '#define FF_FS_REENTRANT	0' \
            '#define FF_FS_REENTRANT	1'; then
    expect "R41 the reentrancy premise is re-read" "FF_FS_REENTRANT is now"
else
    echo "  FAIL R41 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R42 (PIN 14): make the unparseable-line arm SKIP instead of refuse --  =="
echo "==      the attractive wrong parser. A corrupt manifest then reads as    =="
echo "==      one recording NOTHING, which is the permissive answer over       =="
echo "==      exactly the input that must refuse.                              =="
if sub "$F" '            if (sep == NULL || sep == line) {' \
            '            if (false) {'; then
    expect "R42 an unparseable line refuses" "the manifest parser lost a bound"
else
    echo "  FAIL R42 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R43 (PIN 15): drop the busy term from TryClaim, so a second session   =="
echo "==      can arm while one is running. The SCPI claim flag still works,   =="
echo "==      so every ordinary sequence passes -- and the r21 prefilter's     =="
echo "==      once-per-session latch silently becomes a stale answer.          =="
if sub "$F" '    got = !gSdScpiClaim && !sd_card_manager_IsBusyLocked();' \
            '    got = !gSdScpiClaim;'; then
    expect "R43 exclusivity is enforced by both terms" \
           "sd_card_manager_TryClaim no longer"
else
    echo "  FAIL R43 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R44 (PIN 16): remove the authorisation from the #409 retained-RAM     =="
echo "==      scrub. Nothing else changes, and the device can boot after a     =="
echo "==      flash already authorised to overwrite.                           =="
if sub "$F" '    gSDCardData.overwriteAuthorizedForSession = false;' \
            ''; then
    expect "R44 the authorisation is scrubbed at init" \
           "the overwrite authorisation and its"
else
    echo "  FAIL R44 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R45 (PIN 17): restore the token form conv-ts found broken -- a comma   =="
echo "==      attached to the reason token. An exact token match for `refuse`    =="
echo "==      then MISSES this line, i.e. fails open, and `refuse` is shared by  =="
echo "==      three reasons anyway. The message is still present, still tagged,  =="
echo "==      still leads with [SD1124], so PIN 4 passes and only PIN 17 sees   =="
echo "==      that the line no longer identifies WHICH refusal it reports.      =="
if sub "$F" "[SD1124] ourpart '%s'" \
            "[SD1124] refuse, recorded part: '%s'"; then
    expect "R45 the reason token is punctuation-free and closed-set" \
           "do not begin with"
else
    echo "  FAIL R45 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R46 (PIN 18): make the BIT the decision -- delete the exact scan and   =="
echo "==      refuse on the bit. The tempting simplification: it removes a      =="
echo "==      whole second pass, and EVERY refusal test still passes, because   =="
echo "==      the refusal still happens in the cases they cover. What it adds   =="
echo "==      is refusals of CORRECT sessions, because the part index does not  =="
echo "==      determine the path -- the bucket is unknown when the bits are     =="
echo "==      built. Only PIN 18 sees it.                                       =="
echo "==      \u26d4 KEEPS THE CALL and discards its result. Deleting the call drops  =="
echo "==      PIN 13's mention count 3 -> 2, so that arm fires first with its    =="
echo "==      own diagnostic and this one proves nothing about PIN 18 -- the     =="
echo "==      same trap R23 fell into twice.                                     =="
if sub "$F" '        const sd_InspectResult rec = sd_ScanForeignManifests(dataPath);' \
            '        (void)sd_ScanForeignManifests(dataPath);
        const sd_InspectResult rec = SD_INSPECT_HAZARD;'; then
    expect "R46 the bit only gates an exact scan" \
           "no longer gates"
else
    echo "  FAIL R46 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R47 (PIN 19): restore the PREFILTER early exit, so the index pass      =="
echo "==      stops at the first foreign manifest. Correct for the old          =="
echo "==      presence check, wrong now: the bitset becomes a PARTIAL record    =="
echo "==      of which indices are claimed, so a collision recorded in a later  =="
echo "==      manifest is never checked. Silent -- every test with ONE foreign  =="
echo "==      manifest still passes.                                            =="
if sub "$F" '        const int w = snprintf(path, sizeof(path), "%.*s/%s",' \
            '        if (needle == NULL) { out = SD_INSPECT_HAZARD; break; }
        const int w = snprintf(path, sizeof(path), "%.*s/%s",'; then
    expect "R47 the index pass reads every manifest" "stops at the FIRST"
else
    echo "  FAIL R47 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R48 (PIN 20): leave a FAULTED pass's half-filled bits in place. This   =="
echo "==      open still refuses, so nothing visible changes -- and a retry     =="
echo "==      then reads a PARTIAL set as a complete answer.                    =="
if sub "$F" '        if (pass != SD_INSPECT_CLEAR) {
            sd_ForeignBitsClear();' \
            '        if (pass != SD_INSPECT_CLEAR) {'; then
    expect "R48 a faulted pass clears the bits" "no longer clears"
else
    echo "  FAIL R48 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R49 (PIN 21): bring the manifest-COUNT cap back -- the audit HIGH.    =="
echo "==      A FAULT is a refusal, so 17 short sessions in one directory      =="
echo "==      (34 files, under MAX_DIR_FILES) disarm every later capture,      =="
echo "==      opt-in or not. Nothing else notices: the scan still works, the   =="
echo "==      bits are still exact, every refusal test still passes.           =="
if sub "$F" '        manifests++;        /* counted for the log only -- never a refusal */' \
            '        if (++manifests > 16u) { out = SD_INSPECT_FAULT; break; }'; then
    expect "R49 no manifest-count cap" "the manifest counter is no longer a"
else
    echo "  FAIL R49 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R50 (PIN 22): identify a .mfst by its EXTENSION again, so a legal     =="
echo "==      DATA file called old.mfst is parsed as a manifest, FAULTs, and   =="
echo "==      blocks every later capture in that directory.                    =="
if sub "$F" '            if (!SdManifest_FirstLineLooksLikeManifest(chunk, got)) {' \
            '            if (false) {'; then
    expect "R50 a manifest is identified by content" "no longer identifies a"
else
    echo "  FAIL R50 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R51 (PIN 23a): accept \",1\" for a name containing '/'. The manifest    =="
echo "==      for sub/exp.csv lives in <dir>/sub, which the scan never reads,  =="
echo "==      so the only check protecting it is blind. This is the state the  =="
echo "==      audit MEASURED as accepted.                                      =="
echo "==      r22: the fileLen>0 guard moved OUT of this condition -- an empty    =="
echo "==      operand is now refused earlier, by PIN 23c's rule -- so the       =="
echo "==      anchor is the bare memchr test.                                   =="
if sub "$S" "        if (memchr(pBuff, '/', fileLen) != NULL) {" \
            "        if (false) {"; then
    expect "R51 the opt-in refuses a separator in the name" \
           "PIN 23a: the opt-in accepts a SEPARATOR"
else
    echo "  FAIL R51 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R52 (PIN 23b): accept \",1\" while SD:DIR is a rotation bucket. The     =="
echo "==      manifest owning the records in D/P001 lives in D, unscanned.    =="
if sub "$S" '        if (SD_DirHasBucketComponent(pSDCardRuntimeConfig->directory)) {' \
            '        if (false) {'; then
    expect "R52 the opt-in refuses a bucket directory" \
           "PIN 23b: the opt-in accepts a BUCKET"
else
    echo "  FAIL R52 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R53 (PIN 24): move the generation compare OUT of the commit critical  =="
echo "==      section. The snapshot is still taken, the counter is still       =="
echo "==      bumped, the compare still happens and still refuses -- it is     =="
echo "==      just another stale fast path, which is the defect itself.        =="
if sub "$S" '        dirMoved = (gSdDirGeneration != dirGenAtStrip);
        if (!benchOwnsTarget && !dirMoved) {' \
            '        if (!benchOwnsTarget) {'; then
    expect "R53 the generation compare is inside the commit section" \
           "PIN 24 lost: dirMoved = (gSdDirGeneration != dirGenAtStrip);"
else
    echo "  FAIL R53 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R54 (PIN 24): drop the generation BUMP, leaving the snapshot and the  =="
echo "==      compare in place. They then always agree, so the check is        =="
echo "==      present and can never fire -- the inert-guard shape.            =="
if sub "$S" """        gSdDirGeneration++;""" \
            """        (void)0;"""; then
    expect "R54 the generation is bumped on every dir write" \
           "PIN 24 lost: pSDCardRuntimeConfig->directory[pathLen]"
else
    echo "  FAIL R54 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R55 (PIN 21, the COMPARISON arm): keep the plain increment AND add a  =="
echo "==      separate cap beside it. PIN 21's first arm is satisfied -- the    =="
echo "==      increment still stands alone -- so only the arm that forbids a   =="
echo "==      relational test on the counter can see it. Two arms, two         =="
echo "==      mutants: R49 trips the first, this trips the second.             =="
if sub "$F" '        manifests++;        /* counted for the log only -- never a refusal */' \
            '        manifests++;        /* counted for the log only -- never a refusal */
        if (manifests > 16u) { out = SD_INSPECT_FAULT; break; }'; then
    expect "R55 the manifest count is never compared" \
           "the manifest COUNT is compared"
else
    echo "  FAIL R55 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R56 (PIN 18, the above-limit term): drop the OPEN-side half of the    =="
echo "==      r25 fix -- the index pass still records the flag, the bit arm    =="
echo "==      still works, and only a counter past the bitset stops consulting =="
echo "==      the exact check. The false negative returns, silently.           =="
echo "==      r25: re-pointed. Its old target (the pre-r25 skip) no longer     =="
echo "==      exists, and a mutant for the index-pass side would be            =="
echo "==      indistinguishable from R68 -- so this takes the OTHER side.      =="
if sub "$F" '            && (sd_ForeignBitTest(gSDCardData.fileCounter)
                || ((gSDCardData.fileCounter >= SD_FOREIGN_PART_LIMIT)
                    && gSDCardData.foreignAboveLimitRecord))) {' \
            '            && sd_ForeignBitTest(gSDCardData.fileCounter)) {'; then
    expect "R56 the open consults the above-limit flag" "PIN 18: a set foreign-part bit no"
else
    echo "  FAIL R56 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R57 (PIN 26): drop the PARENT walk, so a DEFAULT session logging into =="
echo "==      a bucket can recreate a part the parent's manifest still         =="
echo "==      records. No opt-in involved, so every opt-in test passes.        =="
if sub "$F" '            out = sd_ScanOneDirectory(parent, false, needle);' \
            '            (void)parent;'; then
    expect "R57 a bucket dir scans its parent" "PIN 26a: the PARENT walk is gone"
else
    echo "  FAIL R57 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R58 (PIN 26, the isOwnDir arm): scan the parent but EXCLUDE our own   =="
echo "==      manifest leaf there. The walk is present and looks right; it     =="
echo "==      simply skips the one manifest it went to find -- a session       =="
echo "==      configured at the PARENT with the same name.                     =="
if sub "$F" '            out = sd_ScanOneDirectory(parent, false, needle);' \
            '            out = sd_ScanOneDirectory(parent, true, needle);'; then
    expect "R58 the parent walk does not exclude our leaf" \
           "PIN 26b: the parent walk no longer passes"
else
    echo "  FAIL R58 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R59 (PIN 23c): accept an EMPTY operand with \",1\". The grant then goes  =="
echo "==      to whatever name was already staged -- including one the '/' or  =="
echo "==      '~' rules had just refused on its own command.                   =="
if sub "$S" '        if (fileLen == 0) {' \
            '        if (false) {'; then
    expect "R59 the opt-in requires a name" "accepts an EMPTY operand"
else
    echo "  FAIL R59 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R60 (PIN 23d): accept a TILDE in the NAME, so an 8.3 alias names a    =="
echo "==      file whose long name compares unequal to it -- and the session   =="
echo "==      also WRITES records spelled by alias. REWRITTEN at r28: the old  =="
echo "==      arm neutered the opt-in-only memchr, which no longer exists.     =="
if sub "$S" "        if (SdManifest_PathHasAliasRiskLen(pBuff, fileLen)) {" \
            "        if (false) {"; then
    expect "R60 the name arm refuses a tilde" "are missing, duplicated, or"
else
    echo "  FAIL R60 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R78 (PIN 23d): accept a TILDE in the DIRECTORY. Its OWN arm, because  =="
echo "==      main :1119's repro names the alias as the DIRECTORY, not the     =="
echo "==      file, and a by-name count cannot tell which arm complies.        =="
if sub "$S" "    if (SdManifest_PathHasAliasRiskLen(pBuff, pathLen)) {" \
            "    if (false) {"; then
    expect "R78 the directory arm refuses a tilde" "are missing, duplicated, or"
else
    echo "  FAIL R78 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R79 (PIN 23d): accept a name ending in the MANIFEST EXTENSION, so a   =="
echo "==      session named exp.csv.mfst writes exp.csv.mfst.mfst and leaves   =="
echo "==      its data file occupying a later exp.csv session's manifest name. =="
if sub "$S" "        if (SdManifest_NameHasManifestExtLen(pBuff, fileLen)) {" \
            "        if (false) {"; then
    expect "R79 the setter refuses a .mfst name" "are missing, duplicated, or"
else
    echo "  FAIL R79 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R80 (PIN 23f): drop the WRITE-ONLY guard on the at-use refusals, so   =="
echo "==      SD:GET of a manifest and SD:DELete of the file in the way are    =="
echo "==      refused too -- a worse regression than the hazard they close.    =="
if sub "$F" "                if (!usesOperandName) {" \
            "                if (true) {"; then
    expect "R80 the at-use refusals are write-only" "PIN 23f"
else
    echo "  FAIL R80 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R81 (PIN 23f): keep the NAME arm at use and drop the DIRECTORY arm.   =="
echo "==      This is the half a single \"||\" expression would have hidden:      =="
echo "==      the call text stays present, so only a per-arm pin sees it.      =="
if sub "$F" "                            SdManifest_PathHasAliasRisk(gpSDCardSettings->directory);" \
            "                            false;"; then
    expect "R81 the at-use directory arm exists" "PIN 23f"
else
    echo "  FAIL R81 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R82 (test_924): let the SHARED PARSER accept a leading zero again,    =="
echo "==      which is BOTH the padded case and the lone \"-0\". Caught by       =="
echo "==      behaviour, not by a pin: this is the one arm where the rule's    =="
echo "==      body is host-reachable and can be tested by value.               =="
if sub "$U" "    if (d[0] == '0') {" \
            "    if (false) {"; then
    expect "R82 the parser rejects a leading zero" "a_padded_index_is_never_one_of_our_parts"
else
    echo "  FAIL R82 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R83 (test_924): unbound the parser's index, so a foreign name with an =="
echo "==      absurd suffix maps onto an index no session can write.           =="
if sub "$U" "    if ((idx < 1u) || (idx > SDMANIFEST_PART_INDEX_MAX)) {" \
            "    if (false) {"; then
    expect "R83 the parser bounds its index" "part_suffix_parser_bounds_the_index"
else
    echo "  FAIL R83 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R84 (PIN 37): remove the _Static_assert tying the parser's ceiling to =="
echo "==      SD_FOREIGN_PART_LIMIT. Without it the header's literal is a      =="
echo "==      SECOND source of truth and a drift is silent in every other      =="
echo "==      instrument -- the host suite never compiles that .c file.        =="
if sub "$F" "_Static_assert(SDMANIFEST_PART_INDEX_MAX == (SD_FOREIGN_PART_LIMIT - 1u)," \
            "_Static_assert(1 == 1,"; then
    expect "R84 the ceiling is cross-tied" "PIN 37"
else
    echo "  FAIL R84 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R61 (PIN 23e): accept a non-canonical DIR form, where the text and    =="
echo "==      the resolved path can diverge and the record comparison is       =="
echo "==      textual.                                                         =="
if sub "$S" '        if (SD_DirHasNonCanonicalForm(pSDCardRuntimeConfig->directory)) {' \
            '        if (false) {'; then
    expect "R61 the opt-in refuses a non-canonical dir" "accepts a NON-CANONICAL"
else
    echo "  FAIL R61 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R62 (PIN 27): bring the FAULT back for an overflowing index -- the    =="
echo "==      r23 audit MEDIUM. A foreign file called exp-4294967296.csv then  =="
echo "==      disarms every capture in that directory. Keeps the 3-arg call,   =="
echo "==      so only the header arm sees it.                                  =="
if sub "$U" 'static inline bool SdManifest_NameHasManifestExt(const char *name)' \
            'static inline uint32_t SdManifest_PartIndexOf2(const char *name,
                                                const char *dir,
                                                const char *file,
                                                bool *fault)
{
    (void)dir; (void)file;
    if (fault != NULL) { *fault = (name == NULL); }
    return 0u;
}

static inline bool SdManifest_NameHasManifestExt(const char *name)'; then
    expect "R62 no fault out-param in the header" "declares a fault out-param"
else
    echo "  FAIL R62 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R63 (PIN 28b): drop the DOWNWARD scan, so a manifest written INSIDE a =="
echo "==      bucket child is never read. No opt-in involved, so every opt-in  =="
echo "==      arm passes, and the UPWARD scan still works so PIN 28a passes.   =="
if sub "$F" '            out = sd_ScanOneDirectory(child, false, needle);' \
            '            (void)child;'; then
    expect "R63 the downward scan exists" "PIN 28b: the DOWNWARD scan is gone"
else
    echo "  FAIL R63 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R64 (PIN 28c): stop the downward probe at the first ABSENT bucket.     =="
echo "==      Correct on a conforming card (#689 creates them in order), and a =="
echo "==      user who deleted one intermediate directory then has every later =="
echo "==      bucket skipped -- a missed collision, silently.                  =="
if sub "$F" '        for (b = 1u; (b <= SD_CARD_MANAGER_MAX_BUCKET)
                     && (out == SD_INSPECT_CLEAR); b++) {' \
            '        for (b = 1u; (b <= 1u)
                     && (out == SD_INSPECT_CLEAR); b++) {'; then
    expect "R64 every bucket index is probed" "no longer probes EVERY"
else
    echo "  FAIL R64 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R65 (PIN 29): gate index 0 on !inBucket again -- the r24 audit HIGH.   =="
echo "==      An unsuffixed FIRST file inside a bucket (which happens whenever =="
echo "==      the configured directory is already full) then sets no bit, so   =="
echo "==      the exact check never runs. Fail-OPEN, and it reads as a          =="
echo "==      deliberate scoping choice.                                       =="
echo "==      r28: re-pointed -- fix 2 renamed the operands to fileLeaf/        =="
echo "==      fileLeafLen and the variable to bucketPresent.                    =="
if sub "$U" '    if (SdManifest_SegmentsEqual(leaf, leafLen, fileLeaf, fileLeafLen)) {
        return 0u;
    }' \
            '    if (!bucketPresent && SdManifest_SegmentsEqual(leaf, leafLen, fileLeaf, fileLeafLen)) {
        return 0u;
    }'; then
    expect "R65 index 0 is not gated on the bucket" "index 0 is gated on the bucket again"
else
    echo "  FAIL R65 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R66 (PIN 30): drop the '~' refusal for the DIRECTORY, keeping it for  =="
echo "==      the filename. An 8.3 alias in a directory component then gets    =="
echo "==      the opt-in, and diverges from its long name exactly as one in    =="
echo "==      the name would. The filename arm still passes.                   =="
if sub "$S" """    if (strchr(dir, '~') != NULL) {
        return true;
    }""" '    if (false) {
        return true;
    }'; then
    expect "R66 a tilde in the directory refuses the grant" \
           "a '~' in the configured DIRECTORY no"
else
    echo "  FAIL R66 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R67 (PIN 31): FAULT on a foreign name too long for the path buffer.   =="
echo "==      One 100-character *.mfst in the directory then refuses every     =="
echo "==      capture there. Violates this module's own PIN 27 rule, which is  =="
echo "==      why both pins exist.                                             =="
if sub "$F" '        if (w < 0 || (size_t)w >= sizeof(path)) {' \
            '        if (w < 0 || (size_t)w >= sizeof(path)) {
            out = SD_INSPECT_FAULT;
            break;
        }
        if (false) {'; then
    expect "R67 an unconstructable name is skipped" \
           "PIN 31: a foreign manifest name that will not"
else
    echo "  FAIL R67 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R68 (PIN 25): SKIP an above-limit index again -- the r25 audit HIGH,  =="
echo "==      and the only FALSE NEGATIVE this row has produced. The session   =="
echo "==      still arms, still logs, still refuses every other collision; it  =="
echo "==      simply stops seeing one, silently. Nothing else notices.         =="
if sub "$F" '                        gSDCardData.foreignAboveLimitRecord = true;' \
            '                        (void)0;'; then
    expect "R68 an above-limit index routes to the exact check" \
           "PIN 25: an index past SD_FOREIGN_PART_LIMIT no"
else
    echo "  FAIL R68 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R69 (PIN 18): widen the gate but drop the exact scan behind it, so    =="
echo "==      the above-limit route REFUSES on the flag alone. Over-refuses    =="
echo "==      correct sessions; the bit arm still looks right.                 =="
if sub "$F" '        const sd_InspectResult rec = sd_ScanForeignManifests(dataPath);' \
            '        (void)sd_ScanForeignManifests(dataPath);
        const sd_InspectResult rec = SD_INSPECT_HAZARD;'; then
    expect "R69 the widened gate still ends in the exact scan" "no longer gates"
else
    echo "  FAIL R69 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R70 (PIN 32a): drop the DIRECTORY ASCII refusal. A non-ASCII dir      =="
echo "==      component then gets the opt-in, and FatFs folds it by code page  =="
echo "==      while we fold ASCII only -- so a foreign record naming our file  =="
echo "==      is not matched. The filename arm still passes.                   =="
if sub "$S" '            if ((u[i] < 0x20u) || (u[i] > 0x7Eu)) {
                return true;
            }' '            if (false) {
                return true;
            }'; then
    expect "R70 a non-ASCII directory refuses the grant" "PIN 32a: the DIRECTORY no longer"
else
    echo "  FAIL R70 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R71 (PIN 32): use SIGNED chars for the ASCII range test. Every high   =="
echo "==      byte then compares as negative, so `> 0x7E` is false and the     =="
echo "==      check passes exactly the input it exists to catch -- present,    =="
echo "==      plausible, and inert.                                            =="
if sub "$S" '        const unsigned char *u = (const unsigned char *)dir;' \
            '        const char *u = dir;'; then
    expect "R71 the ASCII test uses unsigned bytes" "unsigned-char casts"
else
    echo "  FAIL R71 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R72 (PIN 33): bring the manifest LINE cap back -- the r26 audit        =="
echo "==      MEDIUM, and the 4th wrong-dimension bound. A manifest this       =="
echo "==      firmware itself can produce then disarms the next capture in     =="
echo "==      that directory. The byte bound still works, so the read is still =="
echo "==      bounded and nothing else notices.                                =="
if sub "$F" '            lines++;    /* counted for the diagnostics below, never a cap */' \
            '            if (++lines > 4161u) { out = SD_INSPECT_FAULT; break; }'; then
    expect "R72 no manifest line cap" "the line counter is no longer a plain"
else
    echo "  FAIL R72 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R73 (PIN 33): drop the BYTE bound, so a non-terminating manifest is   =="
echo "==      unbounded work on the SD task. The line counter is untouched, so =="
echo "==      the no-cap arms all pass.                                        =="
if sub "$F" '        if (got > (size_t)(SD_FOREIGN_SCAN_MAX_BYTES - bytesRead)) {' \
            '        if (false) {'; then
    expect "R73 the read is bounded by bytes" "the byte bound on the manifest read is"
else
    echo "  FAIL R73 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R74 (PIN 33, the COMPARISON arm): keep the plain `lines++` AND add a  =="
echo "==      cap beside it. Arm 1 is satisfied -- the increment still stands   =="
echo "==      alone -- so only the arm forbidding a relational test on the      =="
echo "==      counter can see the refusal come back.                            =="
echo "==      \u26a0 SECOND time this pair has been needed (PIN 25/R49/R55 was the   =="
echo "==      first): a pin with a plain-increment arm AND a never-compared arm =="
echo "==      needs TWO mutants, because removing the increment answers arm 1   =="
echo "==      and never reaches arm 2.                                          =="
if sub "$F" '            lines++;    /* counted for the diagnostics below, never a cap */' \
            '            lines++;    /* counted for the diagnostics below, never a cap */
            if (lines > 4161u) { out = SD_INSPECT_FAULT; break; }'; then
    expect "R74 the manifest line count is never compared" \
           "the manifest LINE count is compared"
else
    echo "  FAIL R74 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R75 (PIN 35): treat the configured name as ONE segment again -- the   =="
echo "==      r28 audit HIGH. A name containing '/' then goes unmatched, so no =="
echo "==      bit is set and the exact check never runs. Fail-OPEN even for a  =="
echo "==      directory the scan DOES read, and every single-segment test      =="
echo "==      still passes.                                                    =="
if sub "$U" '    fileSegs = SdManifest_CountSegments(file);' \
            '    fileSegs = 1u;'; then
    expect "R75 the configured name is segment-walked" "no longer"
else
    echo "  FAIL R75 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R76 (PIN 36): return after the UPWARD scan, so a session configured   =="
echo "==      in a bucket never reads its own bucket children -- where #689 is =="
echo "==      about to put its parts. The parent scan still works, so PIN 28a  =="
echo "==      passes.                                                          =="
echo "==      \u26a0 KEEPS the call, so PIN 28a stays satisfied and only PIN 36      =="
echo "==      sees the lost fall-through. Converting it to a bare `return`      =="
echo "==      removes the assignment too and 28a answers first -- the           =="
echo "==      one-invariant-per-mutant rule again.                              =="
if sub "$F" '            if (out != SD_INSPECT_CLEAR) {
                return out;
            }' \
            '            return out;'; then
    expect "R76 the upward scan falls through to the downward" \
           "no longer falls through"
else
    echo "  FAIL R76 anchor absent."; fail=$((fail+1))
fi
restore

echo "== R77 (PIN 37): stop calling the SHARED parser, so EVERY digit run is   =="
echo "==      a part again -- exp-01.csv, exp-0.csv and exp-4294967296.csv all =="
echo "==      strip to exp, find an unrelated exp.csv.mfst, and refuse a fresh =="
echo "==      capture this firmware could never have written. REWRITTEN at r28: =="
echo "==      the old arm neutered an inlined condition that is now gone.      =="
if sub "$F" "    if (SdManifest_ParsePartIndexDigits(dash + 1, end) == SDMANIFEST_PART_NONE) {" \
            '    if (false) {'; then
    expect "R77 the recorded-part check calls the shared parser" "is not SHARED"
else
    echo "  FAIL R77 anchor absent."; fail=$((fail+1))
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
