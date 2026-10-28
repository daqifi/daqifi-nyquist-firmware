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
BF=""; BG=""; BS=""; BH=""
backup_failed() {
    echo "FATAL: $1"
    echo "       Nothing has been mutated and no trap is installed, so the"
    echo "       worktree is untouched. Fix the backup location and re-run."
    rm -f "$BF" "$BG" "$BS" "$BH"
    exit 2
}
for v in BF BG BS BH; do
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
restore() { cp "$BF" "$F"; cp "$BG" "$G"; cp "$BS" "$S"; cp "$BH" "$H"; }
cleanup() { restore; rm -f "$BF" "$BG" "$BS" "$BH"; }
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
if sub "$F" 'snprintf(out, outLen, "%.*s/%s" SD_MANIFEST_EXT,
                           (int)SdManifest_CanonicalDirLen(gpSDCardSettings->directory),' \
            'snprintf(out, outLen, "%s/%s" SD_MANIFEST_EXT,
                           (gpSDCardSettings->directory),'; then
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
if sub "$F" '    if (!sd_ManifestPathSafeToOverwrite(path, &inspectFault)) {' \
            '    if (0) {'; then
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
        return false;
    }
    if (st.fsize == 0u) {
        return true;    /* nothing there to be destroyed either way */
    }' '    if (st.fsize == 0u) {
        return true;    /* nothing there to be destroyed either way */
    }
    if ((st.fattrib & SYS_FS_ATTR_DIR) != 0) {
        LOG_E("[SD] #924 the manifest path is a DIRECTORY - refusing to touch it");
        return false;
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
            LOG_E("[SD1124] mfst stat e=%d: '"'"'%s'"'"'", (int)e, target);
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
if sub "$F" 'LOG_E("[SD1124] refuse overwrite:' \
            'LOG_E("refuse overwrite [SD1124]:'; then
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

echo "== restored =="
expect "post-restore baseline" CLEAN

echo
echo "RESULT: $pass passed, $fail failed"
if [ "$fail" -ne 0 ]; then
    echo "THE PIN IS PRESENT BUT NOT EFFECTIVE -- an arm was lost. Do not merge."
fi
[ "$fail" -eq 0 ]
